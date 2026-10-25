// Cross-context compute -> native fence -> compute, with guarded SSBOs.
// [rounds=200] [count=4097]; KSTB_RESET_PROBE runs the controlled CL stall
// helper at midpoint, before compute is submitted behind the stalled job.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                     \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define GUARD 4096

static GLuint program(const char *body) {
  char text[4096];
  snprintf(text, sizeof(text),
           "#version 310 es\nprecision highp int;layout(local_size_x=16) in;"
           "layout(std430,binding=0) readonly buffer Input{uint src[];};"
           "layout(std430,binding=1) writeonly buffer Output{uint dst[];};"
           "uniform uint count;uniform uint seed;void main(){"
           "uint i=gl_GlobalInvocationID.x;if(i>=count)return;%s}",
           body);
  const char *source = text;
  GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint ok;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "%s\n", log);
    exit(1);
  }
  GLuint p = glCreateProgram();
  glAttachShader(p, shader);
  glLinkProgram(p);
  glDeleteShader(shader);
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  CHECK(ok);
  return p;
}

static void dispatch(GLuint p, unsigned count, unsigned seed, GLuint input,
                     GLuint output) {
  glUseProgram(p);
  glUniform1ui(glGetUniformLocation(p, "count"), count);
  glUniform1ui(glGetUniformLocation(p, "seed"), seed);
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, input, GUARD, count * 4);
  glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, output, GUARD, count * 4);
  glDispatchCompute((count + 15) / 16, 1, 1);
  CHECK(glGetError() == GL_NO_ERROR);
}

int main(int argc, char **argv) {
  CHECK(argc <= 3);
  unsigned rounds = argc > 1 ? strtoul(argv[1], NULL, 0) : 200;
  unsigned count = argc > 2 ? strtoul(argv[2], NULL, 0) : 4097;
  CHECK(rounds >= 2 && rounds <= 100000 && count && count <= 65536);
  int render_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
  CHECK(render_fd >= 0);
  struct gbm_device *gbm = gbm_create_device(render_fd);
  CHECK(gbm);
  EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
  CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
  CHECK(eglBindAPI(EGL_OPENGL_ES_API));
  CHECK(strstr(eglQueryString(dpy, EGL_EXTENSIONS),
               "EGL_ANDROID_native_fence_sync"));
  PFNEGLCREATESYNCKHRPROC create =
      (void *)eglGetProcAddress("eglCreateSyncKHR");
  PFNEGLDESTROYSYNCKHRPROC destroy =
      (void *)eglGetProcAddress("eglDestroySyncKHR");
  PFNEGLWAITSYNCKHRPROC wait = (void *)eglGetProcAddress("eglWaitSyncKHR");
  PFNEGLDUPNATIVEFENCEFDANDROIDPROC export =
      (void *)eglGetProcAddress("eglDupNativeFenceFDANDROID");
  CHECK(create && destroy && wait && export);
  EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1,
                    EGL_NONE};
  EGLContext ctx[2];
  ctx[0] = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
  ctx[1] = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, ctx[0], attrs);
  CHECK(ctx[0] != EGL_NO_CONTEXT && ctx[1] != EGL_NO_CONTEXT);
  CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[0]));
  CHECK(strstr((const char *)glGetString(GL_RENDERER), "V3D"));
  printf("%s; %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
  GLuint producer = program("dst[i]=(i*1664525u)^seed;"),
         consumer = program("dst[i]=src[i]*3u+seed;");
  GLuint buffers[2];
  glGenBuffers(2, buffers);
  size_t bytes = GUARD + (size_t)count * 4 + GUARD;
  unsigned char *initial = malloc(bytes);
  CHECK(initial);
  memset(initial, 0xa5, bytes);
  for (unsigned k = 0; k < 2; k++) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[k]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, GL_DYNAMIC_COPY);
  }
  free(initial);
  glFinish(); // Setup only. No per-round producer CPU wait.
  for (unsigned n = 0; n < rounds; n++) {
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[n & 1]));
    FILE *reset = NULL;
    if (getenv("KSTB_RESET_PROBE") && n == rounds / 2) {
      reset = popen("python3 /opt/gpu-test/kstb-reset-probe", "r");
      CHECK(reset);
      char line[128];
      CHECK(fgets(line, sizeof(line), reset) && !strcmp(line, "submitted\n"));
      puts("controlled CL stall submitted; queueing compute before recovery");
      fflush(stdout);
    }
    unsigned seed = 0x9e3779b9u * (n + 1);
    dispatch(producer, count, seed, buffers[1], buffers[0]);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    EGLSyncKHR produced = create(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
    CHECK(produced != EGL_NO_SYNC_KHR);
    glFlush();
    int fd = export(dpy, produced);
    CHECK(fd >= 0 && (fcntl(fd, F_GETFD) & FD_CLOEXEC));
    if (reset) {
      struct pollfd pending = {.fd = fd, .events = POLLIN};
      CHECK(poll(&pending, 1, 0) == 0);
      struct timespec stamp;
      CHECK(clock_gettime(CLOCK_MONOTONIC, &stamp) == 0);
      printf("compute fence pending at uptime %.6f\n",
             stamp.tv_sec + stamp.tv_nsec * 1e-9);
      fflush(stdout);
    }
    CHECK(destroy(dpy, produced));
    CHECK(
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx[(n & 1) ^ 1]));
    EGLint fa[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};
    EGLSyncKHR imported = create(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, fa);
    CHECK(imported != EGL_NO_SYNC_KHR && wait(dpy, imported, 0));
    CHECK(destroy(dpy, imported)); // EGL owns imported fd.
    dispatch(consumer, count, seed, buffers[0], buffers[1]);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    EGLSyncKHR completed = create(dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
    CHECK(completed != EGL_NO_SYNC_KHR);
    glFlush();
    fd = export(dpy, completed);
    CHECK(fd >= 0);
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    CHECK(poll(&pfd, 1, 15000) == 1 && (pfd.revents & POLLIN));
    close(fd);
    CHECK(destroy(dpy, completed));
    if (reset)
      CHECK(pclose(reset) == 0);
    for (unsigned k = 0; k < 2; k++) {
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[k]);
      const unsigned char *mapping =
          glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
      CHECK(mapping);
      const uint32_t *values = (const uint32_t *)(mapping + GUARD);
      for (unsigned i = 0; i < count; i++) {
        uint32_t value = (i * 1664525u) ^ seed;
        if (k)
          value = value * 3u + seed;
        if (values[i] != value) {
          fprintf(stderr, "round %u buffer %u word %u: %#x expected %#x\n", n,
                  k, i, values[i], value);
          return 1;
        }
      }
      for (unsigned i = 0; i < GUARD; i++)
        CHECK(mapping[i] == 0xa5 && mapping[GUARD + count * 4 + i] == 0xa5);
      CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    }
    CHECK(glGetError() == GL_NO_ERROR);
    if ((n + 1) % 25 == 0 || n + 1 == rounds) {
      printf("%u/%u chains exact, %u words/buffer, guards checked\n", n + 1,
             rounds, count);
      fflush(stdout);
    }
  }
  glDeleteProgram(producer);
  glDeleteProgram(consumer);
  glDeleteBuffers(2, buffers);
  CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
  CHECK(eglDestroyContext(dpy, ctx[0]) && eglDestroyContext(dpy, ctx[1]) &&
        eglTerminate(dpy));
  gbm_device_destroy(gbm);
  close(render_fd);
  puts("PASS: compute native-fence chains, exact SSBOs and guards");
  return 0;
}
