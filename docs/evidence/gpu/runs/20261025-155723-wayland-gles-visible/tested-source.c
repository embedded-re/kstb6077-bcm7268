// Native GPU Wayland client: animated GLES2 triangle and checked colour markers.
// kstb-wayland-gles-test [seconds]; compositor may resize or fullscreen it.
#define _POSIX_C_SOURCE 200809L
#include "xdg-shell-client-protocol.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s (EGL %#x)\n", __LINE__, #x, eglGetError());          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static volatile sig_atomic_t running = 1;
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct xdg_wm_base *wm;
static struct wl_egl_window *window;
static int width = 800, height = 600, configured, ready;
static unsigned configures, checks;
static double now(clockid_t clock) {
    struct timespec t;
    CHECK(!clock_gettime(clock, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static void stop(int sig) {
    (void)sig;
    running = 0;
}
static void ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_listener = {.ping = ping};
static void global(void *data, struct wl_registry *reg, uint32_t name, const char *interface,
                   uint32_t version) {
    (void)data;
    if (!strcmp(interface, "wl_compositor"))
        compositor =
            wl_registry_bind(reg, name, &wl_compositor_interface, version < 4 ? version : 4);
    if (!strcmp(interface, "xdg_wm_base")) {
        wm = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm, &wm_listener, NULL);
    }
}
static void removed(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data;
    (void)reg;
    (void)name;
}
static const struct wl_registry_listener registry_listener = {global, removed};
static void surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    configured = 1;
    configures++;
    printf("configure %u %dx%d\n", serial, width, height);
    fflush(stdout);
}
static const struct xdg_surface_listener surface_listener = {surface_configure};
static void top_configure(void *data, struct xdg_toplevel *top, int32_t w, int32_t h,
                          struct wl_array *states) {
    (void)data;
    (void)top;
    (void)states;
    if (w > 0 && h > 0) {
        width = w;
        height = h;
        if (window)
            wl_egl_window_resize(window, w, h, 0, 0);
    }
}
static void top_close(void *data, struct xdg_toplevel *top) {
    (void)data;
    (void)top;
    running = 0;
}
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure,
                                                          .close = top_close};
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    (void)data;
    (void)time;
    wl_callback_destroy(cb);
    ready = 1;
}
static const struct wl_callback_listener frame_listener = {frame_done};
static GLuint shader(GLenum kind, const char *source) {
    GLuint sh = glCreateShader(kind);
    glShaderSource(sh, 1, &source, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    CHECK(ok);
    return sh;
}
static void marker(int x, int y, float r, float g, float b) {
    glScissor(x, y, 24, 24);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
static void check_marker(int x, int y, int r, int g, int b) {
    unsigned char pixel[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == 255);
    checks++;
}
int main(int argc, char **argv) {
    CHECK(argc <= 2);
    char *end;
    double seconds = argc == 2 ? strtod(argv[1], &end) : 40;
    CHECK((argc < 2 || (*argv[1] && !*end)) && seconds >= 2 && seconds <= 120);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    display = wl_display_connect(NULL);
    CHECK(display);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    CHECK(wl_display_roundtrip(display) >= 0 && compositor && wm);
    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    CHECK(surface);
    struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(wm, surface);
    xdg_surface_add_listener(xdg, &surface_listener, NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(top, &top_listener, NULL);
    xdg_toplevel_set_app_id(top, "kstb-wayland-gles-test");
    xdg_toplevel_set_title(top, "V3D GPU Wayland - preparing");
    xdg_toplevel_set_min_size(top, 320, 240);
    wl_surface_commit(surface);
    while (!configured && running)
        CHECK(wl_display_dispatch(display) >= 0);
    window = wl_egl_window_create(surface, width, height);
    CHECK(window);
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
        (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    CHECK(get_display);
    EGLDisplay egl = get_display(EGL_PLATFORM_WAYLAND_EXT, display, NULL);
    CHECK(egl != EGL_NO_DISPLAY && eglInitialize(egl, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[] = {EGL_SURFACE_TYPE,
                            EGL_WINDOW_BIT,
                            EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_ALPHA_SIZE,
                            8,
                            EGL_NONE};
    EGLConfig config;
    EGLint count;
    CHECK(eglChooseConfig(egl, attrs, &config, 1, &count) && count == 1);
    const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(egl, config, EGL_NO_CONTEXT, ca);
    CHECK(context != EGL_NO_CONTEXT);
    EGLSurface target = eglCreateWindowSurface(egl, config, (EGLNativeWindowType)window, NULL);
    CHECK(target != EGL_NO_SURFACE);
    CHECK(eglMakeCurrent(egl, target, target, context));
    CHECK(eglSwapInterval(egl, 1));
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "V3D 3.3"));
    printf("GL renderer: %s; GL version: %s\n", renderer, glGetString(GL_VERSION));
    fflush(stdout);
    GLuint vs = shader(GL_VERTEX_SHADER, "attribute vec2 pos;attribute vec3 color;uniform vec2 "
                                         "rot;uniform float aspect;varying vec3 c;void main(){vec2 "
                                         "p=vec2(rot.x*pos.x-rot.y*pos.y,rot.y*pos.x+rot.x*pos.y);"
                                         "gl_Position=vec4(p.x*aspect,p.y,0,1);c=color;}");
    GLuint fs =
        shader(GL_FRAGMENT_SHADER,
               "precision mediump float;varying vec3 c;void main(){gl_FragColor=vec4(c,1);}");
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "pos");
    glBindAttribLocation(program, 1, "color");
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    CHECK(ok);
    glUseProgram(program);
    static const GLfloat vertices[] = {0, .75, 1,  .15, .1,  -.7, -.5, .1,
                                       1, .2,  .7, -.5, .15, .25, 1};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), vertices);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), vertices + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDisable(GL_DITHER);
    GLint rotation = glGetUniformLocation(program, "rot"),
          aspect = glGetUniformLocation(program, "aspect");
    double start = now(CLOCK_MONOTONIC), cpu_start = now(CLOCK_PROCESS_CPUTIME_ID), report = start;
    unsigned frames = 0, last_frames = 0;
    while (running && now(CLOCK_MONOTONIC) - start < seconds) {
        double t = now(CLOCK_MONOTONIC) - start;
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(.025, .04, .09, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform2f(rotation, cosf(t * .8), sinf(t * .8));
        glUniform1f(aspect, (float)height / width);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_SCISSOR_TEST);
        glScissor((int)((sin(t * .6) * .5 + .5) * (width - width / 6)), height / 10, width / 6,
                  height / 12);
        glClearColor(.9, .6, .05, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        marker(0, 0, 1, 0, 0);
        marker(width - 24, 0, 0, 1, 0);
        marker(0, height - 24, 0, 0, 1);
        glDisable(GL_SCISSOR_TEST);
        if (frames % 60 == 0) {
            check_marker(8, 8, 255, 0, 0);
            check_marker(width - 8, 8, 0, 255, 0);
            check_marker(8, height - 8, 0, 0, 255);
        }
        CHECK(glGetError() == GL_NO_ERROR);
        struct wl_callback *cb = wl_surface_frame(surface);
        CHECK(cb);
        wl_callback_add_listener(cb, &frame_listener, NULL);
        ready = 0;
        CHECK(eglSwapBuffers(egl, target));
        frames++;
        while (!ready && running) {
            int rc = wl_display_dispatch(display);
            if (rc < 0 && errno == EINTR)
                continue;
            CHECK(rc >= 0);
        }
        double stamp = now(CLOCK_MONOTONIC);
        if (stamp - report >= 1) {
            double fps = (frames - last_frames) / (stamp - report);
            char title[160];
            snprintf(title, sizeof(title), "V3D GPU Wayland | %dx%d | %.1f FPS | rotating triangle",
                     width, height, fps);
            xdg_toplevel_set_title(top, title);
            printf("FRAME %u size %dx%d fps %.3f cpu_ms %.3f\n", frames, width, height, fps,
                   (now(CLOCK_PROCESS_CPUTIME_ID) - cpu_start) * 1000);
            fflush(stdout);
            report = stamp;
            last_frames = frames;
        }
    }
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);
    printf("PASS: frames %u elapsed %.6f fps %.3f process_cpu_ms %.3f configures %u marker_checks "
           "%u\n",
           frames, now(CLOCK_MONOTONIC) - start, frames / (now(CLOCK_MONOTONIC) - start),
           (now(CLOCK_PROCESS_CPUTIME_ID) - cpu_start) * 1000, configures, checks);
    fflush(stdout);
    glDeleteProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    eglMakeCurrent(egl, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl, target);
    eglDestroyContext(egl, context);
    eglTerminate(egl);
    wl_egl_window_destroy(window);
    xdg_toplevel_destroy(top);
    xdg_surface_destroy(xdg);
    wl_surface_destroy(surface);
    xdg_wm_base_destroy(wm);
    wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);
    return 0;
}
