/*
 * kstb-fmt-test.c - kstb-drm's pixel formats, checked without looking at the
 * TV: the same picture in every format, and the HDMI transmitter's CRC of
 * what goes out on the cable (0xf06f8014, Nexus's BHDM_GetCrcValue_isr) after
 * each. A format the display reads right gives RGB565's CRC. 32-bit pixels
 * with alpha below ff are blended over the compositor's background: those
 * pictures must differ from RGB565's (info only otherwise).
 *
 *   kstb-fmt-test [seconds per picture [test]]     default 0.5, all tests
 *
 * The picture: a grid of 240 x 135 cells in the eight colours whose channels
 * are all off or full on, shifted by one cell per row of cells, so a wrong
 * pitch or channel order changes it. Needs /dev/mem (root). Build:
 *   gcc -O2 -o kstb-fmt-test kstb-fmt-test.c -I/usr/include/libdrm -ldrm
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define W 1920
#define H 1080

static int fd;
static volatile uint32_t *hdmi;		/* 0xf06f8000 */
static uint32_t prev_fb, prev_handle;	/* shown until the next picture is */

/* CMA is 64 MB: free each buffer once the next one is on the screen */
static void free_prev(uint32_t fb, uint32_t handle)
{
	struct drm_mode_destroy_dumb d = { .handle = prev_handle };

	if (prev_fb)
		drmModeRmFB(fd, prev_fb);
	if (prev_handle)
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	prev_fb = fb;
	prev_handle = handle;
}

static int open_kstb(void)
{
	for (int i = 0; i < 8; i++) {
		char path[32];
		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		int f = open(path, O_RDWR | O_CLOEXEC);
		if (f < 0)
			continue;
		drmVersionPtr v = drmGetVersion(f);
		int ok = v && !strcmp(v->name, "kstb-drm");
		drmFreeVersion(v);
		if (ok)
			return f;
		close(f);
	}
	return -1;
}

/* colour index -> r, g, b on or off */
static int cell(int x, int y)
{
	return (x / 240 + y / 135) % 8;
}

struct test {
	const char *name;
	uint32_t format;
	int bpp, alloc_w;	/* dumb buffer width: > W gives a longer pitch */
	uint32_t x;		/* the alpha byte of a 32-bit pixel */
	int swap;		/* write red where blue belongs: must NOT match */
	int differ;		/* alpha below ff: must not match either */
};

static uint32_t pixel(const struct test *t, int c)
{
	int r = c & 4, g = c & 2, b = c & 1;
	if (t->swap) {
		int s = r; r = b; b = s;
	}
	switch (t->format) {
	case DRM_FORMAT_RGB565:   return (r ? 0xf800 : 0) | (g ? 0x07e0 : 0) | (b ? 0x001f : 0);
	case DRM_FORMAT_BGR565:   return (b ? 0xf800 : 0) | (g ? 0x07e0 : 0) | (r ? 0x001f : 0);
	case DRM_FORMAT_ARGB8888: return t->x << 24 | (r ? 0xff0000 : 0) | (g ? 0xff00 : 0) | (b ? 0xff : 0);
	case DRM_FORMAT_ABGR8888: return t->x << 24 | (b ? 0xff0000 : 0) | (g ? 0xff00 : 0) | (r ? 0xff : 0);
	}
	return 0;
}

static void sleep_s(double s)
{
	struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
	nanosleep(&ts, NULL);
}

static int run(const struct test *t, uint32_t crtc, uint32_t conn, drmModeModeInfo *mode, double secs)
{
	struct drm_mode_create_dumb c = { .width = t->alloc_w, .height = H, .bpp = t->bpp };
	struct drm_mode_map_dumb m = { 0 };
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 }, fb = 0;

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) { perror("create dumb"); exit(1); }
	m.handle = handles[0] = c.handle;
	pitches[0] = c.pitch;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) { perror("map dumb"); exit(1); }
	uint8_t *p = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			uint32_t v = pixel(t, cell(x, y));
			if (t->bpp == 16)
				((uint16_t *)(p + (size_t)y * c.pitch))[x] = v;
			else
				((uint32_t *)(p + (size_t)y * c.pitch))[x] = v;
		}
	printf("%-22s pitch %5u: ", t->name, c.pitch);
	fflush(stdout);
	int ret = -1;
	if (drmModeAddFB2(fd, W, H, t->format, handles, pitches, offsets, &fb, 0)) {
		perror("addfb2");
		fb = 0;
	} else if (drmModeSetCrtc(fd, crtc, fb, 0, 0, &conn, 1, mode)) {
		perror("setcrtc");
	} else {
		free_prev(fb, c.handle);
		uint32_t crc[3];
		sleep_s(secs);
		for (int i = 0; i < 3; i++) {
			crc[i] = hdmi[0x14 / 4] & 0xffff;
			sleep_s(0.05);
		}
		printf("crc %04x %04x %04x%s\n", crc[0], crc[1], crc[2],
		       crc[0] == crc[1] && crc[1] == crc[2] ? "" : "  (unstable)");
		ret = crc[2];
	}
	munmap(p, c.size);
	if (ret < 0) {
		struct drm_mode_destroy_dumb d = { .handle = c.handle };
		if (fb)
			drmModeRmFB(fd, fb);
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	}
	return ret;
}

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 0.5;
	static const struct test tests[] = {
		{ "RGB565",                DRM_FORMAT_RGB565,   16, W,    0,    0, 0 },
		{ "BGR565",                DRM_FORMAT_BGR565,   16, W,    0,    0, 0 },
		{ "ARGB8888",              DRM_FORMAT_ARGB8888, 32, W,    0xff, 0, 0 },
		{ "ABGR8888",              DRM_FORMAT_ABGR8888, 32, W,    0xff, 0, 0 },
		{ "ARGB8888 pitch 8192",   DRM_FORMAT_ARGB8888, 32, 2048, 0xff, 0, 0 },
		{ "RGB565 pitch 4096",     DRM_FORMAT_RGB565,   16, 2048, 0,    0, 0 },
		{ "ARGB8888 alpha 00",     DRM_FORMAT_ARGB8888, 32, W,    0,    0, 1 },
		{ "ARGB8888 alpha 5a",     DRM_FORMAT_ARGB8888, 32, W,    0x5a, 0, 1 },
		{ "RGB565 again",          DRM_FORMAT_RGB565,   16, W,    0,    0, 0 },
		{ "ARGB8888 R/B swapped",  DRM_FORMAT_ARGB8888, 32, W,    0xff, 1, 0 },
		{ "RGB565 R/B swapped",    DRM_FORMAT_RGB565,   16, W,    0,    1, 0 },
	};
	int n = sizeof tests / sizeof tests[0], bad = 0, ref = -1, refswap = -1;

	int mem = open("/dev/mem", O_RDWR | O_SYNC);
	if (mem < 0) { perror("/dev/mem"); return 1; }
	hdmi = mmap(0, 4096, PROT_READ, MAP_SHARED, mem, 0xf06f8000);
	if (hdmi == MAP_FAILED) { perror("mmap /dev/mem"); return 1; }
	fd = open_kstb();
	if (fd < 0) { fprintf(stderr, "no kstb-drm device\n"); return 1; }
	drmModeResPtr res = drmModeGetResources(fd);
	drmModeConnectorPtr con = drmModeGetConnector(fd, res->connectors[0]);
	uint32_t crtc = res->crtcs[0], conn = con->connector_id;
	drmModeModeInfo mode = con->modes[0];

	if (argc > 2) {		/* one picture only, e.g. to poke registers meanwhile */
		int i = atoi(argv[2]);
		return i < 0 || i >= n || run(&tests[i], crtc, conn, &mode, secs) < 0;
	}
	for (int i = 0; i < n; i++) {
		int crc = run(&tests[i], crtc, conn, &mode, secs);
		if (i == 0)
			ref = crc;
		if (tests[i].swap && refswap < 0)
			refswap = crc;
		/* the R/B-swapped pictures must differ from the reference and
		 * agree with each other, those with alpha below ff differ;
		 * all others must equal the reference
		 */
		if (crc < 0 || (tests[i].swap ? crc == ref || crc != refswap :
				tests[i].differ ? crc == ref : crc != ref))
			bad++, printf("  ^ MISMATCH\n");
	}
	printf("%s: %d of %d as expected\n", bad ? "FAIL" : "OK", n - bad, n);
	return bad != 0;
}
