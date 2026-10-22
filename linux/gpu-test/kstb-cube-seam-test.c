// Sample face +X of a 64x64 RGBA8 cube map near its edge to +Y and print
// how much of the +Y face each lookup blends in (seamless filtering).
//   kstb-cube-seam-test linear|lmn|lml|nmn|nearest [max_level] [lod]
//   kstb-cube-seam-test linear|lmn|lml|nmn|nearest max_level quad W H
// quad: draws the whole face into W x H pixels with texture() (implicit LOD)
// and mag filter NEAREST, as dEQP texture.mipmap.cube does, and prints R of
// the bottom and top two rows (bottom = the edge to -Y, top = to +Y).
// Every face/level is one solid colour: R = 20 + 40 * face, G = 20 + 30 *
// level. Pixel x samples at (x + 1) / 8 level-0 texels from the edge with
// textureLod(lod). "share" is the +Y part of R; "seamless" is the share a
// seamless bilinear lookup of the level that the min/max level selects gives.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <gbm.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s (EGL 0x%x, GL 0x%x)\n", \
            __LINE__, #x, eglGetError(), glGetError()); exit(1); \
} } while (0)

#define SIZE 64
#define LEVELS 7
#define N 8

static const GLenum faces[6] = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

static GLuint shader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile: %s\n", log);
        exit(1);
    }
    return sh;
}

static const char *vs =
    "#version 300 es\n"
    "void main() {\n"
    "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// +X: ma = x, sc = -z, tc = -y; tc = -1 is the edge to +Y. s stays at the
// face centre.
static const char *fs =
    "#version 300 es\nprecision highp float;\n"
    "uniform highp samplerCube tex;\nuniform float lod;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  float dist = floor(gl_FragCoord.x + 1.0) / 8.0;\n"
    "  float t = -1.0 + 2.0 * dist / 64.0;\n"
    "  o = textureLod(tex, vec3(1.0, -t, 0.0), lod);\n"
    "}\n";

static const char *fs_quad =
    "#version 300 es\nprecision highp float;\n"
    "uniform highp samplerCube tex;\nuniform vec2 size;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 st = gl_FragCoord.xy / size * 2.0 - 1.0;\n"
    "  o = texture(tex, vec3(1.0, -st.y, -st.x));\n"
    "}\n";

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "linear";
    int max_level = argc > 2 ? atoi(argv[2]) : LEVELS - 1;
    int quad = argc > 3 && !strcmp(argv[3], "quad");
    float lod = argc > 3 && !quad ? atof(argv[3]) : 0.0f;
    int w = quad && argc > 4 ? atoi(argv[4]) : N;
    int h = quad && argc > 5 ? atoi(argv[5]) : 1;
    GLenum minf = !strcmp(mode, "lmn") ? GL_LINEAR_MIPMAP_NEAREST :
                  !strcmp(mode, "lml") ? GL_LINEAR_MIPMAP_LINEAR :
                  !strcmp(mode, "nmn") ? GL_NEAREST_MIPMAP_NEAREST :
                  !strcmp(mode, "nearest") ? GL_NEAREST : GL_LINEAR;

    int fd = open("/dev/dri/renderD128", O_RDWR);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));

    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, quad ? fs_quad : fs));
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    GLubyte *buf = malloc(SIZE * SIZE * 4);
    for (int f = 0; f < 6; f++) {
        for (int l = 0; l < LEVELS; l++) {
            int s = SIZE >> l;
            for (int i = 0; i < s * s; i++) {
                buf[i * 4 + 0] = 20 + 40 * f;
                buf[i * 4 + 1] = 20 + 30 * l;
                buf[i * 4 + 2] = 128;
                buf[i * 4 + 3] = 255;
            }
            glTexImage2D(faces[f], l, GL_RGBA8, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
        }
    }
    free(buf);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, minf);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, quad ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, max_level);
    CHECK(glGetError() == GL_NO_ERROR);

    GLuint rb, fbo;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, w, h);
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    glUniform1f(glGetUniformLocation(p, "lod"), lod);
    glUniform2f(glGetUniformLocation(p, "size"), w, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (quad) {
        GLubyte *q = malloc(w * h * 4);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, q);
        CHECK(glGetError() == GL_NO_ERROR);
        printf("%s max_level %d quad %dx%d, R per row (20 = +X only):\n",
               mode, max_level, w, h);
        int rows[4] = { 0, 1, h - 2, h - 1 };
        for (int i = 0; i < 4; i++) {
            printf("  row %3d:", rows[i]);
            for (int x = 0; x < w; x++)
                printf(" %3d", q[(rows[i] * w + x) * 4]);
            printf("\n");
        }
        free(q);
        return 0;
    }
    GLubyte px[N * 4];
    glReadPixels(0, 0, N, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(glGetError() == GL_NO_ERROR);

    // The level a seamless bilinear lookup reads: magnification and
    // non-mipmapped filters use the base level, mipmapped ones the LOD
    // clamped to max_level (nearest mip).
    int mip = minf != GL_LINEAR && minf != GL_NEAREST;
    int level = lod <= 0 || !mip ? 0 : (int)fminf(floorf(lod + 0.5f), max_level);
    int linear = lod <= 0 || minf == GL_LINEAR || minf == GL_LINEAR_MIPMAP_NEAREST ||
                 minf == GL_LINEAR_MIPMAP_LINEAR;
    printf("%s max_level %d lod %.2f (level %d, %s):\n", mode, max_level, lod, level,
           linear ? "linear" : "nearest");
    for (int x = 0; x < N; x++) {
        float dist = (x + 1) / 8.0f / (1 << level);
        float seamless = linear && dist < 0.5f ? 0.5f - dist : 0.0f;
        int r = px[x * 4], g = px[x * 4 + 1];
        float share = (r - 20) / 80.0f;
        printf("  %.3f texels from the edge: R %3d G %3d (level %.1f) share %.3f seamless %.3f\n",
               (x + 1) / 8.0f, r, g, (g - 20) / 30.0f, share, seamless);
    }
    return 0;
}
