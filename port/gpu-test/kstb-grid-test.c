// kstb-grid-test.c - many vertices with many VS outputs on the V3D 3.3:
// dEQP-GLES2's fragment operator tests (vec3 + vec3 etc.) fail, a single quad
// with the same shaders passes. This draws a GRID x GRID grid of quads (6
// vertices each, no index buffer) over a 32x32 FBO; vec4 attributes a0, a1
// are linear in the position, each copied to a vec4 varying, and the fragment
// shader reads COMP components of each (so the linker packs them) and writes
// one (chosen by a uniform) as red. Every pixel is checked.
//   kstb-grid-test [COMP [GRID]]   (COMP 1-4, default 3; GRID default 16)
//   kstb-grid-test sum [GRID]      like dEQP's add.highp_vec3_fragment: the
//                                  fragment shader writes
//                                  (v0.zxy + v1.yzx) / 2 to RGB
// KSTB_INDEXED=1: (GRID+1)^2 shared vertices drawn with glDrawElements
// (GL_UNSIGNED_SHORT), as dEQP does, so the vertex cache reuses outputs.
// KSTB_INDEXED=8: the same with GL_UNSIGNED_BYTE indices (GRID at most 15).
// KSTB_BLEND=1: blending on with glBlendFunc(GL_ONE, GL_ZERO) (same result).
// KSTB_LOC="p a b": attribute locations of pos, a0, a1 (default "0 1 2").
// KSTB_DS=1: a depth/stencil (D24S8) renderbuffer on the FBO too.
// KSTB_VP="x y": draw into a WxW viewport at (x, y) of a 256x256 FBO.
// KSTB_SIGNED=1: dEQP's signed attribute pattern, with different gradients
// per component. Component checks map [-1,1] into [0.25,0.75].
//   gcc -O2 -o kstb-grid-test kstb-grid-test.c -lEGL -lGLESv2
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>

#define W 32

static int signed_pattern;

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

// component c of attribute k at (u, w) in [0, 1]^2
static float value(int k, int c, float u, float w)
{
	if (signed_pattern) {
		float v[2][4] = {
			{2*u-1, 2*w-1, 1-2*u, 1-2*w},
			{2*w-1, 1-2*u, 1-2*w, 2*u-1},
		};
		return v[k][c];
	}
	return (c + 1 + 4 * k) / 40.0f + 0.25f * u + 0.125f * w;
}

static GLubyte *idx8;

static void draw(int nv, const GLushort *idx)
{
	if (idx8)
		glDrawElements(GL_TRIANGLES, nv, GL_UNSIGNED_BYTE, idx8);
	else if (idx)
		glDrawElements(GL_TRIANGLES, nv, GL_UNSIGNED_SHORT, idx);
	else
		glDrawArrays(GL_TRIANGLES, 0, nv);
}

int main(int argc, char **argv)
{
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_NONE };
	static const int corner[6][2] = { {0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1} };
	static const char *sw = "xyzw";
	static unsigned char img[W * W * 4];
	int sum = argc > 1 && argv[1][0] == 's';
	int comp = argc > 1 ? atoi(argv[1]) : 3;
	int grid = argc > 2 ? atoi(argv[2]) : 16;
	int nv, i, k, c, x, y, sel_n = 0, total_bad = 0;
	int indexed = getenv("KSTB_INDEXED") ? atoi(getenv("KSTB_INDEXED")) : 0;
	GLushort *idx = NULL;
	int loc[3] = { 0, 1, 2 };
	int vx = 0, vy = 0, fbw;
	GLfloat *pos, *a[2];
	char fs[2048], *p;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLint num;
	GLuint fb, rb, prog;
	GLint sel;

	signed_pattern = getenv("KSTB_SIGNED") && atoi(getenv("KSTB_SIGNED"));

	if (comp < 1 || comp > 4)
		comp = 3;
	if (grid < 1)
		grid = 1;
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

	nv = grid * grid * 6;
	pos = malloc(nv * 4 * sizeof(GLfloat));
	a[0] = malloc(nv * 4 * sizeof(GLfloat));
	a[1] = malloc(nv * 4 * sizeof(GLfloat));
	if (indexed == 8 && grid > 15) {
		grid = 15;
		nv = grid * grid * 6;
	}
	if (indexed) {
		if ((grid + 1) * (grid + 1) > 65536)
			grid = 255;
		idx = malloc(nv * sizeof(GLushort));
		for (i = 0; i < nv; i++) {
			int cell = i / 6, cx = cell % grid, cy = cell / grid;

			idx[i] = (cy + corner[i % 6][1]) * (grid + 1) +
				 cx + corner[i % 6][0];
		}
		if (indexed == 8) {
			idx8 = malloc(nv);
			for (i = 0; i < nv; i++)
				idx8[i] = idx[i];
		}
	}
	for (i = 0; i < (indexed ? (grid + 1) * (grid + 1) : nv); i++) {
		int cell = i / 6, cx = cell % grid, cy = cell / grid;
		float u = (float)(cx + corner[i % 6][0]) / grid;
		float w = (float)(cy + corner[i % 6][1]) / grid;

		if (indexed) {
			u = (float)(i % (grid + 1)) / grid;
			w = (float)(i / (grid + 1)) / grid;
		}

		pos[i * 4 + 0] = u * 2 - 1;
		pos[i * 4 + 1] = w * 2 - 1;
		pos[i * 4 + 2] = 0;
		pos[i * 4 + 3] = 1;
		for (k = 0; k < 2; k++)
			for (c = 0; c < 4; c++)
				a[k][i * 4 + c] = value(k, c, u, w);
	}

	if (sum)
		comp = 3;
	p = fs;
	if (sum)
		p += sprintf(p, "uniform int sel; varying highp vec4 v0; varying highp vec4 v1;\n"
			     "void main() {\n  highp vec3 res = v0.zxy + v1.yzx;\n"
			     "  mediump vec4 color = vec4(0.0, 0.0, 0.0, 1.0);\n"
			     "  color.xyz = res * 0.5;\n  gl_FragColor = color;\n}\n");
	else {
	p += sprintf(p, "precision mediump float; uniform int sel;\n"
		     "varying vec4 v0; varying vec4 v1;\nvoid main() {\n  float r = 0.0;\n");
	for (k = 0; k < 2; k++)
		for (c = 0; c < comp; c++, sel_n++)
			p += sprintf(p, "  if (sel == %d) r = v%d.%c; else r += v%d.%c * 0.0;\n",
				     sel_n, k, sw[c], k, sw[c]);
	p += sprintf(p, "  gl_FragColor = vec4(%s, 0.0, 0.0, 1.0);\n}\n",
		     signed_pattern ? "r * 0.25 + 0.5" : "r");
	}

	if (getenv("KSTB_VP"))
		sscanf(getenv("KSTB_VP"), "%d %d", &vx, &vy);
	fbw = getenv("KSTB_VP") ? 256 : W;
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, fbw, fbw);
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (getenv("KSTB_DS") && atoi(getenv("KSTB_DS"))) {
		GLuint ds;

		glGenRenderbuffers(1, &ds);
		glBindRenderbuffer(GL_RENDERBUFFER, ds);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, fbw, fbw);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ds);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ds);
	}
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "framebuffer incomplete\n");
		return 1;
	}
	glViewport(vx, vy, W, W);
	if (getenv("KSTB_BLEND") && atoi(getenv("KSTB_BLEND"))) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ZERO);
	}

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	if (getenv("KSTB_LOC"))
		sscanf(getenv("KSTB_LOC"), "%d %d %d", &loc[0], &loc[1], &loc[2]);
	glBindAttribLocation(prog, loc[0], "pos");
	glBindAttribLocation(prog, loc[1], "a0");
	glBindAttribLocation(prog, loc[2], "a1");
	glLinkProgram(prog);
	glUseProgram(prog);
	sel = glGetUniformLocation(prog, "sel");
	glVertexAttribPointer(loc[0], 4, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(loc[1], 4, GL_FLOAT, GL_FALSE, 0, a[0]);
	glVertexAttribPointer(loc[2], 4, GL_FLOAT, GL_FALSE, 0, a[1]);
	for (i = 0; i < 3; i++)
		glEnableVertexAttribArray(loc[i]);

	if (sum) {
		static const int c0[3] = { 2, 0, 1 }, c1[3] = { 1, 2, 0 };

		printf("sum, %dx%d grid (%d vertices%s):\n", grid, grid, nv,
		       indexed == 8 ? ", 8-bit indices" : indexed ? ", indexed" : "");
		glClear(GL_COLOR_BUFFER_BIT);
		draw(nv, idx);
		glReadPixels(vx, vy, W, W, GL_RGBA, GL_UNSIGNED_BYTE, img);
		for (c = 0; c < 3; c++) {
			int bad = 0, fx = -1, fy = -1, got = 0, want = 0;

			for (y = 0; y < W; y++)
				for (x = 0; x < W; x++) {
					float u = (x + 0.5f) / W, w = (y + 0.5f) / W;
					int g = img[(y * W + x) * 4 + c];
					int e = (int)((value(0, c0[c], u, w) +
						       value(1, c1[c], u, w)) * 0.5f * 255 + 0.5f);
					e = e < 0 ? 0 : e > 255 ? 255 : e;

					if (abs(g - e) > 3 && !bad++) {
						fx = x; fy = y; got = g; want = e;
					}
				}
			printf(" %c: %d of %d pixels off", "RGB"[c], bad, W * W);
			if (bad)
				printf(" (first at %d,%d: %d, want %d)", fx, fy, got, want);
			printf("\n");
			total_bad += bad;
		}
		printf("%s\n", total_bad ? "FAIL" : "OK");
		return total_bad != 0;
	}

	printf("%d components of each varying, %dx%d grid (%d vertices%s):\n",
	       comp, grid, grid, nv, indexed == 8 ? ", 8-bit indices" : indexed ? ", indexed" : "");
	sel_n = 0;
	for (k = 0; k < 2; k++)
		for (c = 0; c < comp; c++, sel_n++) {
			int bad = 0, fx = -1, fy = -1, got = 0, want = 0;

			glUniform1i(sel, sel_n);
			glClear(GL_COLOR_BUFFER_BIT);
			draw(nv, idx);
			glReadPixels(vx, vy, W, W, GL_RGBA, GL_UNSIGNED_BYTE, img);
			for (y = 0; y < W; y++)
				for (x = 0; x < W; x++) {
					int g = img[(y * W + x) * 4];
					float expected = value(k, c, (x + 0.5f) / W,
							       (y + 0.5f) / W);
					if (signed_pattern)
						expected = expected * 0.25f + 0.5f;
					int e = (int)(expected * 255 + 0.5f);

					if (abs(g - e) > 3) {
						if (!bad++) {
							fx = x; fy = y; got = g; want = e;
						}
					}
				}
			printf(" a%d.%c: %d of %d pixels off", k, sw[c], bad, W * W);
			if (bad)
				printf(" (first at %d,%d: %d, want %d)", fx, fy, got, want);
			printf("\n");
			total_bad += bad;
		}
	printf("%s\n", total_bad ? "FAIL" : "OK");
	return total_bad != 0;
}
