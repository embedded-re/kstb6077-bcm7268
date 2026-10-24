// Native textureGather and texelFetch checked together against CPU texels.
// kstb-gather-test [width height component]; defaults to R32UI, 4x4, red.
// KSTB_GATHER_RGBA=1 or KSTB_GATHER_F16=1 selects RGBA32UI or RGBA16F.
// KSTB_GATHER_COMPUTE=1 selects compute instead of a fragment shader.
// KSTB_GATHER_SWIZZLE=1 checks B/A/zero/one texture swizzles.
// KSTB_GATHER_BORDER=1 checks floating-point border colors (RGBA16F only).
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
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static GLuint shader(GLenum type, const char *source) {
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

static const char *vs = "#version 310 es\nvoid main(){ "
                        "gl_Position=vec4(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0,0,1); }";
int main(int argc, char **argv) {
    CHECK(argc == 1 || argc == 4);
    int w = argc == 4 ? atoi(argv[1]) : 4, h = argc == 4 ? atoi(argv[2]) : 4,
        component = argc == 4 ? atoi(argv[3]) : 0;
    CHECK(w > 0 && h > 0 && w <= 256 && h <= 256 && component >= 0 && component < 4);
    int compute = getenv("KSTB_GATHER_COMPUTE") != NULL;
    int swizzle = getenv("KSTB_GATHER_SWIZZLE") != NULL;
    int border = getenv("KSTB_GATHER_BORDER") != NULL;
    int f16 = getenv("KSTB_GATHER_F16") != NULL;
    int rgba = getenv("KSTB_GATHER_RGBA") != NULL || f16;
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    printf("%s\n", glGetString(GL_VERSION));

    int tw = w + 2, th = h + 2;
    uint32_t *src = malloc((size_t)w * h * (rgba ? 4 : 1) * 4);
    CHECK(src);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < (rgba ? 4 : 1); c++)
                src[((size_t)y * w + x) * (rgba ? 4 : 1) + c] = 1000 + ((y * w + x) % 64) * 16 + c;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, f16 ? GL_RGBA16F : rgba ? GL_RGBA32UI : GL_R32UI, w, h);
    float *floats = NULL;
    if (f16) {
        floats = malloc((size_t)w * h * 16);
        CHECK(floats);
        for (size_t i = 0; i < (size_t)w * h * 4; i++)
            floats[i] = src[i];
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                    f16    ? GL_RGBA
                    : rgba ? GL_RGBA_INTEGER
                           : GL_RED_INTEGER,
                    f16 ? GL_FLOAT : GL_UNSIGNED_INT, f16 ? (void *)floats : (void *)src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (swizzle) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_ALPHA);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ZERO);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
    }
    if (border) {
        CHECK(f16);
        const GLfloat bc[4] = {200, 201, 202, 203};
        glTexParameterfv(GL_TEXTURE_2D, 0x1004, bc);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812d);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812d);
    }
    char fs[4096];
    snprintf(fs, sizeof(fs),
             "#version 310 es\nprecision highp float; precision highp int;"
             "uniform highp %ssampler2D tex; layout(std430,binding=0) buffer D { uvec4 result[]; };"
             "%s void main(){ ivec2 p=%s; if(any(greaterThanEqual(p,ivec2(%d,%d))))return;"
             "int idx=2*(p.y*%d+p.x);"
             "result[idx]=uvec4(textureGather(tex,vec2(p)/vec2(%d,%d),%d));"
             "result[idx+1]=uvec4(texelFetch(tex,clamp(p,ivec2(0),ivec2(%d,%d)),0)); %s }",
             f16 ? "" : "u", compute ? "layout(local_size_x=8) in;" : "out vec4 color;",
             compute ? "ivec2(gl_GlobalInvocationID.xy)" : "ivec2(gl_FragCoord.xy)", tw, th, tw, w,
             h, component, w - 1, h - 1, compute ? "" : "color=vec4(1);");
    GLuint prog = glCreateProgram();
    if (compute)
        glAttachShader(prog, shader(GL_COMPUTE_SHADER, fs));
    else {
        glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
        glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    }
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    CHECK(ok);
    glUseProgram(prog);
    size_t n = (size_t)tw * th, bytes = (n * 8 + 2048) * 4;
    uint32_t *initial = malloc(bytes);
    CHECK(initial);
    memset(initial, 0xa5, bytes);
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, GL_DYNAMIC_COPY);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffer, 4096, n * 32);
    GLuint rb, fbo;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, tw, th);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, tw, th);
    CHECK(glGetError() == GL_NO_ERROR);
    if (compute)
        glDispatchCompute((tw + 7) / 8, th, 1);
    else
        glDrawArrays(GL_TRIANGLES, 0, 3);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);
    const uint32_t *data = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    CHECK(data);
    size_t errors = 0;
    const int dx[4] = {0, 1, 1, 0}, dy[4] = {1, 1, 0, 0};
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++)
            for (int c = 0; c < 4; c++) {
                int sx = x - 1 + dx[c], sy = y - 1 + dy[c];
                int outside = sx < 0 || sy < 0 || sx >= w || sy >= h;
                if (sx < 0)
                    sx = 0;
                if (sy < 0)
                    sy = 0;
                if (sx >= w)
                    sx = w - 1;
                if (sy >= h)
                    sy = h - 1;
                int selected = swizzle ? (int[]){2, 3, 4, 5}[component] : component;
                uint32_t want = selected >= 4   ? (selected == 5)
                                : rgba          ? src[((size_t)sy * w + sx) * 4 + selected]
                                : selected == 0 ? src[(size_t)sy * w + sx]
                                : selected == 3 ? 1
                                                : 0;
                if (border && outside)
                    want = selected >= 4 ? (selected == 5) : 200 + selected;
                uint32_t got = data[1024 + ((size_t)y * tw + x) * 8 + c];
                if (got != want) {
                    if (errors < 12)
                        printf("(%d,%d).%d: %#x expected %#x\n", x, y, c, got, want);
                    errors++;
                }
            }
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++)
            for (int c = 0; c < 4; c++) {
                int sx = x < w ? x : w - 1, sy = y < h ? y : h - 1;
                int selected = swizzle ? (int[]){2, 3, 4, 5}[c] : c;
                uint32_t want = selected >= 4   ? (selected == 5)
                                : rgba          ? src[((size_t)sy * w + sx) * 4 + selected]
                                : selected == 0 ? src[(size_t)sy * w + sx]
                                : selected == 3 ? 1
                                                : 0;
                uint32_t got = data[1024 + ((size_t)y * tw + x) * 8 + 4 + c];
                if (got != want) {
                    if (errors < 12)
                        printf("fetch (%d,%d).%d: %#x expected %#x\n", x, y, c, got, want);
                    errors++;
                }
            }
    for (size_t i = 0; i < 1024; i++)
        if (data[i] != 0xa5a5a5a5 || data[1024 + n * 8 + i] != 0xa5a5a5a5)
            errors++;
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    printf("gather + fetch %s %s %dx%d component %d: %zu errors, guards checked\n",
           compute ? "compute" : "fragment",
           f16    ? "RGBA16F"
           : rgba ? "RGBA32UI"
                  : "R32UI",
           w, h, component, errors);
    CHECK(errors == 0);
    free(src);
    free(floats);
    free(initial);
    glDeleteBuffers(1, &buffer);
    glDeleteTextures(1, &tex);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
    glDeleteProgram(prog);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
