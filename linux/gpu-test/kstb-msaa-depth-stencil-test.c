// Focused 4x MSAA GBM window depth/stencil draw and readback checks.
// gcc -O2 -o kstb-msaa-depth-stencil-test kstb-msaa-depth-stencil-test.c -lEGL -lGLESv2 -lgbm
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s (EGL 0x%x, GL 0x%x)\n", \
            __LINE__, #x, eglGetError(), glGetError()); exit(1); \
} } while (0)

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
        fprintf(stderr, "shader compile: %s\n", log);
        exit(1);
    }
    return sh;
}

static void draw(GLuint program, GLfloat depth, GLfloat r, GLfloat g, GLfloat b)
{
    glUniform1f(glGetUniformLocation(program, "z"), depth);
    glUniform3f(glGetUniformLocation(program, "rgb"), r, g, b);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    CHECK(glGetError() == GL_NO_ERROR);
}

static int pixel(const char *label, int r, int g, int b)
{
    GLubyte px[4];
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(glGetError() == GL_NO_ERROR);
    printf("%s: %u %u %u %u (expected %d %d %d)\n",
           label, px[0], px[1], px[2], px[3], r, g, b);
    return abs((int)px[0] - r) > 2 || abs((int)px[1] - g) > 2 ||
           abs((int)px[2] - b) > 2;
}

int main(void)
{
    int fd = open("/dev/dri/renderD128", O_RDWR);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
                       EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                       EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                       EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES, 4, EGL_NONE };
    EGLConfig config;
    EGLint count, visual, samples, depth_bits, stencil_bits;
    CHECK(eglChooseConfig(dpy, attrs, &config, 1, &count) && count == 1);
    CHECK(eglGetConfigAttrib(dpy, config, EGL_NATIVE_VISUAL_ID, &visual));
    CHECK(eglGetConfigAttrib(dpy, config, EGL_SAMPLES, &samples));
    CHECK(eglGetConfigAttrib(dpy, config, EGL_DEPTH_SIZE, &depth_bits));
    CHECK(eglGetConfigAttrib(dpy, config, EGL_STENCIL_SIZE, &stencil_bits));
    printf("config: samples %d, depth %d, stencil %d, visual 0x%x\n",
           samples, depth_bits, stencil_bits, visual);
    struct gbm_surface *window = gbm_surface_create(gbm, 64, 64, visual, GBM_BO_USE_RENDERING);
    CHECK(window);
    EGLSurface surface = eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)window, NULL);
    CHECK(surface != EGL_NO_SURFACE);
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, surface, surface, ctx));
    CHECK(glGetError() == GL_NO_ERROR);

    const char *vs_source = "#version 300 es\n"
        "layout(location=0) in vec2 xy; uniform float z;\n"
        "void main() { gl_Position = vec4(xy, z, 1.0); }\n";
    const char *fs_source = "#version 300 es\nprecision mediump float;\n"
        "uniform vec3 rgb; out vec4 color;\n"
        "void main() { color = vec4(rgb, 1.0); }\n";
    GLuint vs = shader(GL_VERTEX_SHADER, vs_source);
    GLuint fs = shader(GL_FRAGMENT_SHADER, fs_source);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked);
    glUseProgram(program);
    GLuint vao, vbo;
    GLfloat triangle[] = { -1.f, -1.f, 3.f, -1.f, -1.f, 3.f };
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    glViewport(0, 0, 64, 64);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 1);
    glClearDepthf(1);
    glClearStencil(0);

    int failures = 0;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    draw(program, 0.0f, 1, 0, 0);  // window depth 0.5
    draw(program, 0.4f, 0, 1, 0);  // occluded
    failures += pixel("depth near survives", 255, 0, 0);
    draw(program, -0.4f, 0, 0, 1); // nearer
    failures += pixel("depth nearer replaces", 0, 0, 255);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    draw(program, 0, 1, 0, 0);
    glStencilFunc(GL_EQUAL, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    draw(program, 0, 0, 1, 0);
    failures += pixel("stencil match", 0, 255, 0);
    glStencilFunc(GL_EQUAL, 2, 0xff);
    draw(program, 0, 0, 0, 1);
    failures += pixel("stencil reject", 0, 255, 0);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    draw(program, 0, 1, 0, 0);
    glStencilFunc(GL_EQUAL, 1, 0xff);
    draw(program, 0.4f, 0, 1, 0); // stencil passes, depth rejects
    failures += pixel("depth and stencil", 255, 0, 0);
    CHECK(glGetError() == GL_NO_ERROR);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surface);
    gbm_surface_destroy(window);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    printf("%d pixel checks failed\n", failures);
    return failures ? 2 : 0;
}
