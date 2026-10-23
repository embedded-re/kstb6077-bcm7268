// Fragment SSBO atomics, with aggregate and returned-value checks on the CPU.
// MESA_GLES_VERSION_OVERRIDE=3.1 kstb-ssbo-atomic-test [W H]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static GLuint shader(GLenum type, const char *source)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &source, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        exit(1);
    }
    return sh;
}

static const char *vs =
    "#version 310 es\n"
    "void main() { gl_Position = vec4(gl_VertexID == 1 ? 3.0 : -1.0,\n"
    " gl_VertexID == 2 ? 3.0 : -1.0, 0.0, 1.0); }\n";

static const char *fs =
    "#version 310 es\nprecision highp float;\nprecision highp int;\n"
    "layout(std430, binding=0) buffer B { uint u[12]; int s[4]; uint r[]; };\n"
    "uniform uint width; uniform uint count;\nout vec4 color;\n"
    "void main() {\n"
    " uint i = uint(gl_FragCoord.y) * width + uint(gl_FragCoord.x);\n"
    " uint bit = 1u << (i & 31u);\n"
    " r[3u*i] = atomicAdd(u[0], 1u);\n"
    " atomicMin(u[1], i); atomicMax(u[2], i);\n"
    " atomicOr(u[3], bit); atomicAnd(u[4], ~bit); atomicXor(u[5], bit);\n"
    " r[3u*i+1u] = atomicExchange(u[6], i+1u);\n"
    " r[3u*i+2u] = atomicCompSwap(u[7], 0u, i+1u);\n"
    " atomicAdd(u[8], 0xffffffffu); atomicAdd(u[9], 7u);\n"
    " atomicAdd(u[10], 0xfffffff9u);\n"
    " atomicAdd(s[0], -1);\n"
    " int v = int(i) - int(count/2u);\n"
    " atomicMin(s[1], v); atomicMax(s[2], v);\n"
    " color = vec4(1.0);\n}\n";

int main(int argc, char **argv)
{
    if (argc != 1 && argc != 3) {
        fprintf(stderr, "usage: %s [W H]\n", argv[0]);
        return 1;
    }
    int w = argc == 3 ? atoi(argv[1]) : 64;
    int h = argc == 3 ? atoi(argv[2]) : 64;
    CHECK(w > 0 && w <= 2048 && h > 0 && h <= 2048);
    uint32_t n = (uint32_t)w * h;
    size_t bytes = (16 + 3 * (size_t)n) * sizeof(uint32_t);
    uint32_t *initial = calloc(1, bytes);
    CHECK(initial);
    initial[1] = initial[4] = UINT32_MAX;
    initial[11] = 0xdecafbadu;
    initial[13] = INT32_MAX;
    initial[14] = (uint32_t)INT32_MIN;
    initial[15] = 123456789u;
    for (size_t i = 16; i < bytes / 4; i++)
        initial[i] = 0xdeadbeefu;

    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    printf("%s, %dx%d pixels, %u atomic participants\n", glGetString(GL_VERSION), w, h, n);
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);

    GLuint buf, rb, fbo;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf);
    free(initial);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glUseProgram(p);
    glUniform1ui(glGetUniformLocation(p, "width"), w);
    glUniform1ui(glGetUniformLocation(p, "count"), n);
    glViewport(0, 0, w, h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);

    const uint32_t *data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    CHECK(data);
    uint32_t bits_or = 0, bits_and = UINT32_MAX, bits_xor = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bit = 1u << (i & 31);
        bits_or |= bit;
        bits_and &= ~bit;
        bits_xor ^= bit;
    }
    uint32_t expected[16] = { n, 0, n - 1, bits_or, bits_and, bits_xor,
        0, 0, 0u - n, 7u * n, 0u - 7u * n, 0xdecafbadu,
        0u - n, (uint32_t)(-(int32_t)(n / 2)),
        (uint32_t)((int32_t)(n - 1) - (int32_t)(n / 2)), 123456789u };
    int errors = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 6 || i == 7)
            continue;
        if (data[i] != expected[i]) {
            fprintf(stderr, "field[%d] = %#x, expected %#x\n", i, data[i], expected[i]);
            errors++;
        }
    }

    unsigned char *add_seen = calloc(n, 1), *exchange_seen = calloc((size_t)n + 1, 1);
    CHECK(add_seen && exchange_seen);
    uint32_t final_exchange = data[6], winner = data[7];
    if (final_exchange == 0 || final_exchange > n || winner == 0 || winner > n)
        errors++;
    else
        exchange_seen[final_exchange] = 1;
    uint32_t winners = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t add = data[16 + 3 * i], exchange = data[17 + 3 * i], cas = data[18 + 3 * i];
        if (add >= n || add_seen[add])
            errors++;
        else
            add_seen[add] = 1;
        if (exchange > n || exchange_seen[exchange])
            errors++;
        else
            exchange_seen[exchange] = 1;
        if (cas == 0) {
            winners++;
            if (winner != i + 1)
                errors++;
        } else if (cas != winner) {
            errors++;
        }
    }
    if (winners != 1)
        errors++;
    printf("aggregates and returned values: %s (%d errors, CAS winner %u, exchange final %u)\n",
           errors ? "FAIL" : "exact", errors, winner, final_exchange);
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    CHECK(glGetError() == GL_NO_ERROR);
    free(add_seen);
    free(exchange_seen);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
    glDeleteBuffers(1, &buf);
    glDeleteProgram(p);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx));
    CHECK(eglTerminate(dpy));
    gbm_device_destroy(gbm);
    close(fd);
    return errors ? 1 : 0;
}
