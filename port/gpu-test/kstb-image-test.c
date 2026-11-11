// Fragment image load/store, independently checked through SSBO and framebuffer readback.
// MESA_GLES_VERSION_OVERRIDE=3.1 kstb-image-test [W H [x_shift: -1|0|1]]
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
    "#version 310 es\nprecision highp float; precision highp int;\n"
    "layout(r32ui,binding=0) readonly uniform highp uimage2D src;\n"
    "layout(r32ui,binding=1) writeonly uniform highp uimage2D dst;\n"
    "layout(std430,binding=0) writeonly buffer B { uvec4 result[]; };\n"
    "uniform uint width; uniform int x_shift; out vec4 color;\n"
    "void main() { ivec2 p=ivec2(gl_FragCoord.xy);\n"
    " uint i=uint(p.y)*width+uint(p.x); ivec2 q=p+ivec2(x_shift,0);\n"
    " uvec4 v=imageLoad(src,q); result[i]=v;\n"
    " imageStore(dst,q,uvec4(v.r^0xdecafbadu,0u,0u,1u));\n"
    " color=vec4(1.0); }\n";

static uint32_t pattern(uint32_t i)
{
    return i * 1664525u + 1013904223u;
}

int main(int argc, char **argv)
{
    CHECK(argc == 1 || argc == 3 || argc == 4);
    int w = argc >= 3 ? atoi(argv[1]) : 17, h = argc >= 3 ? atoi(argv[2]) : 13;
    int shift = argc == 4 ? atoi(argv[3]) : 0;
    CHECK(w > 0 && w <= 1024 && h > 0 && h <= 1024);
    CHECK(shift >= -1 && shift <= 1);
    size_t n = (size_t)w * h;
    uint32_t *pixels = malloc(n * 4 * sizeof(uint32_t));
    CHECK(pixels);
    for (uint32_t i = 0; i < n; i++) pixels[i] = pattern(i);
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
    printf("%s, image %dx%d shift=%d\n", glGetString(GL_VERSION), w, h, shift);
    GLuint tex[2]; glGenTextures(2, tex);
    for (unsigned i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (i == 0)
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RED_INTEGER,GL_UNSIGNED_INT,pixels);
        else if (shift) {
            for (size_t j = 0; j < n; j++) pixels[j] = 0x12345678u;
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RED_INTEGER,GL_UNSIGNED_INT,pixels);
        }
        glBindImageTexture(i, tex[i], 0, GL_FALSE, 0, i ? GL_WRITE_ONLY : GL_READ_ONLY, GL_R32UI);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    CHECK(ok);
    GLuint buf; glGenBuffers(1, &buf); glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    for (size_t i = 0; i < n * 4; i++) pixels[i] = 0xdeadbeefu;
    glBufferData(GL_SHADER_STORAGE_BUFFER, n * 16, pixels, GL_DYNAMIC_READ);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buf);
    GLuint rb, fbo[2];
    glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glGenFramebuffers(2, fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glUseProgram(prog); glUniform1ui(glGetUniformLocation(prog, "width"), w);
    glUniform1i(glGetUniformLocation(prog, "x_shift"), shift);
    glViewport(0,0,w,h); glDrawArrays(GL_TRIANGLES,0,3);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish(); CHECK(glGetError() == GL_NO_ERROR);
    size_t errors = 0;
    const uint32_t *data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,n*16,GL_MAP_READ_BIT);
    CHECK(data);
    for (uint32_t i = 0; i < n; i++) {
        int sx = (int)(i % w) + shift;
        uint32_t expect[4] = {0,0,0,0};
        if (sx >= 0 && sx < w) {
            expect[0] = pattern((i / w) * w + sx);
            expect[3] = 1;
        }
        for (unsigned c = 0; c < 4; c++)
            if (data[4*i+c] != expect[c]) {
                if (errors < 8) fprintf(stderr,"load[%u].%u=%#x expected %#x\n",i,c,data[4*i+c],expect[c]);
                errors++;
            }
    }
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    glBindFramebuffer(GL_FRAMEBUFFER,fbo[1]);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[1],0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glReadPixels(0,0,w,h,GL_RGBA_INTEGER,GL_UNSIGNED_INT,pixels);
    CHECK(glGetError() == GL_NO_ERROR);
    for (uint32_t i = 0; i < n; i++) {
        int writer_x = (int)(i % w) - shift;
        uint32_t expect[4] = {writer_x >= 0 && writer_x < w ?
                             pattern(i)^0xdecafbadu : 0x12345678u,0,0,1};
        for (unsigned c = 0; c < 4; c++)
            if (pixels[4*i+c] != expect[c]) {
                if (errors < 8) fprintf(stderr,"store[%u].%u=%#x expected %#x\n",i,c,pixels[4*i+c],expect[c]);
                errors++;
            }
    }
    printf("r32ui image load/store: %s (%zu errors, %zu checked words per path)\n",errors?"FAIL":"exact",errors,n*4);
    glDeleteTextures(2,tex); glDeleteFramebuffers(2,fbo); glDeleteRenderbuffers(1,&rb);
    glDeleteBuffers(1,&buf); glDeleteProgram(prog);
    CHECK(eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy,ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm); close(fd); free(pixels);
    return errors ? 1 : 0;
}
