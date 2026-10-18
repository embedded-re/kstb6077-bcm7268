// SPDX-License-Identifier: GPL-2.0
/*
 * kstb-drm.c - KMS driver for the KSTB6077's HDMI output
 *
 * BOLT's boot splash (or kmon's `video init`) sets up the display at
 * 1920x1080, 59.94 Hz, RGB565, pitch 3840, and it keeps running under Linux
 * (docs/hardware/display.md). This driver doesn't touch the timing; it only
 * changes what the display scans out:
 *
 * - one fixed mode, one primary plane, buffers from CMA (DMA GEM);
 * - formats: the graphics feeder's two format registers (GFD 0xf0641014/18,
 *   bits per channel and each channel's lowest bit, as Nexus derives them
 *   from its pixel format code) and its pitch register (0xf0641058); the
 *   display lists write them only at set-up, so a direct write stays.
 *   Without the "gfd-format" and "gfd-pitch" resources (an older device
 *   tree), RGB565 with pitch 3840 only. 32-bit pixels always have alpha:
 *   the feeder applies the fourth byte and the compositor blends the
 *   picture over its background, so there is no XRGB8888 (a client
 *   writing 0 there would get the background);
 * - page flip: the buffer's address into the two flip registers 0xf0603488 and
 *   0xf060348c; the RDC copies it into the graphics feeder (GFD 0xf0641048) at
 *   the next frame, the whole frame at once;
 * - vblank: BVNF_0 L2 bit 0 latches once per frame (GIC 49), always
 *   unmasked; a flip's event goes out at the first one after the GFD reads
 *   the new address;
 * - fbdev emulation for the text console; simplefb on BOLT's framebuffer is
 *   removed when this driver takes over.
 *
 * BOLT's framebuffer 0x7db0b700 is never freed (not Linux RAM): it is shown
 * again whenever the plane or CRTC is off, so the display never reads a
 * buffer Linux has freed.
 */
#include <linux/aperture.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#define WIDTH		1920
#define HEIGHT		1080
#define PITCH		3840		/* BOLT's: RGB565 */

/* "rdc" registers, from 0xf0603484 */
#define RDC_FRAME	0x0		/* counts up once per frame */
#define RDC_FLIP0	0x4		/* the surface address the RDC copies into the GFD */
#define RDC_FLIP1	0x8		/* same value, written together with FLIP0 */

struct kstb_drm {
	struct drm_device drm;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	void __iomem *rdc;
	void __iomem *gfd;		/* GFD surface address: what is shown now */
	void __iomem *gfd_format;	/* GFD format registers 1 and 2, or NULL */
	void __iomem *gfd_pitch;	/* GFD pitch register */
	u32 format;			/* DRM format the GFD is set to */
	u32 pitch;
	u32 boot_fb;			/* BOLT's framebuffer */
	u32 shown;			/* last address written to the flip registers */
	struct drm_pending_vblank_event *event;	/* sent once the GFD has `shown` */
	int irq;
};

#define to_kstb(d) container_of(d, struct kstb_drm, drm)

/* 1920x1080 at 59.94 Hz: CEA-861 VIC 16 with the clock divided by 1.001 */
static const struct drm_display_mode kstb_mode = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED, 148352,
		 1920, 2008, 2052, 2200, 0, 1080, 1084, 1089, 1125, 0,
		 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
};

/* GFD format registers: 1 = type << 16 | bits of channels 3..0 (alpha,
 * red, green, blue), a nibble each; 2 = each channel's lowest bit, a byte
 * each, 0 for a channel without bits. Type 2 is Nexus's for RGB565, 0 for
 * 32-bit RGB (other types tested: 1 takes the alpha from elsewhere, 3-5
 * are palette and alpha-only formats in Nexus; leave them alone).
 */
static const struct kstb_format {
	u32 drm, def1, def2;
} kstb_formats_gfd[] = {
	{ DRM_FORMAT_RGB565,   0x00020565, 0x000b0500 },	/* BOLT's */
	{ DRM_FORMAT_BGR565,   0x00020565, 0x0000050b },
	{ DRM_FORMAT_ARGB8888, 0x00008888, 0x18100800 },
	{ DRM_FORMAT_ABGR8888, 0x00008888, 0x18000810 },
};

static void kstb_set_format(struct kstb_drm *k, u32 format, u32 pitch)
{
	int i;

	if (!k->gfd_format || (format == k->format && pitch == k->pitch))
		return;
	for (i = 0; i < ARRAY_SIZE(kstb_formats_gfd); i++)
		if (kstb_formats_gfd[i].drm == format)
			break;
	if (WARN_ON(i == ARRAY_SIZE(kstb_formats_gfd)))
		return;
	writel(kstb_formats_gfd[i].def1, k->gfd_format);
	writel(kstb_formats_gfd[i].def2, k->gfd_format + 4);
	writel(pitch, k->gfd_pitch);
	k->format = format;
	k->pitch = pitch;
}

static void kstb_flip(struct kstb_drm *k, u32 addr)
{
	k->shown = addr;
	writel(addr, k->rdc + RDC_FLIP0);
	writel(addr, k->rdc + RDC_FLIP1);
}

/* show BOLT's framebuffer and wait until the display has taken it (a frame
 * is 16.7 ms), so the caller may free the buffer shown before
 */
static void kstb_show_boot_fb(struct kstb_drm *k)
{
	u32 v;

	kstb_set_format(k, DRM_FORMAT_RGB565, PITCH);
	kstb_flip(k, k->boot_fb);
	if (readl_poll_timeout(k->gfd, v, v == k->boot_fb, 500, 100000))
		drm_warn(&k->drm, "display still shows %08x\n", v);
}

/* plane */

static int kstb_plane_atomic_check(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct drm_plane_state *ps = drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *cs;
	int ret;

	if (!ps->crtc)
		return 0;
	cs = drm_atomic_get_new_crtc_state(state, ps->crtc);
	ret = drm_atomic_helper_check_plane_state(ps, cs, DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING, false, true);
	if (ret || !ps->visible)
		return ret;
	/* x must stay 0 to keep the address's alignment (a multiple of
	 * 256 in every tested case); without the format registers, only
	 * BOLT's format and pitch
	 */
	if (ps->src.x1 || (ps->fb->pitches[0] & 255) ||
	    (!to_kstb(plane->dev)->gfd_format &&
	     (ps->fb->format->format != DRM_FORMAT_RGB565 || ps->fb->pitches[0] != PITCH))) {
		drm_dbg_kms(plane->dev, "%p4cc pitch %u x %u not supported\n",
			    &ps->fb->format->format, ps->fb->pitches[0], ps->src.x1 >> 16);
		return -EINVAL;
	}
	return 0;
}

static void kstb_plane_atomic_update(struct drm_plane *plane, struct drm_atomic_state *state)
{
	struct kstb_drm *k = to_kstb(plane->dev);
	struct drm_plane_state *ps = drm_atomic_get_new_plane_state(state, plane);

	if (!ps->fb || !ps->visible)
		return;
	kstb_set_format(k, ps->fb->format->format, ps->fb->pitches[0]);
	kstb_flip(k, drm_fb_dma_get_gem_addr(ps->fb, ps, 0));
}

static void kstb_plane_atomic_disable(struct drm_plane *plane, struct drm_atomic_state *state)
{
	kstb_show_boot_fb(to_kstb(plane->dev));
}

static const struct drm_plane_helper_funcs kstb_plane_helper_funcs = {
	.atomic_check = kstb_plane_atomic_check,
	.atomic_update = kstb_plane_atomic_update,
	.atomic_disable = kstb_plane_atomic_disable,
};

static const struct drm_plane_funcs kstb_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static const u32 kstb_formats[] = {
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565, DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
};
static const u64 kstb_modifiers[] = { DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID };

/* CRTC */

static int kstb_crtc_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *cs = drm_atomic_get_new_crtc_state(state, crtc);

	if (cs->enable && !drm_mode_match(&cs->mode, &kstb_mode, DRM_MODE_MATCH_TIMINGS |
					  DRM_MODE_MATCH_CLOCK | DRM_MODE_MATCH_FLAGS))
		return -EINVAL;
	return 0;	/* with the plane off, BOLT's framebuffer is shown */
}

static void kstb_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);	/* the display itself always runs */
}

static void kstb_crtc_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct kstb_drm *k = to_kstb(crtc->dev);

	kstb_show_boot_fb(k);
	spin_lock_irq(&crtc->dev->event_lock);
	if (k->event) {			/* a flip still waiting for its frame */
		drm_crtc_send_vblank_event(crtc, k->event);
		k->event = NULL;
		drm_crtc_vblank_put(crtc);
	}
	spin_unlock_irq(&crtc->dev->event_lock);
	drm_crtc_vblank_off(crtc);
	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

/* after the plane's flip: its event waits for the frame that shows it */
static void kstb_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct kstb_drm *k = to_kstb(crtc->dev);
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (!event || !crtc->state->active)
		return;
	crtc->state->event = NULL;
	spin_lock_irq(&crtc->dev->event_lock);
	if (drm_crtc_vblank_get(crtc) == 0) {
		WARN_ON(k->event);
		k->event = event;
	} else {
		drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

/* The interrupt stays unmasked: its status bit latches every frame even
 * while masked, so unmasking it fires at once for a frame long past (and a
 * flip would look done a frame early). 60 interrupts a second cost nothing.
 */
static int kstb_enable_vblank(struct drm_crtc *crtc)
{
	return 0;
}

static void kstb_disable_vblank(struct drm_crtc *crtc)
{
}

static const struct drm_crtc_helper_funcs kstb_crtc_helper_funcs = {
	.atomic_check = kstb_crtc_atomic_check,
	.atomic_enable = kstb_crtc_atomic_enable,
	.atomic_disable = kstb_crtc_atomic_disable,
	.atomic_flush = kstb_crtc_atomic_flush,
};

static const struct drm_crtc_funcs kstb_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = kstb_enable_vblank,
	.disable_vblank = kstb_disable_vblank,
};

static irqreturn_t kstb_vblank_irq(int irq, void *arg)
{
	struct kstb_drm *k = arg;
	struct drm_crtc *crtc = &k->crtc;

	drm_crtc_handle_vblank(crtc);
	spin_lock(&k->drm.event_lock);
	if (k->event && readl(k->gfd) == k->shown) {
		drm_crtc_send_vblank_event(crtc, k->event);
		k->event = NULL;
		drm_crtc_vblank_put(crtc);
	}
	spin_unlock(&k->drm.event_lock);
	return IRQ_HANDLED;
}

/* connector: the one mode; the TV's EDID isn't read yet */

static int kstb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode = drm_mode_duplicate(connector->dev, &kstb_mode);

	if (!mode)
		return 0;
	drm_mode_probed_add(connector, mode);
	return 1;
}

static const struct drm_connector_helper_funcs kstb_connector_helper_funcs = {
	.get_modes = kstb_connector_get_modes,
};

static const struct drm_connector_funcs kstb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_funcs kstb_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_mode_config_funcs kstb_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_DMA_FOPS(kstb_fops);

static const struct drm_driver kstb_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &kstb_fops,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
	.name = "kstb-drm",
	.desc = "KSTB6077 HDMI display",
	.major = 1,
	.minor = 0,
};

static int kstb_modeset_init(struct kstb_drm *k)
{
	struct drm_device *drm = &k->drm;
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;
	drm->mode_config.min_width = WIDTH;
	drm->mode_config.max_width = WIDTH;
	drm->mode_config.min_height = HEIGHT;
	drm->mode_config.max_height = HEIGHT;
	drm->mode_config.preferred_depth = 16;
	drm->mode_config.funcs = &kstb_mode_config_funcs;

	ret = drm_universal_plane_init(drm, &k->plane, 1, &kstb_plane_funcs, kstb_formats,
				       k->gfd_format ? ARRAY_SIZE(kstb_formats) : 1, kstb_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(&k->plane, &kstb_plane_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, &k->crtc, &k->plane, NULL, &kstb_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(&k->crtc, &kstb_crtc_helper_funcs);

	k->encoder.possible_crtcs = 1;
	ret = drm_encoder_init(drm, &k->encoder, &kstb_encoder_funcs, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;

	ret = drm_connector_init(drm, &k->connector, &kstb_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret)
		return ret;
	drm_connector_helper_add(&k->connector, &kstb_connector_helper_funcs);
	ret = drm_connector_attach_encoder(&k->connector, &k->encoder);
	if (ret)
		return ret;

	ret = drm_vblank_init(drm, 1);
	if (ret)
		return ret;
	drm_mode_config_reset(drm);
	return 0;
}

static int kstb_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *fb;
	struct kstb_drm *k;
	int ret;

	k = devm_drm_dev_alloc(dev, &kstb_driver, struct kstb_drm, drm);
	if (IS_ERR(k))
		return PTR_ERR(k);
	platform_set_drvdata(pdev, k);

	k->rdc = devm_platform_ioremap_resource_byname(pdev, "rdc");
	if (IS_ERR(k->rdc))
		return PTR_ERR(k->rdc);
	k->gfd = devm_platform_ioremap_resource_byname(pdev, "gfd");
	if (IS_ERR(k->gfd))
		return PTR_ERR(k->gfd);
	if (platform_get_resource_byname(pdev, IORESOURCE_MEM, "gfd-format")) {
		k->gfd_format = devm_platform_ioremap_resource_byname(pdev, "gfd-format");
		if (IS_ERR(k->gfd_format))
			return PTR_ERR(k->gfd_format);
		k->gfd_pitch = devm_platform_ioremap_resource_byname(pdev, "gfd-pitch");
		if (IS_ERR(k->gfd_pitch))
			return PTR_ERR(k->gfd_pitch);
		/* BOLT's (checked: anything else would be left from an
		 * earlier load of this driver, and kstb_show_boot_fb resets it)
		 */
		k->format = DRM_FORMAT_RGB565;
		k->pitch = PITCH;
		if (readl(k->gfd_format) != 0x00020565 || readl(k->gfd_pitch) != PITCH)
			k->format = 0;
	}
	/* not requested: simplefb holds it until we remove it below */
	fb = platform_get_resource_byname(pdev, IORESOURCE_MEM, "boot-fb");
	if (!fb)
		return -EINVAL;
	k->boot_fb = fb->start;
	k->shown = readl(k->gfd);
	k->irq = platform_get_irq(pdev, 0);
	if (k->irq < 0)
		return k->irq;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = aperture_remove_conflicting_devices(fb->start, resource_size(fb), kstb_driver.name);
	if (ret)
		return ret;

	ret = kstb_modeset_init(k);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, k->irq, kstb_vblank_irq, 0, "kstb-drm", k);
	if (ret)
		return ret;

	ret = drm_dev_register(&k->drm, 0);
	if (ret)
		return ret;
	drm_client_setup_with_fourcc(&k->drm, DRM_FORMAT_RGB565);
	drm_info(&k->drm, "1920x1080@59.94 %s, showing %08x\n",
		 k->gfd_format ? "RGB565/BGR565/ARGB8888/ABGR8888" : "RGB565", k->shown);
	return 0;
}

static void kstb_drm_remove(struct platform_device *pdev)
{
	struct kstb_drm *k = platform_get_drvdata(pdev);

	drm_dev_unplug(&k->drm);
	drm_atomic_helper_shutdown(&k->drm);
}

static void kstb_drm_shutdown(struct platform_device *pdev)
{
	drm_atomic_helper_shutdown(&((struct kstb_drm *)platform_get_drvdata(pdev))->drm);
}

static const struct of_device_id kstb_drm_of_match[] = {
	{ .compatible = "kaon,kstb6077-display" },
	{ }
};
MODULE_DEVICE_TABLE(of, kstb_drm_of_match);

static struct platform_driver kstb_drm_driver = {
	.driver = {
		.name = "kstb-drm",
		.of_match_table = kstb_drm_of_match,
	},
	.probe = kstb_drm_probe,
	.remove = kstb_drm_remove,
	.shutdown = kstb_drm_shutdown,
};
module_platform_driver(kstb_drm_driver);

MODULE_DESCRIPTION("KSTB6077 HDMI display (KMS)");
MODULE_LICENSE("GPL");
