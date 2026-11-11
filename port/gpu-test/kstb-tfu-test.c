// kstb-tfu-test.c - one TFU job on the V3D 3.3: glGenerateMipmap on a 64x64
// RGBA8 texture (Mesa's v3d_generate_mipmap hands it to the TFU), then draws
// level 1 (32x32) into a 32x32 FBO and checks the colour. No window, no
// dma-buf: EGL surfaceless on /dev/dri/renderD128.
// Controls without the TFU: "kstb-tfu-test manual" uploads level 1 with
// glTexImage2D instead (same expected colour); "kstb-tfu-test level0" draws
// level 0 (every pixel red or blue).
//   gcc -O2 -o kstb-tfu-test kstb-tfu-test.c -lEGL -lGLESv2
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define step(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static const char *vs_src =
	"attribute vec2 pos; varying vec2 uv;\n"
	"void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src =
	"precision mediump float; varying vec2 uv; uniform sampler2D t;\n"
	"void main() { gl_FragColor = texture2D(t, uv); }\n";

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

int main(int argc, char **argv)
{
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_NONE };
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	static unsigned char pix[64 * 64 * 4], out[32 * 32 * 4];
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLint n;
	GLuint tex, rb, fb, prog;
	int i, bad = 0;
	const char *mode = argc > 1 ? argv[1] : "tfu";
	int level0 = !strcmp(mode, "level0");

	PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = get_dpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(dpy, NULL, NULL) ||
	    !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
		step("EGL init failed");
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		step("eglMakeCurrent failed");
		return 1;
	}
	step("renderer: %s", glGetString(GL_RENDERER));

	// 2x2 pixel checkerboard of (255,0,0) and (0,0,255): every 2x2 box
	// averages to (128,0,128)
	for (i = 0; i < 64 * 64; i++) {
		int x = i % 64, y = i / 64, red = ((x ^ y) & 1) == 0;
		pix[i * 4 + 0] = red ? 255 : 0;
		pix[i * 4 + 1] = 0;
		pix[i * 4 + 2] = red ? 0 : 255;
		pix[i * 4 + 3] = 255;
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
	glFinish();
	step("texture uploaded");

	if (!strcmp(mode, "manual")) {
		// levels 1..6 by hand, all (128,0,128)
		for (i = 0; i < 32 * 32; i++) {
			pix[i * 4 + 0] = 128;
			pix[i * 4 + 1] = 0;
			pix[i * 4 + 2] = 128;
		}
		for (i = 1; i <= 6; i++)
			glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA, 64 >> i, 64 >> i, 0,
				     GL_RGBA, GL_UNSIGNED_BYTE, pix);
		glFinish();
		step("levels 1-6 uploaded, error 0x%x", glGetError());
	} else if (!level0) {
		step("glGenerateMipmap (TFU job) ...");
		glGenerateMipmap(GL_TEXTURE_2D);
		glFinish();
		step("glGenerateMipmap done, error 0x%x", glGetError());
	}

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, 32, 32);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glViewport(0, 0, 32, 32);

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
			level0 ? GL_NEAREST : GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, out);
	step("level 1 drawn, error 0x%x", glGetError());

	// RGB565 target: 128 reads back as 132 or 123 depending on rounding
	for (i = 0; i < 32 * 32; i++) {
		int r = out[i * 4], g = out[i * 4 + 1], b = out[i * 4 + 2];

		if (level0 ? !((r > 247 && b < 8) || (r < 8 && b > 247)) || g > 8 :
		    abs(r - 128) > 8 || g > 8 || abs(b - 128) > 8)
			bad++;
	}
	step("pixel 0: %d,%d,%d  pixel 527: %d,%d,%d", out[0], out[1], out[2],
	     out[527 * 4], out[527 * 4 + 1], out[527 * 4 + 2]);
	step("%s %s: %d of 1024 pixels off (want %s)", mode, bad ? "FAIL" : "OK", bad,
	     level0 ? "255,0,0 or 0,0,255" : "about 128,0,128");
	return bad != 0;
}
