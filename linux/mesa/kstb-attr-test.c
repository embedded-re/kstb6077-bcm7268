// kstb-attr-test.c - vertex attributes through varyings to the fragment
// shader on the V3D 3.3, shaped like dEQP-GLES2's operator tests (which fail
// for vec3 + vec3 in the fragment shader): vec4 attributes a0 and a1, each
// copied to a vec4 varying, the fragment shader writes one component of
// a0 or a1 (chosen by a uniform) as red. The fragment shader reads COMP
// components of each varying (default 3, like ".xyz"), so the linker packs
// them. The values differ per vertex: component i of a0 at vertex v is
// (i + 1 + 8v)/40, of a1 (i + 5 + 8v)/40; each is checked at the pixel nearest
// each corner (vertex). EGL surfaceless, 16x16 FBO.
//   kstb-attr-test [COMP]   (1 to 4)
//   gcc -O2 -o kstb-attr-test kstb-attr-test.c -lEGL -lGLESv2
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>

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

static const char *vs_src =
	"attribute vec4 pos; attribute vec4 a0; attribute vec4 a1;\n"
	"varying vec4 v0; varying vec4 v1;\n"
	"void main() { gl_Position = pos; v0 = a0; v1 = a1; }\n";

int main(int argc, char **argv)
{
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_NONE };
	static const char *sw = "xyzw";
	GLfloat pos[4][4] = { {-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1} };
	GLfloat a[2][4][4];
	int comp = argc > 1 ? atoi(argv[1]) : 3, i, v, bad = 0, sel_n = 0;
	char fs[2048], *p;
	unsigned char px[4];
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLint num;
	GLuint fb, rb, prog;
	GLint sel;

	if (comp < 1 || comp > 4)
		comp = 3;
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

	for (v = 0; v < 4; v++)
		for (i = 0; i < 4; i++) {
			a[0][v][i] = (i + 1 + 8 * v) / 40.0f;
			a[1][v][i] = (i + 5 + 8 * v) / 40.0f;
		}

	// all COMP components of both varyings stay live in every run
	p = fs;
	p += sprintf(p, "precision mediump float; uniform int sel;\n"
		     "varying vec4 v0; varying vec4 v1;\nvoid main() {\n  float r = 0.0;\n");
	for (v = 0; v < 2; v++)
		for (i = 0; i < comp; i++, sel_n++)
			p += sprintf(p, "  if (sel == %d) r = v%d.%c; else r += v%d.%c * 0.0;\n",
				     sel_n, v, sw[i], v, sw[i]);
	p += sprintf(p, "  gl_FragColor = vec4(r, 0.0, 0.0, 1.0);\n}\n");

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, 16, 16);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glViewport(0, 0, 16, 16);

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "a0");
	glBindAttribLocation(prog, 2, "a1");
	glLinkProgram(prog);
	glUseProgram(prog);
	sel = glGetUniformLocation(prog, "sel");
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, a[0]);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, a[1]);
	for (i = 0; i < 3; i++)
		glEnableVertexAttribArray(i);

	printf("%d components of each varying, at the 4 corners:\n", comp);
	sel_n = 0;
	for (v = 0; v < 2; v++)
		for (i = 0; i < comp; i++, sel_n++) {
			static const int cx[4] = { 0, 15, 0, 15 }, cy[4] = { 0, 0, 15, 15 };
			int k;

			glUniform1i(sel, sel_n);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			printf(" a%d.%c:", v, sw[i]);
			for (k = 0; k < 4; k++) {
				int want = (int)(a[v][k][i] * 255 + 0.5);

				glReadPixels(cx[k], cy[k], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
				printf(" %d/%d", px[0], want);
				// a pixel centre is half a pixel in from the vertex
				if (abs(px[0] - want) > 12)
					bad++;
			}
			printf("\n");
		}
	printf("%s: %d of %d wrong\n", bad ? "FAIL" : "OK", bad, 8 * comp);
	return bad != 0;
}
