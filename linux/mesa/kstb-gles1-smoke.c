/* Verify GLES1 context creation, clear, and readback on the V3D GBM device. */
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int samples = argc > 1 ? atoi(argv[1]) : 0;
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) { perror("open render node"); return 1; }
    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) { fputs("gbm_create_device failed\n", stderr); return 1; }
    EGLDisplay display = eglGetDisplay((EGLNativeDisplayType)gbm);
    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "eglInitialize: 0x%x\n", eglGetError()); return 1;
    }
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_SAMPLES, samples, EGL_NONE
    };
    EGLConfig config;
    EGLint count, visual;
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || count != 1 ||
        !eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &visual)) {
        fprintf(stderr, "eglChooseConfig: 0x%x\n", eglGetError()); return 1;
    }
    struct gbm_surface *window = gbm_surface_create(gbm, 128, 128,
                                                     (uint32_t)visual,
                                                     GBM_BO_USE_RENDERING);
    if (!window) { fputs("gbm_surface_create failed\n", stderr); return 1; }
    EGLSurface surface = eglCreateWindowSurface(display, config,
                                                 (EGLNativeWindowType)window, NULL);
    const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
    if (!eglBindAPI(EGL_OPENGL_ES_API) || surface == EGL_NO_SURFACE) {
        fprintf(stderr, "eglCreateWindowSurface: 0x%x\n", eglGetError()); return 1;
    }
    EGLContext ctx = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, ctx)) {
        fprintf(stderr, "eglMakeCurrent GLES1: 0x%x\n", eglGetError()); return 1;
    }
    glViewport(0, 0, 128, 128);
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLubyte pixel[4] = {0};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    printf("GLES1 clear pixel: %u %u %u %u (GL error 0x%x)\n",
           pixel[0], pixel[1], pixel[2], pixel[3], error);
    int ok = error == GL_NO_ERROR && pixel[0] == 51 && pixel[1] == 102 &&
             pixel[2] == 153 && pixel[3] == 255;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 16, 16);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("GLES1 %dx MSAA scissor pixel: %u %u %u %u\n", samples,
           pixel[0], pixel[1], pixel[2], pixel[3]);
    ok = ok && pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 &&
         pixel[3] == 255;
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("GLES1 %dx MSAA outside pixel: %u %u %u %u\n", samples,
           pixel[0], pixel[1], pixel[2], pixel[3]);
    ok = ok && pixel[0] == 51 && pixel[1] == 102 && pixel[2] == 153 &&
         pixel[3] == 255 && glGetError() == GL_NO_ERROR;
    static const struct {
        int x, y, width, height;
        GLubyte color[4];
    } clears[] = {
        {3, 5, 50, 42, {23, 180, 19, 255}},
        {22, 0, 39, 31, {210, 248, 64, 24}},
        {0, 35, 60, 29, {6, 235, 166, 228}},
        {13, 17, 42, 32, {186, 16, 17, 63}},
        {31, 2, 20, 58, {101, 89, 215, 200}},
        {1, 1, 27, 39, {244, 200, 31, 255}},
    };
    GLubyte expected[128][128][4];
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            for (int c = 0; c < 4; c++)
                expected[y][x][c] = pixel[c];
    /* Begin from a known state, then switch contexts just as EGL CTS does. */
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (argc > 3) {
        GLubyte check[128][128][4];
        glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, check);
        int bad = 0;
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
                for (int c = 0; c < 4; c++)
                    bad += check[y][x][c] != (c == 3 ? 255 : 0);
        printf("after full black clear: %d mismatched components\n", bad);
    }
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++) {
            expected[y][x][0] = expected[y][x][1] = expected[y][x][2] = 0;
            expected[y][x][3] = 255;
        }
    for (int i = 0; i < 6; i++) {
        if (!(i & 1)) {
            glFinish();
            if (!eglMakeCurrent(display, surface, surface, ctx)) {
                fprintf(stderr, "repeated eglMakeCurrent: 0x%x\n", eglGetError());
                return 1;
            }
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor(clears[i].x, clears[i].y, clears[i].width, clears[i].height);
        glClearColor(clears[i].color[0] / 255.0f, clears[i].color[1] / 255.0f,
                     clears[i].color[2] / 255.0f, clears[i].color[3] / 255.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (argc > 2)
            glFinish();
        for (int y = clears[i].y; y < clears[i].y + clears[i].height; y++)
            for (int x = clears[i].x; x < clears[i].x + clears[i].width; x++)
                for (int c = 0; c < 4; c++)
                    expected[y][x][c] = clears[i].color[c];
        if (argc > 3) {
            GLubyte check[128][128][4];
            glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, check);
            int bad = 0;
            for (int y = 0; y < 128; y++)
                for (int x = 0; x < 128; x++)
                    for (int c = 0; c < 4; c++) {
                        int delta = (int)check[y][x][c] - expected[y][x][c];
                        if (delta < -2 || delta > 2) {
                            if (!bad)
                                printf("clear %d first mismatch (%d,%d) c%d: got %u expected %u\n",
                                       i, x, y, c, check[y][x][c], expected[y][x][c]);
                            bad++;
                        }
                    }
            printf("after clear %d: %d mismatched components\n", i, bad);
            if (i == 0) {
                for (int y = 5; y <= 53; y += 16) {
                    printf("row %d color blocks:", y);
                    for (int x = 0; x < 64; x += 4)
                        printf(" %d:%u", x, check[y][x][0]);
                    putchar('\n');
                }
            }
        }
    }
    GLubyte framebuffer[128][128][4];
    glReadPixels(0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);
    int mismatches = 0;
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            for (int c = 0; c < 4; c++) {
                int delta = (int)framebuffer[y][x][c] - expected[y][x][c];
                if (delta < -2 || delta > 2) {
                    if (!mismatches)
                        printf("first mismatch at (%d,%d) channel %d: got %u expected %u\n",
                               x, y, c, framebuffer[y][x][c], expected[y][x][c]);
                    mismatches++;
                }
            }
    printf("GLES1 %dx MSAA six clears: %d mismatched components\n", samples,
           mismatches);
    ok = ok && mismatches == 0 && glGetError() == GL_NO_ERROR;
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, ctx);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    gbm_surface_destroy(window);
    gbm_device_destroy(gbm);
    close(fd);
    return ok ? 0 : 1;
}
