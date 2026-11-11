// Fragment image atomics, with aggregate and returned-value checks on the CPU.
// KSTB_COMPUTE=1 selects compute (8x4 local size), otherwise fragment.
// MESA_GLES_VERSION_OVERRIDE=3.1 kstb-image-atomic-test [W H]
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
    "#version 310 es\n#extension GL_OES_shader_image_atomic : require\nprecision highp float;\nprecision highp int;\n"
    "layout(std430, binding=0) buffer B { uint u[12]; int s[4]; uint r[]; };\n"
    "layout(r32ui,binding=0) coherent uniform highp uimage2D ui;\n"
    "layout(r32i,binding=1) coherent uniform highp iimage2D si;\n"
    "uniform uint width; uniform uint count;\n%s\n"
    "void main() {\n"
    " %s\n"
    " uint bit = 1u << (i & 31u);\n"
    " r[3u*i] = imageAtomicAdd(ui, ivec2(0,0), 1u);\n"
    " imageAtomicMin(ui, ivec2(1,0), i); imageAtomicMax(ui, ivec2(2,0), i);\n"
    " imageAtomicOr(ui, ivec2(3,0), bit); imageAtomicAnd(ui, ivec2(4,0), ~bit); imageAtomicXor(ui, ivec2(5,0), bit);\n"
    " r[3u*i+1u] = imageAtomicExchange(ui, ivec2(6,0), i+1u);\n"
    " r[3u*i+2u] = imageAtomicCompSwap(ui, ivec2(7,0), 0u, i+1u);\n"
    " imageAtomicAdd(ui, ivec2(8,0), 0xffffffffu); imageAtomicAdd(ui, ivec2(9,0), 7u);\n"
    " imageAtomicAdd(ui, ivec2(10,0), 0xfffffff9u);\n"
    " imageAtomicAdd(si, ivec2(0,0), -1);\n"
    " int v = int(i) - int(count/2u);\n"
    " imageAtomicMin(si, ivec2(1,0), v); imageAtomicMax(si, ivec2(2,0), v);\n"
    " %s\n}\n";

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
    int compute = getenv("KSTB_COMPUTE") != NULL;
    char source[8192];
    snprintf(source, sizeof(source), fs,
        compute ? "layout(local_size_x=8,local_size_y=4) in;" : "out vec4 color;",
        compute ? "if(gl_GlobalInvocationID.x>=width || gl_GlobalInvocationID.y>=count/width) return; uint i=gl_GlobalInvocationID.y*width+gl_GlobalInvocationID.x;" :
                  "uint i=uint(gl_FragCoord.y)*width+uint(gl_FragCoord.x);",
        compute ? "" : "color=vec4(1.0);");
    printf("stage: %s\n", compute ? "compute" : "fragment");
    if (!compute) glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(p, shader(compute ? GL_COMPUTE_SHADER : GL_FRAGMENT_SHADER, source));
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);

    GLuint buf, rb, fbo;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf);
    GLuint tex[2]; glGenTextures(2,tex);
    for(int i=0;i<2;i++) {
        GLenum internal=i?GL_R32I:GL_R32UI;
        glBindTexture(GL_TEXTURE_2D,tex[i]);
        glTexStorage2D(GL_TEXTURE_2D,1,internal,i?4:12,1);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,i?4:12,1,GL_RED_INTEGER,
                        i?GL_INT:GL_UNSIGNED_INT,initial+(i?12:0));
        glBindImageTexture(i,tex[i],0,GL_FALSE,0,GL_READ_WRITE,internal);
    }
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
    if (compute) glDispatchCompute((w+7)/8,(h+3)/4,1);
    else glDrawArrays(GL_TRIANGLES, 0, 3);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_FRAMEBUFFER_BARRIER_BIT);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);

    const uint32_t *data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    CHECK(data);
    uint32_t *combined=malloc(bytes);CHECK(combined);
    for(size_t i=0;i<bytes/4;i++) combined[i]=data[i];
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    GLuint read_fbo;glGenFramebuffers(1,&read_fbo);glBindFramebuffer(GL_FRAMEBUFFER,read_fbo);
    for(int i=0;i<2;i++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[i],0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glReadPixels(0,0,i?4:12,1,GL_RED_INTEGER,i?GL_INT:GL_UNSIGNED_INT,combined+(i?12:0));
        CHECK(glGetError()==GL_NO_ERROR);
    }
    data=combined;
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
    free(combined);glDeleteFramebuffers(1,&read_fbo);glDeleteTextures(2,tex);
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
