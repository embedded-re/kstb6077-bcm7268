// A fragment shader writes one word per pixel into an SSBO through the TMU;
// the CPU reads the buffer back after the draw's job, and again after one
// more job (a clear of the same framebuffer) that writes nothing through
// the TMU. Counts per read: exact (the shader's value), old (still the CPU's
// fill 0xdeadbeef) and other.
//   kstb-tmu-write-test [W H [words-per-pixel]]   default 64 x 64, 1 word
// 4 words uses a uvec4 store; 8 words uses a batch of scalar stores.
// Needs GLES 3.1 SSBOs: MESA_GLES_VERSION_OVERRIDE=3.1, and on 3.3 a kernel
// with the cache flush (or a scratch driver that exposes SSBOs without it).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s (EGL 0x%x, GL 0x%x)\n", \
            __LINE__, #x, eglGetError(), glGetError()); exit(1); \
} } while (0)

#define OLD 0xdeadbeefu

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
    "#version 310 es\n"
    "void main() {\n"
    "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *fs =
    "precision highp float;\n"
    "#if WORDS_PER_PIXEL == 4\n"
    "layout(std430, binding = 0) buffer B { uvec4 v[]; };\n"
    "#else\n"
    "layout(std430, binding = 0) buffer B { uint v[]; };\n"
    "#endif\n"
    "uniform uint seed;\nuniform uint width;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  uint i = uint(gl_FragCoord.y) * width + uint(gl_FragCoord.x);\n"
    "#if WORDS_PER_PIXEL == 4\n"
    "  uvec4 j = uvec4(i * 4u) + uvec4(0u, 1u, 2u, 3u);\n"
    "  v[i] = uvec4(seed) ^ (j * 2654435761u);\n"
    "#else\n"
    "  for (uint j = 0u; j < uint(WORDS_PER_PIXEL); ++j) {\n"
    "    uint k = i * uint(WORDS_PER_PIXEL) + j;\n"
    "    v[k] = seed ^ (k * 2654435761u);\n"
    "  }\n"
    "#endif\n"
    "  o = vec4(1.0);\n"
    "}\n";

static int count(const char *when, GLuint seed, int n)
{
    const GLuint *v = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, n * 4, GL_MAP_READ_BIT);
    CHECK(v);
    int exact = 0, old = 0, other = 0, first = -1;
    for (int i = 0; i < n; i++) {
        if (v[i] == (seed ^ (i * 2654435761u)))
            exact++;
        else {
            if (v[i] == OLD)
                old++;
            else
                other++;
            if (first < 0)
                first = i;
        }
    }
    printf("%-15s exact %6d  old %6d  other %6d", when, exact, old, other);
    if (first >= 0)
        printf("  (first wrong word %d = 0x%08x)", first, v[first]);
    printf("\n");
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    return exact == n;
}

int main(int argc, char **argv)
{
    if (argc != 1 && argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s [W H [1|4|8]]\n", argv[0]);
        return 1;
    }
    int w = argc > 2 ? atoi(argv[1]) : 64;
    int h = argc > 2 ? atoi(argv[2]) : 64;
    int words = argc > 3 ? atoi(argv[3]) : 1;
    CHECK(w > 0 && w <= 2048 && h > 0 && h <= 2048);
    CHECK(words == 1 || words == 4 || words == 8);
    int n = w * h * words;
    GLuint seed = (GLuint)time(NULL) * 2246822519u;

    int fd = open("/dev/dri/renderD128", O_RDWR);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    GLint nssbo = 0;
    glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &nssbo);
    printf("%s, %d fragment SSBOs, %dx%d pixels, %d words/pixel, seed 0x%08x\n",
           glGetString(GL_VERSION), nssbo, w, h, words, seed);
    CHECK(nssbo > 0);

    GLuint p = glCreateProgram();
    char fragment_source[4096];
    int len = snprintf(fragment_source, sizeof(fragment_source),
                       "#version 310 es\n#define WORDS_PER_PIXEL %d\n%s", words, fs);
    CHECK(len > 0 && (size_t)len < sizeof(fragment_source));
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fragment_source));
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);

    GLuint *fill = malloc(n * 4);
    CHECK(fill);
    for (int i = 0; i < n; i++)
        fill[i] = OLD;
    GLuint buf;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, n * 4, fill, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf);
    free(fill);

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
    glUniform1ui(glGetUniformLocation(p, "seed"), seed);
    glUniform1ui(glGetUniformLocation(p, "width"), w);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);
    int exact_after_draw = count("after the draw", seed, n);

    // A job without TMU writes; the kernel invalidates the caches before it.
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    int exact_after_clear = count("after a clear", seed, n);
    CHECK(glGetError() == GL_NO_ERROR);
    return exact_after_draw && exact_after_clear ? 0 : 1;
}
