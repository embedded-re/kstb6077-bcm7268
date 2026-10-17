/*
 * kstb-flip-test.c - page flips on kstb-drm: a CPU-drawn white bar moves
 * across the screen, 16 pixels per frame, double-buffered, one page flip per
 * vblank. A torn frame would show the bar broken at the tear line.
 *
 *   kstb-flip-test [seconds]     default 10
 *
 * Prints the flip rate and the frames that took more than one vblank (from
 * the flip events' vblank sequence numbers). Build (Alpine chroot):
 *   gcc -O2 -o kstb-flip-test kstb-flip-test.c -I/usr/include/libdrm -ldrm
 */
#include <errno.h>
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
#define BAR 64
#define STEP 16
#define BG 0x0010		/* dark blue */
#define FG 0xffff

struct buf { uint32_t handle, pitch, fb; uint16_t *px; int bar; };

static int fd;
static unsigned last_seq, flips, missed;
static int pending;

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

static void make_buf(struct buf *b)
{
	struct drm_mode_create_dumb c = { .width = W, .height = H, .bpp = 16 };
	struct drm_mode_map_dumb m = { 0 };
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) { perror("create dumb"); exit(1); }
	b->handle = c.handle;
	b->pitch = c.pitch;
	handles[0] = c.handle;
	pitches[0] = c.pitch;
	if (drmModeAddFB2(fd, W, H, DRM_FORMAT_RGB565, handles, pitches, offsets, &b->fb, 0)) {
		perror("addfb2");
		exit(1);
	}
	m.handle = c.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) { perror("map dumb"); exit(1); }
	b->px = mmap(0, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
	if (b->px == MAP_FAILED) { perror("mmap"); exit(1); }
	for (size_t i = 0; i < (size_t)H * b->pitch / 2; i++)
		b->px[i] = BG;
	b->bar = -1;
}

static void bar(struct buf *b, int x, uint16_t c)
{
	for (int y = 0; y < H; y++) {
		uint16_t *row = b->px + (size_t)y * b->pitch / 2 + x;
		for (int i = 0; i < BAR && x + i < W; i++)
			row[i] = c;
	}
}

static void flip_done(int f, unsigned seq, unsigned s, unsigned us, void *data)
{
	if (flips && seq - last_seq > 1)
		missed += seq - last_seq - 1;
	last_seq = seq;
	flips++;
	pending = 0;
}

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 10;
	drmEventContext ev = { .version = 2, .page_flip_handler = flip_done };
	struct buf b[2];

	fd = open_kstb();
	if (fd < 0) { fprintf(stderr, "no kstb-drm device\n"); return 1; }
	drmModeResPtr res = drmModeGetResources(fd);
	drmModeConnectorPtr con = drmModeGetConnector(fd, res->connectors[0]);
	uint32_t crtc = res->crtcs[0], conn = con->connector_id;
	drmModeModeInfo mode = con->modes[0];

	make_buf(&b[0]);
	make_buf(&b[1]);
	printf("mode %s %u Hz, pitch %u\n", mode.name, mode.vrefresh, b[0].pitch);
	if (drmModeSetCrtc(fd, crtc, b[0].fb, 0, 0, &conn, 1, &mode)) { perror("setcrtc"); return 1; }

	int front = 0, x = 0;
	double t0 = now();
	while (now() - t0 < secs) {
		struct buf *bk = &b[!front];
		if (bk->bar >= 0)
			bar(bk, bk->bar, BG);
		bar(bk, x, FG);
		bk->bar = x;
		x = (x + STEP) % W;
		if (drmModePageFlip(fd, crtc, bk->fb, DRM_MODE_PAGE_FLIP_EVENT, NULL)) {
			perror("page flip");
			break;
		}
		pending = 1;
		while (pending)
			drmHandleEvent(fd, &ev);
		front = !front;
	}
	double t = now() - t0;
	printf("%u flips in %.2f s: %.2f per second, %u frames missed\n", flips, t, flips / t, missed);
	return 0;	/* at close the console takes the screen back */
}
