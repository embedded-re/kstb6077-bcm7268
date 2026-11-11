// kstb-gles-test.c - OpenGL ES 2 on the V3D 3.3 through Mesa's v3d driver and
// the kernel's v3d: EGL on GBM over /dev/dri/renderD128, no window (FBO).
// Draws a triangle with red, green, blue corners on black, reads it back and
// checks it, times frames at 1920x1080, and copies the triangle to the HDMI
// framebuffer (/dev/fb0, RGB565) at the screen's centre.
//
// Needs Mesa <= 23.3 (24.0 dropped V3D 3.3): built and run in the Alpine 3.19
// chroot (port/mesa/build-mesa.sh), see port/README.md "GPU".
//   gcc -O2 -o kstb-gles-test kstb-gles-test.c -lEGL -lGLESv2 -lgbm
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <gbm.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W 256
#define H 256

static const char *vs_src =
	"attribute vec2 pos; attribute vec3 col; varying vec3 v;\n"
	"void main() { v = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src =
	"precision mediump float; varying vec3 v;\n"
	"void main() { gl_FragColor = vec4(v, 1.0); }\n";
// a constant colour, no varyings: KSTB_FLAT=1
static const char *fs_flat_src =
	"precision mediump float; varying vec3 v;\n"
	"void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }\n";
// the same with a texture lookup (a 1x1 white texture): KSTB_TEX=1
static const char *fs_tex_src =
	"precision mediump float; varying vec3 v; uniform sampler2D t;\n"
	"void main() { gl_FragColor = vec4(v, 1.0) * texture2D(t, vec2(0.5)); }\n";

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

static GLuint target(int w, int h)
{
	GLuint fb, rb;

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, w, h);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "framebuffer incomplete\n");
		exit(1);
	}
	glViewport(0, 0, w, h);
	return fb;
}

static void draw(void)
{
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void)
{
	static GLfloat pos[] = { -0.75f, -0.75f, 0.75f, -0.75f, 0.0f, 0.75f };
	static const GLfloat col[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	static uint8_t px[W * H * 4];
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm;
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig cfg;
	EGLint n, major, minor;
	GLuint prog;
	int lit = 0, bad = 0, i, x, y, ok;

	// KSTB_VERTS="x0 y0 x1 y1 x2 y2": other corners (clip space), for tests
	if (getenv("KSTB_VERTS"))
		sscanf(getenv("KSTB_VERTS"), "%f %f %f %f %f %f",
		       &pos[0], &pos[1], &pos[2], &pos[3], &pos[4], &pos[5]);
	if (fd < 0 || !(gbm = gbm_create_device(fd))) {
		perror("renderD128 / gbm");
		return 1;
	}
	dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (!eglInitialize(dpy, &major, &minor)) {
		fprintf(stderr, "eglInitialize failed\n");
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	eglChooseConfig(dpy, cattr, &cfg, 1, &n);
	ctx = eglCreateContext(dpy, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, xattr);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "no context (%x)\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d, %s\nGL_RENDERER %s\nGL_VERSION %s\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
	if (getenv("KSTB_TEX")) {
		static const uint8_t white[4] = { 255, 255, 255, 255 };
		GLuint tex;

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_tex_src));
		printf("fragment shader with a texture lookup\n");
	} else if (getenv("KSTB_FLAT")) {
		glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_flat_src));
		printf("fragment shader with a constant colour\n");
	} else {
		glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_src));
	}
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glLinkProgram(prog);
	glUseProgram(prog);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, col);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);

	// 256x256: corners at pixels (32,32) (224,32) (128,224), area 18432
	target(W, H);
	draw();
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (i = 0; i < W * H; i++) {
		uint8_t *p = px + 4 * i;

		if (p[0] | p[1] | p[2])
			lit++;
		if (p[3] != 255)
			bad++;
	}
	{
		int x0 = W, y0 = H, x1 = -1, y1 = -1;

		for (y = 0; y < H; y++)
			for (x = 0; x < W; x++)
				if (px[4 * (y * W + x)] | px[4 * (y * W + x) + 1] | px[4 * (y * W + x) + 2]) {
					x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
					y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
				}
		printf("lit box x %d-%d, y %d-%d\n", x0, x1, y0, y1);
	}
	printf("lit pixels %d (area 18432), alpha != 255: %d\n", lit, bad);
	for (i = 0; i < 4; i++) {
		static const int at[4][2] = { { 128, 96 }, { 40, 36 }, { 216, 36 }, { 128, 216 } };
		uint8_t *p = px + 4 * (at[i][1] * W + at[i][0]);

		printf("pixel (%d,%d): %3d %3d %3d\n", at[i][0], at[i][1], p[0], p[1], p[2]);
	}
	{
		FILE *f = fopen("kstb-gles-test.ppm", "wb");

		if (f) {
			fprintf(f, "P6 %d %d 255\n", W, H);
			for (y = H - 1; y >= 0; y--)
				for (x = 0; x < W; x++)
					fwrite(px + 4 * (y * W + x), 1, 3, f);
			fclose(f);
		}
	}
	ok = abs(lit - 18432) < 400 && !bad;
	{
		uint8_t *c = px + 4 * (96 * W + 128), *r = px + 4 * (36 * W + 40);

		ok = ok && abs(c[0] - c[1]) < 40 && abs(c[1] - c[2]) < 80 && r[0] > 200 && r[1] < 40;
	}

	// KSTB_REPEAT=n: the same 256x256 draw n more times, lit pixels of each
	if (getenv("KSTB_REPEAT")) {
		int n = atoi(getenv("KSTB_REPEAT"));

		printf("repeat:");
		for (i = 0; i < n; i++) {
			int l = 0, k;

			draw();
			glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
			for (k = 0; k < W * H; k++)
				l += !!(px[4 * k] | px[4 * k + 1] | px[4 * k + 2]);
			printf(" %d", l);
		}
		printf("\n");
		return 0;
	}

	// frames at 1920x1080
	target(1920, 1080);
	draw();
	glFinish();
	double t = now();
	for (i = 0; i < 100; i++) {
		draw();
		glFinish();
	}
	t = now() - t;
	printf("1920x1080: 100 frames in %.3f s, %.1f frames/s\n", t, 100 / t);

	// the 256x256 picture onto the TV (row 0 of GL is the bottom), centred.
	// simplefb's buffer starts inside a page (0x7db0b700): the mapping begins
	// at the page, so pixel (0,0) is at smem_start's offset in it
	int fb = open("/dev/fb0", O_RDWR);
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	if (fb >= 0 && !ioctl(fb, FBIOGET_FSCREENINFO, &fix) &&
	    !ioctl(fb, FBIOGET_VSCREENINFO, &var) && var.bits_per_pixel == 16) {
		long off = fix.smem_start & (sysconf(_SC_PAGESIZE) - 1);
		uint8_t *m = mmap(NULL, off + fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
		int x0 = (var.xres - W) / 2, y0 = (var.yres - H) / 2;

		if (m != MAP_FAILED) {
			for (y = 0; y < H; y++)
				for (x = 0; x < W; x++) {
					uint8_t *p = px + 4 * ((H - 1 - y) * W + x);
					uint16_t *d = (uint16_t *)(m + off + (y0 + y) * fix.line_length) + x0 + x;

					*d = (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3;
				}
			printf("copied to /dev/fb0 at (%d,%d)\n", x0, y0);
		}
	}
	printf("%s\n", ok ? "OK" : "FAIL");
	return !ok;
}
