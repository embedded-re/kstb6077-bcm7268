// Native fence export/import and server-wait ordering across shared contexts.
// gcc -O2 -o kstb-native-fence-test kstb-native-fence-test.c -lEGL -lGLESv2 -lgbm
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s (EGL 0x%x, GL 0x%x)\n", \
            __LINE__, #x, eglGetError(), glGetError()); exit(1); \
} } while (0)

int main(void)
{
    int render_fd = open("/dev/dri/renderD128", O_RDWR);
    CHECK(render_fd >= 0);
    struct gbm_device *gbm = gbm_create_device(render_fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(strstr(eglQueryString(dpy, EGL_EXTENSIONS),
                 "EGL_ANDROID_native_fence_sync"));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    PFNEGLCREATESYNCKHRPROC create = (void *)eglGetProcAddress("eglCreateSyncKHR");
    PFNEGLDESTROYSYNCKHRPROC destroy = (void *)eglGetProcAddress("eglDestroySyncKHR");
    PFNEGLWAITSYNCKHRPROC wait = (void *)eglGetProcAddress("eglWaitSyncKHR");
    PFNEGLCLIENTWAITSYNCKHRPROC client_wait =
        (void *)eglGetProcAddress("eglClientWaitSyncKHR");
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC export =
        (void *)eglGetProcAddress("eglDupNativeFenceFDANDROID");
    CHECK(create && destroy && wait && client_wait && export);

    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx[2];
    ctx[0] = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    ctx[1] = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, ctx[0], ca);
    CHECK(ctx[0] != EGL_NO_CONTEXT && ctx[1] != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[0]));
    GLuint tex, fb[2];
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    for (int i = 0; i < 2; i++) {
        CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[i]));
        glGenFramebuffers(1, &fb[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, fb[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    }

    for (int n = 0; n < 100; n++) {
        CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[0]));
        glClearColor((n & 1) ? 1 : 0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        EGLSyncKHR produced = create(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
        CHECK(produced != EGL_NO_SYNC_KHR);
        glFlush();
        int fd = export(dpy, produced);
        CHECK(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC));
        CHECK(destroy(dpy, produced));

        CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[1]));
        EGLint fa[] = { EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE };
        EGLSyncKHR imported = create(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, fa);
        CHECK(imported != EGL_NO_SYNC_KHR); // EGL now owns fd.
        CHECK(wait(dpy, imported, 0));
        CHECK(destroy(dpy, imported));

        // Export with no intervening draw: the imported dependency must survive.
        EGLSyncKHR chained = create(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
        CHECK(chained != EGL_NO_SYNC_KHR);
        glFlush();
        fd = export(dpy, chained);
        CHECK(fd >= 0);
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        CHECK(poll(&pfd, 1, 5000) == 1 && (pfd.revents & POLLIN));
        CHECK(client_wait(dpy, chained, 0, 5000000000ULL) == EGL_CONDITION_SATISFIED_KHR);
        close(fd);
        CHECK(destroy(dpy, chained));
        unsigned char px[4];
        glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(glGetError() == GL_NO_ERROR);
        CHECK(px[0] == ((n & 1) ? 255 : 0) && px[1] == 0 && px[2] == 255 && px[3] == 255);
    }
    for (int i = 0; i < 2; i++) {
        CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[i]));
        glDeleteFramebuffers(1, &fb[i]);
    }
    glDeleteTextures(1, &tex);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    for (int i = 0; i < 2; i++) {
        CHECK(eglDestroyContext(dpy, ctx[i]));
    }
    CHECK(eglTerminate(dpy));
    gbm_device_destroy(gbm);
    close(render_fd);
    puts("OK: 100 native fence export/import/server-wait chains, exact pixels");
    return 0;
}
