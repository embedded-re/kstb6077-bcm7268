// Bounded compute-path timing: groups loops rounds dispatches [empty|store].
// Each workgroup has 128 invocations. Round zero is cold. No CPU wait between
// dispatches; every measured round ends in glFinish and verifies output/guards.
// Per-file DRM counters include kernel queue time, not isolated shader time.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define GUARD 4096
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
struct stats {
    uint64_t render_ns, clean_ns, render_jobs, clean_jobs, bin_jobs;
};
static double now(void) {
    struct timespec t;
    CHECK(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static unsigned number(const char *s, unsigned max, int zero) {
    char *end;
    unsigned long n = strtoul(s, &end, 10);
    CHECK(*s && !*end && n <= max && (zero || n));
    return n;
}
static struct stats snapshot(int fd) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    FILE *f = fopen(path, "r");
    CHECK(f);
    struct stats s = {0};
    unsigned found = 0;
    while (fgets(line, sizeof(line), f)) {
        uint64_t value;
        if (sscanf(line, "drm-engine-render: %" SCNu64, &value) == 1) {
            s.render_ns = value;
            found |= 1;
        }
        if (sscanf(line, "drm-engine-cache_clean: %" SCNu64, &value) == 1) {
            s.clean_ns = value;
            found |= 2;
        }
        if (sscanf(line, "v3d-jobs-render: %" SCNu64, &value) == 1) {
            s.render_jobs = value;
            found |= 4;
        }
        if (sscanf(line, "v3d-jobs-cache_clean: %" SCNu64, &value) == 1) {
            s.clean_jobs = value;
            found |= 8;
        }
        if (sscanf(line, "v3d-jobs-bin: %" SCNu64, &value) == 1) {
            s.bin_jobs = value;
            found |= 16;
        }
    }
    CHECK(!ferror(f) && !fclose(f) && found == 31);
    return s;
}
static GLuint program(int empty) {
    const char *head = "#version 310 es\nprecision highp int;\nlayout(local_size_x=128) in;\n";
    const char *body = empty ? "void main(){}\n"
                             : "layout(std430,binding=0) writeonly buffer D {uint data[];};\n"
                               "uniform highp uint loops;uniform highp uint seed;\n"
                               "void main(){uint i=gl_GlobalInvocationID.x;uint x=i^seed;"
                               "for(uint j=0u;j<loops;j++)x=x*1664525u+1013904223u;data[i]=x;}\n";
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 2, (const GLchar *[]){head, body}, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        exit(1);
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, sh);
    glLinkProgram(p);
    glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);
    return p;
}
int main(int argc, char **argv) {
    CHECK(argc >= 1 && argc <= 6);
    unsigned groups = argc > 1 ? number(argv[1], 1024, 0) : 1;
    unsigned loops = argc > 2 ? number(argv[2], 65536, 1) : 0;
    unsigned rounds = argc > 3 ? number(argv[3], 10001, 0) : 101;
    unsigned dispatches = argc > 4 ? number(argv[4], 64, 0) : 1;
    CHECK(argc < 6 || !strcmp(argv[5], "empty") || !strcmp(argv[5], "store"));
    int empty = argc == 6 && !strcmp(argv[5], "empty");
    CHECK(!empty || loops == 0);
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
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "V3D"));
    printf("%s; %s; groups %u loops %u rounds %u dispatches %u mode %s\n", glGetString(GL_VERSION),
           renderer, groups, loops, rounds, dispatches, empty ? "empty" : "store");
    GLuint p = program(empty);
    glUseProgram(p);
    GLint seed_location = -1;
    if (!empty) {
        GLint loc = glGetUniformLocation(p, "loops");
        CHECK(loc >= 0);
        glUniform1ui(loc, loops);
        seed_location = glGetUniformLocation(p, "seed");
        CHECK(seed_location >= 0);
    }
    size_t count = (size_t)groups * 128, bytes = GUARD + count * 4 + GUARD;
    unsigned char *initial = malloc(bytes);
    CHECK(initial);
    memset(initial, 0xa5, bytes);
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, GL_DYNAMIC_COPY);
    free(initial);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffer, GUARD, count * 4);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR);
    struct stats before = snapshot(fd);
    uint64_t total_render = 0, total_clean = 0;
    for (unsigned round = 0; round < rounds; round++) {
        uint32_t seed = 0x9e3779b9u * (round + 1);
        if (!empty)
            glUniform1ui(seed_location, seed);
        double start = now();
        for (unsigned d = 0; d < dispatches; d++) {
            glDispatchCompute(groups, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        double submitted = now();
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        glFinish();
        double finished = now();
        CHECK(glGetError() == GL_NO_ERROR);
        struct stats after = snapshot(fd);
        uint64_t jobs = (groups + 15) / 16 * dispatches;
        CHECK(after.render_jobs - before.render_jobs == jobs);
        CHECK(after.clean_jobs - before.clean_jobs == jobs);
        CHECK(after.bin_jobs == before.bin_jobs);
        printf("round %u %s host_submit_ms %.6f host_finish_ms %.6f wall_ms %.6f "
               "render_ms %.6f clean_ms %.6f render_jobs %" PRIu64 " clean_jobs %" PRIu64
               " bin_jobs 0\n",
               round, round ? "warm" : "cold", (submitted - start) * 1000,
               (finished - submitted) * 1000, (finished - start) * 1000,
               (after.render_ns - before.render_ns) / 1e6, (after.clean_ns - before.clean_ns) / 1e6,
               jobs, jobs);
        total_render += jobs;
        total_clean += jobs;
        before = after;
        // Keep verification outside the measurement. LCG affine composition lets
        // the CPU check any loop count with O(log loops) setup and O(count) work.
        uint32_t mul = 1, add = 0, a = 1664525u, b = 1013904223u, n = loops;
        while (n) {
            if (n & 1) {
                mul *= a;
                add = add * a + b;
            }
            b *= a + 1;
            a *= a;
            n >>= 1;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        const unsigned char *map =
            glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
        CHECK(map);
        const uint32_t *values = (const uint32_t *)(map + GUARD);
        for (size_t i = 0; i < count; i++) {
            uint32_t expected = empty ? 0xa5a5a5a5u : ((uint32_t)i ^ seed) * mul + add;
            if (values[i] != expected) {
                fprintf(stderr, "FAIL round %u index %zu: %#x expected %#x\n", round, i, values[i],
                        expected);
                return 1;
            }
        }
        for (size_t i = 0; i < GUARD; i++)
            CHECK(map[i] == 0xa5 && map[GUARD + count * 4 + i] == 0xa5);
        CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
        fflush(stdout);
    }
    printf("PASS: %u rounds, %zu values/round, exact outputs and guards; "
           "%" PRIu64 " render jobs, %" PRIu64 " cache-clean jobs, 0 bin jobs\n",
           rounds, count, total_render, total_clean);
    glDeleteBuffers(1, &buffer);
    glDeleteProgram(p);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
