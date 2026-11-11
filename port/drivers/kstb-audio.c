// SPDX-License-Identifier: GPL-2.0
/*
 * kstb-audio.c - HDMI sound on the BCM7268 as an ALSA card
 *
 * The audio block (FMM, hardware/audio.md) is started the way BOLT's
 * splash audio routine 0x070109cc does it, steps 1-10, with the register
 * writes of extras/mon/audio_hw.c; then the HDMI N/CTS for 1080p59.94, the only
 * mode kmon sets up. Ring buffer 0 plays a buffer in DRAM in a loop
 * (channel config bit 10) and ignores the write pointer, so the ALSA buffer
 * is the ring itself: the read pointer (bit 31 = wrap flag) is the
 * position. No audio interrupt is known, so an hrtimer ticks once per
 * period. When nothing plays the ring loops over a small buffer of zeros.
 *
 * Format: 48 kHz, 2 channels, 32 bits per sample (the sample in the top
 * bits), 8 bytes per frame.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

/* ring buffer 0 and DMA on/off, in the FMM block at 0xf0ca0000 */
#define RB0_RD		0xf0ca0800
#define RB0_WR		0xf0ca0804
#define RB0_START	0xf0ca0808
#define RB0_END		0xf0ca080c
#define DMA_CTRL	0xf0ca00c0
#define RD_WRAP		BIT(31)

/* HDMI audio clock regeneration (N keeps BOLT's upper bits, 0x04000000) */
#define HDMI_N		0xf06fa0c8
#define HDMI_CTS0	0xf06fa0cc
#define HDMI_CTS1	0xf06fa0d0
#define N_1080P5994	5824
#define CTS_1080P5994	140625

#define BUF_MAX		(256 * 1024)
#define SILENCE_BYTES	4096

/* the register windows of the "reg" property, by absolute address */
#define NWIN 6

struct kstb_audio {
	struct device *dev;
	void __iomem *base[NWIN];
	u32 start[NWIN], size[NWIN];
	void *silence;
	dma_addr_t silence_dma;
	struct snd_card *card;
	struct snd_pcm_substream *substream;
	struct hrtimer timer;
	ktime_t period;
};

static void __iomem *kreg(struct kstb_audio *ka, u32 a)
{
	int i;

	for (i = 0; i < NWIN; i++)
		if (a - ka->start[i] < ka->size[i])
			return ka->base[i] + (a - ka->start[i]);
	WARN(1, "kstb-audio: %08x not in reg\n", a);
	return NULL;
}

static u32 rd(struct kstb_audio *ka, u32 a)
{
	return readl(kreg(ka, a));
}

static void wr(struct kstb_audio *ka, u32 a, u32 v)
{
	writel(v, kreg(ka, a));
}

static void field(struct kstb_audio *ka, u32 a, u32 keep, u32 val)
{
	wr(ka, a, (rd(ka, a) & keep) | val);
}

static void set(struct kstb_audio *ka, u32 a, u32 bits)
{
	field(ka, a, ~bits, bits);
}

static void clr(struct kstb_audio *ka, u32 a, u32 bits)
{
	field(ka, a, ~bits, 0);
}

static void fill(struct kstb_audio *ka, u32 a, u32 n, u32 v0, u32 step)
{
	u32 i;

	for (i = 0; i < n; i++)
		wr(ka, a + 4 * i, v0 + i * step);
}

/* BOLT's audio start, steps 1-10 (extras/mon/audio_hw.c); ring 0 = buf..buf+size-1 */
static void kstb_audio_hw_start(struct kstb_audio *ka, u32 buf, u32 size)
{
	u32 i, a, v;

	/* 1-2: audio block out of reset, its set-up */
	clr(ka, 0xf11005c0, BIT(31));
	wr(ka, 0xf0cb0804, 0);
	wr(ka, 0xf0cb0808, 0x29fc8);
	wr(ka, 0xf0cb0838, 0x2000698);
	wr(ka, 0xf0cb083c, 0);
	wr(ka, 0xf0cb08fc, 0);
	field(ka, 0xf0cb080c, 0xfffff0ff, 0x200);
	set(ka, 0xf0cb0820, 1);
	wr(ka, 0xf0cb0824, 0xce10);
	wr(ka, 0xf0cb0828, 0x1c1a1);
	wr(ka, 0xf0cb082c, 0x330bb);
	wr(ka, 0xf0cb0814, 0x1ffff);
	wr(ka, 0xf0cb0a04, 0x200);
	wr(ka, 0xf0cb0a08, 0x6502);
	wr(ka, 0xf0cb0a0c, 0xe9045);
	wr(ka, 0xf0cb0a00, 0x240);
	set(ka, 0xf0cb0c94, 2);
	wr(ka, 0xf0cb0c40, 0);
	wr(ka, 0xf0cb0c60, 0);
	wr(ka, 0xf0cb0c54, 0x40ffffff);
	wr(ka, 0xf0cb0c74, 0x40ffffff);
	v = rd(ka, 0xf0cb0c94);			/* bit 0: low, then high */
	wr(ka, 0xf0cb0c94, v & ~1u);
	wr(ka, 0xf0cb0c94, v | 1);
	wr(ka, 0xf0cb0c50, 0xf4);
	wr(ka, 0xf0cb0c70, 0xf4);
	v = rd(ka, 0xf0cb0c34);			/* bit 8: pulse */
	wr(ka, 0xf0cb0c34, v | 0x100);
	wr(ka, 0xf0cb0c34, v & ~0x100u);
	/* 3 */
	usleep_range(1000, 2000);
	set(ka, 0xf0cb0c38, 2);
	/* 4: audio clock */
	wr(ka, 0xf04e2800, 0x18a0);
	field(ka, 0xf04e2804, 0xff000000, 0x20);
	field(ka, 0xf04e2808, 0x000000ff, 0x1d00);
	field(ka, 0xf04e2808, 0xffffff00, 0x3);
	wr(ka, 0xf04e280c, 0x83126);
	wr(ka, 0xf04e2810, 0);
	wr(ka, 0xf04e2814, 0);
	wr(ka, 0xf04e2800, 0x1);
	wr(ka, 0xf04e281c, 0x2000064);
	wr(ka, 0xf04e2818, 0x10000000);
	wr(ka, 0xf04e2800, 0x8a0);
	clr(ka, 0xf04e0314, 1);
	set(ka, 0xf0cb0240, 3);
	/* 5: routing/mixer tables */
	for (i = 0; i < 192; i++) {		/* 2x2 identity, 1.0 = 0x800000 */
		wr(ka, 0xf0ca4800 + 16 * i, 0x800000);
		wr(ka, 0xf0ca4804 + 16 * i, 0);
		wr(ka, 0xf0ca4808 + 16 * i, 0);
		wr(ka, 0xf0ca480c + 16 * i, 0x800000);
	}
	fill(ka, 0xf0ca5400, 768, 0x100000, 0);
	fill(ka, 0xf0ca6000, 48, 0x800000, 0);
	fill(ka, 0xf0ca41b0, 12, 0x80, 1);
	fill(ka, 0xf0ca41e0, 12, 0xc0, 1);
	fill(ka, 0xf0ca4210, 12, 0x101, 2);
	fill(ka, 0xf0ca4000, 12, 0, 0x100000);
	fill(ka, 0xf0ca2020, 12, 0, 0x10001);
	fill(ka, 0xf0ca2800, 12, 0, 0);
	/* 6: where DRAM is (from BOLT's memory-controller info, this box) */
	field(ka, 0xf0ca01b0, 0xf000000f, 0xf0f700);
	/* 7: ring buffers; only 0 is used, BOLT points all 42 into the buffer */
	fill(ka, 0xf0ca0800, 336, 0, 0);
	for (i = 0; i < 42; i++) {
		u32 r = 0xf0ca0800 + 24 * i;

		a = buf + 0x800 * i;
		wr(ka, r + 0x0, a);		/* read */
		wr(ka, r + 0x4, a);		/* write */
		wr(ka, r + 0x8, a);		/* start */
		wr(ka, r + 0xc, a + size - 1);	/* end */
	}
	/* 8: channel config */
	fill(ka, 0xf0ca0048, 15, 0x420, 0);
	fill(ka, 0xf0ca00fc, 15, 0, 1);
	/* 9: data path */
	field(ka, 0xf0cb0200, 0x6f0ffc00, 0x90000100);
	for (a = 0xf0ca4030; a <= 0xf0ca40b0; a += 0x20)
		field(ka, a, 0xffffefc0, 0x1000);
	field(ka, 0xf0cb0300, 0x7f0ffc00, 0x80f00102);
	wr(ka, 0xf0cb0324, 0x2000000);
	field(ka, 0xf0cb0320, 0xfffbfff7, 0x40008);
	field(ka, 0xf0cb0220, 0xfffbfff7, 0x40008);
	field(ka, 0xf0cb0c00, 0x7f0ffc00, 0x80400108);
	for (a = 0xf0ca4000; a <= 0xf0ca4010; a += 4)
		set(ka, a, 1);
	field(ka, 0xf0cb0c00, 0xff0ffc00, 0x400108);
	field(ka, 0xf0ca40b0, 0xffffefc0, 0x1000);
	wr(ka, 0xf0ca0048, 0x80000420);		/* channel 0: loop over the ring */
	set(ka, 0xf0ca4004, 1);
	set(ka, 0xf0ca2020, 0x100);
	set(ka, 0xf0cb0c00, BIT(31));
	set(ka, 0xf0ca4010, 1);
	set(ka, 0xf0ca2020, 0x100);
	set(ka, 0xf0ca0048, 1);			/* channel 0 on */
	/* 10: start playback, HDMI side on */
	set(ka, DMA_CTRL, 1);
	set(ka, 0xf06fa0e0, 1);
}

/* point ring 0 at another buffer: stop, new bounds, read pointer = start, go */
static void kstb_audio_ring(struct kstb_audio *ka, u32 buf, u32 size)
{
	clr(ka, DMA_CTRL, 1);
	wr(ka, RB0_END, buf + size - 1);
	wr(ka, RB0_START, buf);
	wr(ka, RB0_WR, buf);
	wr(ka, RB0_RD, buf);
	set(ka, DMA_CTRL, 1);
}

static enum hrtimer_restart kstb_audio_tick(struct hrtimer *t)
{
	struct kstb_audio *ka = container_of(t, struct kstb_audio, timer);

	snd_pcm_period_elapsed(ka->substream);
	hrtimer_forward_now(t, ka->period);
	return HRTIMER_RESTART;
}

static const struct snd_pcm_hardware kstb_audio_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S32_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = BUF_MAX,
	.period_bytes_min = 1024,
	.period_bytes_max = BUF_MAX / 2,
	.periods_min = 2,
	.periods_max = 256,
};

static int kstb_audio_open(struct snd_pcm_substream *ss)
{
	struct kstb_audio *ka = snd_pcm_substream_chip(ss);
	int ret;

	ss->runtime->hw = kstb_audio_hw;
	/* the ring's lengths that were tried are all multiples of 256 bytes */
	ret = snd_pcm_hw_constraint_step(ss->runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 256);
	if (ret < 0)
		return ret;
	ret = snd_pcm_hw_constraint_step(ss->runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 256);
	if (ret < 0)
		return ret;
	ret = snd_pcm_hw_constraint_integer(ss->runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;
	ka->substream = ss;
	return 0;
}

static int kstb_audio_close(struct snd_pcm_substream *ss)
{
	struct kstb_audio *ka = snd_pcm_substream_chip(ss);

	hrtimer_cancel(&ka->timer);
	ka->substream = NULL;
	return 0;
}

static int kstb_audio_prepare(struct snd_pcm_substream *ss)
{
	struct kstb_audio *ka = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;

	hrtimer_cancel(&ka->timer);
	ka->period = ns_to_ktime(div_u64((u64)rt->period_size * NSEC_PER_SEC, rt->rate));
	return 0;
}

static int kstb_audio_trigger(struct snd_pcm_substream *ss, int cmd)
{
	struct kstb_audio *ka = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		kstb_audio_ring(ka, rt->dma_addr, frames_to_bytes(rt, rt->buffer_size));
		hrtimer_start(&ka->timer, ka->period, HRTIMER_MODE_REL_SOFT);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
		kstb_audio_ring(ka, ka->silence_dma, SILENCE_BYTES);
		hrtimer_try_to_cancel(&ka->timer);
		return 0;
	}
	return -EINVAL;
}

static int kstb_audio_sync_stop(struct snd_pcm_substream *ss)
{
	struct kstb_audio *ka = snd_pcm_substream_chip(ss);

	hrtimer_cancel(&ka->timer);
	return 0;
}

static snd_pcm_uframes_t kstb_audio_pointer(struct snd_pcm_substream *ss)
{
	struct kstb_audio *ka = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	u32 pos = (rd(ka, RB0_RD) & ~RD_WRAP) - rt->dma_addr;

	if (pos >= frames_to_bytes(rt, rt->buffer_size))
		return 0;
	return bytes_to_frames(rt, pos);
}

static const struct snd_pcm_ops kstb_audio_ops = {
	.open = kstb_audio_open,
	.close = kstb_audio_close,
	.prepare = kstb_audio_prepare,
	.trigger = kstb_audio_trigger,
	.sync_stop = kstb_audio_sync_stop,
	.pointer = kstb_audio_pointer,
};

static int kstb_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct kstb_audio *ka;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int i, ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(31));	/* bit 31 = wrap flag */
	if (ret)
		return ret;
	ret = snd_devm_card_new(dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1, THIS_MODULE,
				sizeof(*ka), &card);
	if (ret)
		return ret;
	ka = card->private_data;
	ka->dev = dev;
	ka->card = card;

	/* not requested: the clock generator and memory controller have other users */
	for (i = 0; i < NWIN; i++) {
		struct resource *r = platform_get_resource(pdev, IORESOURCE_MEM, i);

		if (!r)
			return dev_err_probe(dev, -EINVAL, "reg %d missing\n", i);
		ka->base[i] = devm_ioremap(dev, r->start, resource_size(r));
		if (!ka->base[i])
			return -ENOMEM;
		ka->start[i] = r->start;
		ka->size[i] = resource_size(r);
	}

	ka->silence = dmam_alloc_coherent(dev, SILENCE_BYTES, &ka->silence_dma, GFP_KERNEL);
	if (!ka->silence)
		return -ENOMEM;
	hrtimer_setup(&ka->timer, kstb_audio_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);

	/* whatever kmon left playing stops; then our own start on the zeros */
	clr(ka, DMA_CTRL, 1);
	kstb_audio_hw_start(ka, ka->silence_dma, SILENCE_BYTES);
	field(ka, HDMI_N, 0xf7f00000, N_1080P5994);
	wr(ka, HDMI_CTS0, CTS_1080P5994);
	wr(ka, HDMI_CTS1, CTS_1080P5994);

	strscpy(card->driver, "kstb-audio", sizeof(card->driver));
	strscpy(card->shortname, "KSTB6077 HDMI", sizeof(card->shortname));
	snprintf(card->longname, sizeof(card->longname),
		 "KSTB6077 HDMI audio (FMM ring 0 at %08x)", RB0_RD);
	ret = snd_pcm_new(card, "HDMI", 0, 1, 0, &pcm);
	if (ret)
		return ret;
	pcm->private_data = ka;
	strscpy(pcm->name, "KSTB6077 HDMI", sizeof(pcm->name));
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &kstb_audio_ops);
	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, dev, BUF_MAX, BUF_MAX);

	ret = snd_card_register(card);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, ka);
	dev_info(dev, "48 kHz S32_LE stereo, silence at %pad\n", &ka->silence_dma);
	return 0;
}

/* the buffers go away with the device: the DMA must not read them afterwards */
static void kstb_audio_remove(struct platform_device *pdev)
{
	struct kstb_audio *ka = platform_get_drvdata(pdev);

	clr(ka, DMA_CTRL, 1);
}

static const struct of_device_id kstb_audio_of_match[] = {
	{ .compatible = "kaon,kstb6077-hdmi-audio" },
	{ }
};
MODULE_DEVICE_TABLE(of, kstb_audio_of_match);

static struct platform_driver kstb_audio_driver = {
	.probe = kstb_audio_probe,
	.remove = kstb_audio_remove,
	.driver = {
		.name = "kstb-audio",
		.of_match_table = kstb_audio_of_match,
	},
};
module_platform_driver(kstb_audio_driver);

MODULE_DESCRIPTION("KSTB6077 (BCM7268) HDMI audio");
MODULE_LICENSE("GPL");
