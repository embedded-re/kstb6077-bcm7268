// Gather shadow comparisons, including infinite floating-point border depths.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        exit(1);
    }
    return s;
}
static int compare(int func, float reference, float depth) {
    switch (func) {
    case 0:
        return 0;
    case 1:
        return reference < depth;
    case 2:
        return reference == depth;
    case 3:
        return reference <= depth;
    case 4:
        return reference > depth;
    case 5:
        return reference != depth;
    case 6:
        return reference >= depth;
    case 7:
        return 1;
    }
    abort();
}
int main(void) {
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    printf("%s\n", glGetString(GL_VERSION));
    const char *vs =
        "#version 310 es\nvoid "
        "main(){gl_Position=vec4(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0,0,1);}";
    const char *fs = "#version 310 es\nprecision highp float;precision highp int;"
                     "uniform highp sampler2DShadow tex;uniform highp float reference;"
                     "layout(std430,binding=0) buffer D {uvec4 result[];};out vec4 color;"
                     "void main(){ivec2 p=ivec2(gl_FragCoord.xy);"
                     "result[p.y*6+p.x]=floatBitsToUint(textureGather(tex,vec2(p)/"
                     "4.0,reference));color=vec4(1);}";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    CHECK(ok);
    glUseProgram(prog);
    GLint ref = glGetUniformLocation(prog, "reference");
    CHECK(ref >= 0);
    float texels[16];
    for (int i = 0; i < 16; i++)
        texels[i] = (i % 5) * 0.25f;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, 4, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_DEPTH_COMPONENT, GL_FLOAT, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    GLuint rb, fbo;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 6, 6);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 6, 6);
    uint32_t initial[1024 + 36 * 4 + 1024];
    memset(initial, 0xa5, sizeof(initial));
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(initial), initial, GL_DYNAMIC_COPY);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffer, 4096, 36 * 16);
    const GLenum funcs[] = {GL_NEVER,   GL_LESS,     GL_EQUAL,  GL_LEQUAL,
                            GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    const float borders[] = {-INFINITY, -0.5f, 0, 0.5f, 1, 1.5f, INFINITY};
    const float references[] = {-INFINITY, -0.5f, 0, 0.25f, 0.5f, 1, 1.25f, INFINITY};
    const int dx[] = {0, 1, 1, 0}, dy[] = {1, 1, 0, 0};
    unsigned tests = 0, errors = 0;
    for (unsigned f = 0; f < 8; f++)
        for (unsigned b = 0; b < 7; b++)
            for (unsigned r = 0; r < 8; r++) {
                const float border[] = {borders[b], 0, 0, 0};
                glTexParameterfv(GL_TEXTURE_2D, 0x1004, border);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, funcs[f]);
                glUniform1f(ref, references[r]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(initial), initial);
                CHECK(glGetError() == GL_NO_ERROR);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
                glFinish();
                CHECK(glGetError() == GL_NO_ERROR);
                const uint32_t *data =
                    glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(initial), GL_MAP_READ_BIT);
                CHECK(data);
                for (int y = 0; y < 6; y++)
                    for (int x = 0; x < 6; x++)
                        for (int c = 0; c < 4; c++) {
                            int sx = x - 1 + dx[c], sy = y - 1 + dy[c];
                            float depth = sx < 0 || sy < 0 || sx >= 4 || sy >= 4
                                              ? borders[b]
                                              : texels[sy * 4 + sx];
                            uint32_t want = compare(f, references[r], depth) ? 0x3f800000 : 0;
                            uint32_t got = data[1024 + (y * 6 + x) * 4 + c];
                            if (got != want) {
                                if (errors < 12)
                                    printf(
                                        "func %#x border %a ref %a (%d,%d).%d = %#x expected %#x\n",
                                        funcs[f], borders[b], references[r], x, y, c, got, want);
                                errors++;
                            }
                        }
                for (unsigned i = 0; i < 1024; i++)
                    if (data[i] != 0xa5a5a5a5 || data[1024 + 36 * 4 + i] != 0xa5a5a5a5)
                        errors++;
                CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
                tests++;
            }
    printf("%u shadow gather draws, all 8 functions, finite/infinite borders and references: %u "
           "errors, guards checked\n",
           tests, errors);
    glDeleteBuffers(1, &buffer);
    glDeleteTextures(1, &tex);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
    glDeleteProgram(prog);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm);
    close(fd);
    return errors ? 1 : 0;
}
