// Query capabilities without compiling shaders or submitting GPU work.
// kstb-gl-caps 2|3|3.1 (3.1 requires the temporary Mesa version override).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s failed (EGL %#x)\n", #x, eglGetError()); exit(1); } } while (0)

static void limit(const char *name, GLenum token)
{
    GLint n = 0;
    glGetIntegerv(token, &n);
    GLenum err = glGetError();
    if (err == GL_NO_ERROR)
        printf("%s=%d\n", name, n);
    else
        printf("%s=query-error-%#x\n", name, err);
}

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "2") && strcmp(argv[1], "3") && strcmp(argv[1], "3.1"))) {
        fprintf(stderr, "usage: %s 2|3|3.1\n", argv[0]);
        return 1;
    }
    int major = atoi(argv[1]), minor = !strcmp(argv[1], "3.1");
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[] = { EGL_CONTEXT_MAJOR_VERSION, major,
        EGL_CONTEXT_MINOR_VERSION, minor, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
    CHECK(ctx != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    printf("requested=%s\nGL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\nGL_SHADING_LANGUAGE_VERSION=%s\n",
           argv[1], glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("GL_EXTENSIONS=%s\n", glGetString(GL_EXTENSIONS));
    printf("EGL_EXTENSIONS=%s\n", eglQueryString(dpy, EGL_EXTENSIONS));
#define LIMIT(token) limit(#token, token)
    LIMIT(GL_MAX_TEXTURE_SIZE);
    LIMIT(GL_MAX_3D_TEXTURE_SIZE);
    LIMIT(GL_MAX_ARRAY_TEXTURE_LAYERS);
    LIMIT(GL_MAX_TEXTURE_IMAGE_UNITS);
    LIMIT(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS);
    LIMIT(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    LIMIT(GL_MAX_DRAW_BUFFERS);
    LIMIT(GL_MAX_COLOR_ATTACHMENTS);
    LIMIT(GL_MAX_SAMPLES);
    LIMIT(GL_MAX_UNIFORM_BUFFER_BINDINGS);
    if (major == 3 && minor == 1) {
        LIMIT(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS);
        LIMIT(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS);
        LIMIT(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS);
        LIMIT(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS);
        LIMIT(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT);
        LIMIT(GL_MAX_VERTEX_IMAGE_UNIFORMS);
        LIMIT(GL_MAX_FRAGMENT_IMAGE_UNIFORMS);
        LIMIT(GL_MAX_COMPUTE_IMAGE_UNIFORMS);
        LIMIT(GL_MAX_IMAGE_UNITS);
        LIMIT(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
        LIMIT(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE);
        LIMIT(GL_MAX_VERTEX_ATOMIC_COUNTERS);
        LIMIT(GL_MAX_FRAGMENT_ATOMIC_COUNTERS);
        LIMIT(GL_MAX_COMPUTE_ATOMIC_COUNTERS);
    }
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx));
    CHECK(eglTerminate(dpy));
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
