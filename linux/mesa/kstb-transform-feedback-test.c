// Minimal V3D transform feedback reproducer. Run with the isolated Mesa driver.
// gcc -O2 -o kstb-transform-feedback-test kstb-transform-feedback-test.c -lEGL -lGLESv2 -lgbm
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s (EGL 0x%x, GL 0x%x)\n", \
            __LINE__, #x, eglGetError(), glGetError()); exit(1); \
} } while (0)

static GLuint shader(GLenum type, const char *source)
{
    GLuint sh = glCreateShader(type);
    CHECK(sh);
    glShaderSource(sh, 1, &source, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "shader compile: %s\n", log);
        exit(1);
    }
    return sh;
}

int main(int argc, char **argv)
{
    char *end = NULL;
    long instances = argc == 2 ? strtol(argv[1], &end, 10) : 1;
    if (argc > 2 || instances < 1 || instances > 1024 ||
        (argc == 2 && (!argv[1][0] || *end))) {
        fprintf(stderr, "usage: %s [instances: 1..1024]\n", argv[0]);
        return 1;
    }
    int fd = open("/dev/dri/renderD128", O_RDWR);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint context_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, context_attrs);
    CHECK(ctx != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));

    const char *vs_source = "#version 300 es\nlayout(location=0) in vec4 pos;\n"
                            "void main() { gl_Position = pos; }\n";
    const char *fs_source = "#version 300 es\nprecision mediump float;\n"
                            "out vec4 color; void main() { color = vec4(1.0); }\n";
    GLuint vs = shader(GL_VERTEX_SHADER, vs_source);
    GLuint fs = shader(GL_FRAGMENT_SHADER, fs_source);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    const char *varying = "gl_Position";
    glTransformFeedbackVaryings(program, 1, &varying, GL_SEPARATE_ATTRIBS);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        fprintf(stderr, "program link: %s\n", log);
        return 1;
    }
    glUseProgram(program);

    GLuint color, fb, vao, input, output, query;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 16, 16);
    const GLfloat positions[3][4] = {
        {-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.0f, 1.0f}, {0.5f, -0.5f, 0.0f, 1.0f}
    };
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &input);
    glBindBuffer(GL_ARRAY_BUFFER, input);
    glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(positions[0]), 0);
    glEnableVertexAttribArray(0);
    glGenBuffers(1, &output);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, output);
    size_t output_size = sizeof(positions) * instances;
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, output_size + 64, NULL, GL_DYNAMIC_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output);
    glGenQueries(1, &query);
    CHECK(glGetError() == GL_NO_ERROR);

    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArraysInstanced(GL_POINTS, 0, 3, (GLsizei)instances);
    glEndTransformFeedback();
    glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    CHECK(glGetError() == GL_NO_ERROR);
    glFinish();
    GLuint count = 0;
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &count);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, output);
    const GLfloat *mapped = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                                               output_size, GL_MAP_READ_BIT);
    CHECK(mapped);
    int mismatch = count != (GLuint)(3 * instances);
    printf("primitives written: %u (expected %ld)\n", count, 3 * instances);
    for (int i = 0; i < 3 * instances; i++) {
        printf("vertex %d: %g %g %g %g\n", i,
               mapped[4*i], mapped[4*i+1], mapped[4*i+2], mapped[4*i+3]);
        for (int j = 0; j < 4; j++)
            mismatch |= mapped[4*i+j] != positions[i % 3][j];
    }
    CHECK(glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER));
    CHECK(glGetError() == GL_NO_ERROR);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    gbm_device_destroy(gbm);
    close(fd);
    return mismatch ? 2 : 0;
}
