// kstb-varying-test.c - varyings from the vertex to the fragment shader on the
// V3D 3.3: the vertex shader writes N float varyings v0..v(N-1) with the
// constant values (i+1)/16, the fragment shader outputs one of them (chosen by
// a uniform) as red. Prints what each varying arrives as. No window:
// EGL surfaceless on /dev/dri/renderD128, a 16x16 RGBA8 FBO.
//   kstb-varying-test [N]   (default 8, at most 8)
//   gcc -O2 -o kstb-varying-test kstb-varying-test.c -lEGL -lGLESv2
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
		fprintf(stderr, "shader: %s\n%s\n", log, src);
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
	int n = argc > 1 ? atoi(argv[1]) : 8, i, bad = 0;
	char vs[2048], fs[2048], *p;
	unsigned char px[4];
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLint num;
	GLuint fb, rb, prog;
	GLint sel;

	if (n < 1 || n > 8)
		n = 8;
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = get_dpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(dpy, NULL, NULL) ||
	    !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &num) || num < 1)
		return 1;
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
			    eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr)))
		return 1;

	// separate float varyings, so each is its own (scalar) varying
	p = vs;
	p += sprintf(p, "attribute vec2 pos;\n");
	for (i = 0; i < n; i++)
		p += sprintf(p, "varying float v%d;\n", i);
	p += sprintf(p, "void main() {\n");
	for (i = 0; i < n; i++)
		p += sprintf(p, "  v%d = %d.0 / 16.0;\n", i, i + 1);
	p += sprintf(p, "  gl_Position = vec4(pos, 0.0, 1.0);\n}\n");
	p = fs;
	p += sprintf(p, "precision mediump float; uniform int sel;\n");
	for (i = 0; i < n; i++)
		p += sprintf(p, "varying float v%d;\n", i);
	p += sprintf(p, "void main() {\n  float r = 0.0;\n");
	// every varying is read in every run, so all of them stay live
	for (i = 0; i < n; i++)
		p += sprintf(p, "  if (sel == %d) r = v%d; else r += v%d * 0.0;\n", i, i, i);
	p += sprintf(p, "  gl_FragColor = vec4(r, 0.0, 0.0, 1.0);\n}\n");

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, 16, 16);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glViewport(0, 0, 16, 16);

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	sel = glGetUniformLocation(prog, "sel");
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);

	printf("%d varyings:", n);
	for (i = 0; i < n; i++) {
		int want = ((i + 1) * 255 + 8) / 16;

		glUniform1i(sel, i);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf(" v%d=%d/%d", i, px[0], want);
		if (abs(px[0] - want) > 2)
			bad++;
	}
	printf("\n%s: %d of %d varyings wrong\n", bad ? "FAIL" : "OK", bad, n);
	return bad != 0;
}
