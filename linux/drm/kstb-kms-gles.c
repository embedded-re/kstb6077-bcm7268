// kstb-kms-gles.c - the GPU straight to the screen: Mesa's v3d renders into
// scanout buffers that kstb-drm allocates (Mesa's kmsro, GBM on the KMS
// device), kstb-drm flips them at vsync. No CPU copy.
//
// A triangle with red, green, blue corners spins once every 4 s on dark blue,
// at 1920x1080 RGB565, one page flip per frame. Prints the flip rate, the
// frames that took more than one vblank and which flips came late (the last
// one always does: its glReadPixels waits for the GPU), and the centre pixel
// of the last frame twice: read by the GPU (glReadPixels) and read from the
// KMS buffer the display scans out (mapped through kstb-drm).
//
//   kstb-kms-gles [seconds]      default 10
//
// Needs Mesa 23.3.6 with linux/mesa/kmsro-kstb-drm.patch, in the Alpine 3.19
// chroot:
//   gcc -O2 -o kstb-kms-gles kstb-kms-gles.c -I/usr/include/libdrm -lEGL -lGLESv2 -lgbm -ldrm
//   LD_LIBRARY_PATH=/opt/mesa/lib LIBGL_DRIVERS_PATH=/opt/mesa/lib/dri ./kstb-kms-gles
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define W 1920
#define H 1080

static const char *vs_src =
	"attribute vec2 pos; attribute vec3 col; varying vec3 v;\n"
	"void main() { v = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src =
	"precision mediump float; varying vec3 v;\n"
	"void main() { gl_FragColor = vec4(v, 1.0); }\n";

static int fd;
static unsigned last_seq, flips, missed, miss_at[8], nmiss;
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

static void die(const char *what)
{
	fprintf(stderr, "%s failed (EGL %#x, GL %#x)\n", what, eglGetError(), glGetError());
	exit(1);
}

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok;
	char log[1024];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "shader: %s\n", log);
		exit(1);
	}
	return s;
}

// one KMS framebuffer per GBM buffer, made the first time it comes round
static void fb_destroy(struct gbm_bo *bo, void *data)
{
	uint32_t id = (uintptr_t)data;
	drmModeRmFB(fd, id);
}

static uint32_t fb_for(struct gbm_bo *bo)
{
	uint32_t id = (uintptr_t)gbm_bo_get_user_data(bo);
	uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 }, pitches[4] = { gbm_bo_get_stride(bo) };
	uint32_t offsets[4] = { 0 };

	if (id)
		return id;
	if (drmModeAddFB2(fd, W, H, gbm_bo_get_format(bo), handles, pitches, offsets, &id, 0)) {
		perror("addfb2");
		exit(1);
	}
	printf("buffer %u: handle %u, stride %u, format %.4s\n", id, handles[0], pitches[0],
	       (char *)&(uint32_t){ gbm_bo_get_format(bo) });
	gbm_bo_set_user_data(bo, (void *)(uintptr_t)id, fb_destroy);
	return id;
}

// the pixel at (x, y) of the buffer the display shows, through kstb-drm's own mapping
static uint16_t scanout_pixel(struct gbm_bo *bo, int x, int y)
{
	struct drm_mode_map_dumb m = { .handle = gbm_bo_get_handle(bo).u32 };
	uint32_t stride = gbm_bo_get_stride(bo);
	size_t size = (size_t)stride * H;
	uint16_t v;

	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m))
		return 0;
	uint8_t *p = mmap(0, size, PROT_READ, MAP_SHARED, fd, m.offset);
	if (p == MAP_FAILED)
		return 0;
	v = *(uint16_t *)(p + (size_t)y * stride + x * 2);
	munmap(p, size);
	return v;
}

static void flip_done(int f, unsigned seq, unsigned s, unsigned us, void *data)
{
	if (flips && seq - last_seq > 1) {
		missed += seq - last_seq - 1;
		if (nmiss < 8)
			miss_at[nmiss++] = flips;
	}
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

	fd = open_kstb();
	if (fd < 0) { fprintf(stderr, "no kstb-drm device\n"); return 1; }
	drmModeResPtr res = drmModeGetResources(fd);
	drmModeConnectorPtr con = drmModeGetConnector(fd, res->connectors[0]);
	uint32_t crtc = res->crtcs[0], conn = con->connector_id;
	drmModeModeInfo mode = con->modes[0];

	struct gbm_device *gbm = gbm_create_device(fd);
	if (!gbm) die("gbm_create_device");
	struct gbm_surface *gs = gbm_surface_create(gbm, W, H, GBM_FORMAT_RGB565,
						    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!gs) die("gbm_surface_create");

	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay dpy = get_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (!eglInitialize(dpy, NULL, NULL)) die("eglInitialize");
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			  EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_NONE };
	EGLConfig cfgs[64], cfg = NULL;
	EGLint n = 0;
	eglChooseConfig(dpy, attr, cfgs, 64, &n);
	for (int i = 0; i < n && !cfg; i++) {
		EGLint id;
		eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &id);
		if (id == GBM_FORMAT_RGB565)
			cfg = cfgs[i];
	}
	if (!cfg) die("an RGB565 EGL config");
	EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gs, NULL);
	if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE) die("context/surface");
	eglMakeCurrent(dpy, surf, surf, ctx);
	printf("%s, %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	GLuint prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glLinkProgram(prog);
	glUseProgram(prog);
	static const float col[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	glViewport(0, 0, W, H);

	struct gbm_bo *shown = NULL;
	int frame = 0;
	double t0 = 0;
	for (;;) {
		float pos[6], a = frame * 2 * M_PI / (4 * 59.94);
		for (int i = 0; i < 3; i++) {
			float b = a + i * 2 * M_PI / 3;
			pos[2 * i] = 0.8f * sinf(b) * H / W;
			pos[2 * i + 1] = 0.8f * cosf(b);
		}
		glClearColor(0, 0, 0.25f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, col);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		int last = frame && now() - t0 >= secs;
		uint8_t gpu[4] = { 0 };
		if (last)
			glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gpu);
		eglSwapBuffers(dpy, surf);

		struct gbm_bo *bo = gbm_surface_lock_front_buffer(gs);
		if (!bo) die("lock front buffer");
		uint32_t id = fb_for(bo);
		if (!frame) {
			if (drmModeSetCrtc(fd, crtc, id, 0, 0, &conn, 1, &mode)) { perror("setcrtc"); return 1; }
			t0 = now();
		} else {
			if (drmModePageFlip(fd, crtc, id, DRM_MODE_PAGE_FLIP_EVENT, NULL)) {
				perror("page flip");
				return 1;
			}
			pending = 1;
			while (pending)
				drmHandleEvent(fd, &ev);
		}
		if (shown)
			gbm_surface_release_buffer(gs, shown);
		shown = bo;
		frame++;
		if (last) {
			double t = now() - t0;
			uint16_t p = scanout_pixel(bo, W / 2, H - 1 - H / 2);
			printf("%u flips in %.2f s: %.2f per second, %u frames missed\n",
			       flips, t, flips / t, missed);
			if (nmiss) {
				printf("late flips:");
				for (unsigned i = 0; i < nmiss; i++)
					printf(" %u", miss_at[i]);
				printf("\n");
			}
			printf("centre pixel: GPU %u,%u,%u; scanout buffer %04x = %u,%u,%u\n",
			       gpu[0], gpu[1], gpu[2], p, (p >> 11) << 3, ((p >> 5) & 63) << 2,
			       (p & 31) << 3);
			break;
		}
	}
	return 0;	/* at close the console takes the screen back */
}
