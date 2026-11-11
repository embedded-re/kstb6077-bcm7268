// Fragment SSBO layout and range binding, checked word for word on the CPU.
// MESA_GLES_VERSION_OVERRIDE=3.1 kstb-ssbo-layout-test [W H]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static const char *vs = "#version 310 es\n"
    "void main() { gl_Position=vec4(gl_VertexID==1?3.0:-1.0,"
    "gl_VertexID==2?3.0:-1.0,0.0,1.0); }\n";
static const char *body =
    "precision highp float; precision highp int;\n"
    "struct Record { float coeff[3]; vec3 xyz; float w; mat2 m; uint flags; };\n"
    "layout(%s,binding=0) readonly buffer S { uint prefix; Record rec[]; };\n"
    "layout(std430,binding=1) writeonly buffer D { uint result[]; };\n"
    "uniform uint width; out vec4 color;\n"
    "void main() { uint i=uint(gl_FragCoord.y)*width+uint(gl_FragCoord.x);\n"
    " Record r=rec[i]; uint j=i*12u;\n"
    " result[j]=floatBitsToUint(r.coeff[0]);\n"
    " result[j+1u]=floatBitsToUint(r.coeff[1]);\n"
    " result[j+2u]=floatBitsToUint(r.coeff[2]);\n"
    " result[j+3u]=floatBitsToUint(r.xyz.x);\n"
    " result[j+4u]=floatBitsToUint(r.xyz.y);\n"
    " result[j+5u]=floatBitsToUint(r.xyz.z);\n"
    " result[j+6u]=floatBitsToUint(r.w);\n"
    " result[j+7u]=floatBitsToUint(r.m[0][0]);\n"
    " result[j+8u]=floatBitsToUint(r.m[0][1]);\n"
    " result[j+9u]=floatBitsToUint(r.m[1][0]);\n"
    " result[j+10u]=floatBitsToUint(r.m[1][1]);\n"
    " result[j+11u]=r.flags^prefix; color=vec4(1.0); }\n";

static uint32_t value(uint32_t i, unsigned field)
{
    if (field == 11)
        return i ^ 0xabcdef01u;
    float f = (float)(i * 16 + field + 1);
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static void run(int w, int h, int std140)
{
    const char *layout = std140 ? "std140" : "std430";
    // Byte offsets derived from the two standard layouts, independent of GL queries.
    const unsigned offsets140[12] = {0,16,32,48,52,56,60,64,68,80,84,96};
    const unsigned offsets430[12] = {0,4,8,16,20,24,28,32,36,40,44,48};
    const unsigned *offsets = std140 ? offsets140 : offsets430;
    size_t n = (size_t)w * h, stride = std140 ? 112 : 64;
    size_t srcbytes = 16 + n * stride, dstbytes = n * 12 * 4;
    GLint alignment = 0;
    glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
    CHECK(alignment > 0);
    size_t base = ((4096 + (size_t)alignment - 1) / alignment) * alignment;
    size_t srctotal = base + srcbytes + 64, dsttotal = base + dstbytes + 64;
    unsigned char *src = malloc(srctotal), *dst = malloc(dsttotal);
    CHECK(src && dst);
    memset(src, 0xa5, srctotal);
    memset(dst, 0x5a, dsttotal);
    const uint32_t prefix = 0x12345678u;
    memcpy(src + base, &prefix, 4);
    for (uint32_t i = 0; i < n; i++)
        for (unsigned j = 0; j < 12; j++) {
            uint32_t bits = value(i, j);
            memcpy(src + base + 16 + i * stride + offsets[j], &bits, 4);
        }
    char fs[4096];
    int len = snprintf(fs, sizeof(fs), "#version 310 es\n");
    CHECK(len > 0 && snprintf(fs + len, sizeof(fs) - len, body, layout) < (int)sizeof(fs) - len);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    CHECK(ok);
    GLuint bufs[2];
    glGenBuffers(2, bufs);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, srctotal, src, GL_STATIC_READ);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bufs[0], base, srcbytes);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, dsttotal, dst, GL_DYNAMIC_READ);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, bufs[1], base, dstbytes);
    glUseProgram(prog);
    glUniform1ui(glGetUniformLocation(prog, "width"), w);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);
    const unsigned char *data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, dsttotal, GL_MAP_READ_BIT);
    CHECK(data);
    size_t errors = 0;
    for (size_t k = 0; k < dsttotal / 4; k++) {
        uint32_t got, expected = 0x5a5a5a5au;
        if (k * 4 >= base && k * 4 < base + dstbytes) {
            size_t idx = k - base / 4;
            unsigned field = idx % 12;
            expected = value(idx / 12, field) ^ (field == 11 ? prefix : 0);
        }
        memcpy(&got, data + k * 4, 4);
        if (got != expected) {
            if (errors < 8)
                fprintf(stderr, "%s byte %zu: %#x expected %#x\n", layout, k * 4, got, expected);
            errors++;
        }
    }
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bufs[0]);
    data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, srctotal, GL_MAP_READ_BIT);
    CHECK(data);
    if (memcmp(data, src, srctotal)) errors++;
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    printf("%s %dx%d stride=%zu binding_offset=%zu: %s (%zu errors; source and destination guards checked)\n",
           layout, w, h, stride, base, errors ? "FAIL" : "exact", errors);
    CHECK(errors == 0);
    glDeleteBuffers(2, bufs);
    glDeleteProgram(prog);
    free(src); free(dst);
}

int main(int argc, char **argv)
{
    CHECK(argc == 1 || argc == 3);
    int w = argc == 3 ? atoi(argv[1]) : 17, h = argc == 3 ? atoi(argv[2]) : 13;
    CHECK(w > 0 && w <= 512 && h > 0 && h <= 512);
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    printf("%s\n", glGetString(GL_VERSION));
    GLuint rb, fbo;
    glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, w, h);
    run(w, h, 0); run(w, h, 1);
    CHECK(glGetError() == GL_NO_ERROR);
    glDeleteFramebuffers(1, &fbo); glDeleteRenderbuffers(1, &rb);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm); close(fd);
    return 0;
}
