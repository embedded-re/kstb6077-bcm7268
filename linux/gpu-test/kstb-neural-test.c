// A 32 -> 16 (ReLU) -> 8 dense neural network, executed as two compute
// dispatches. kstb-neural-test [--cpu-only] [batch [local-size [rounds]]].
// Defaults: 65, 16, 1. Round zero is cold; subsequent rounds reuse
// programs/buffers with new inputs.
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
#include <time.h>
#include <unistd.h>

#define INPUTS 32
#define HIDDEN 16
#define OUTPUTS 8
#define GUARD 4096
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static double now(void) {
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static float random_value(uint32_t *state) {
    uint32_t s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return ((int)((s >> 16) & 255) - 128) / 256.0f;
}
static float *array(size_t n) {
    float *p = malloc(n * sizeof(*p));
    CHECK(p);
    return p;
}
static void dense(const float *input, const float *weights, const float *bias, float *output,
                  unsigned batch, unsigned in, unsigned out, int relu) {
    for (unsigned b = 0; b < batch; b++)
        for (unsigned o = 0; o < out; o++) {
            float value = bias[o];
            for (unsigned i = 0; i < in; i++)
                value += input[b * in + i] * weights[o * in + i];
            output[b * out + o] = relu ? fmaxf(value, 0.0f) : value;
        }
}
static unsigned argmax(const float *values) {
    unsigned best = 0;
    for (unsigned i = 1; i < OUTPUTS; i++)
        if (values[i] > values[best])
            best = i;
    return best;
}
static uint32_t classes(const float *values, unsigned batch) {
    uint32_t hash = 2166136261u;
    for (unsigned b = 0; b < batch; b++)
        hash = (hash ^ argmax(values + b * OUTPUTS)) * 16777619u;
    return hash;
}
static const char *compute_source =
    "#version 310 es\nprecision highp float;precision highp int;\n"
    "layout(local_size_x=%u) in;uniform highp uint batch;\n"
    "layout(std430,binding=0) readonly buffer Inputs { float inputs[]; };\n"
    "layout(std430,binding=1) readonly buffer Weights { float weights[]; };\n"
    "layout(std430,binding=2) readonly buffer Bias { float bias[]; };\n"
    "layout(std430,binding=3) writeonly buffer Outputs { float outputs[]; };\n"
    "void main(){uint index=gl_GlobalInvocationID.x;if(index>=batch*%uu)return;"
    "uint b=index/%uu;uint o=index%%%uu;float value=bias[o];"
    "for(uint i=0u;i<%uu;i++)value+=inputs[b*%uu+i]*weights[o*%uu+i];"
    "outputs[index]=%s;}\n";
static GLuint program(unsigned in, unsigned out, int relu, unsigned local_size) {
    char source[4096];
    snprintf(source, sizeof(source), compute_source, local_size, out, out, out, in, in, in,
             relu ? "max(value,0.0)" : "value");
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, (const GLchar *[]){source}, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        exit(1);
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader);
    glLinkProgram(prog);
    glDeleteShader(shader);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    CHECK(ok);
    return prog;
}
static GLuint upload(const float *values, size_t count) {
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, count * 4, values, GL_STATIC_DRAW);
    CHECK(glGetError() == GL_NO_ERROR);
    return buffer;
}
static GLuint guarded(size_t count) {
    size_t bytes = GUARD + count * 4 + GUARD;
    unsigned char *initial = malloc(bytes);
    CHECK(initial);
    memset(initial, 0xa5, bytes);
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, initial, GL_DYNAMIC_COPY);
    free(initial);
    CHECK(glGetError() == GL_NO_ERROR);
    return buffer;
}
static unsigned verify(const char *label, GLuint buffer, const float *expected, float *actual,
                       size_t count) {
    size_t bytes = GUARD + count * 4 + GUARD;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    const unsigned char *mapping =
        glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    CHECK(mapping);
    const float *values = (const float *)(mapping + GUARD);
    unsigned errors = 0;
    float worst = 0;
    for (size_t i = 0; i < count; i++) {
        float error = fabsf(values[i] - expected[i]);
        if (!isfinite(values[i]) || error > 1e-5f + 2e-5f * fabsf(expected[i])) {
            if (errors < 8)
                printf("%s[%zu] %.9g expected %.9g\n", label, i, values[i], expected[i]);
            errors++;
        }
        worst = fmaxf(worst, error);
        actual[i] = values[i];
    }
    for (size_t i = 0; i < GUARD; i++)
        if (mapping[i] != 0xa5 || mapping[GUARD + count * 4 + i] != 0xa5)
            errors++;
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    printf("%s: %zu values, max absolute error %.9g, %u errors; guards checked\n", label, count,
           worst, errors);
    return errors;
}
static unsigned number(const char *text, unsigned maximum) {
    char *end;
    unsigned long n = strtoul(text, &end, 10);
    CHECK(*text && !*end && n > 0 && n <= maximum);
    return n;
}
static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
// Optional per-file queue accounting, always outside measured intervals.
static void dump_stats(int fd, int round) {
    if (!getenv("KSTB_NEURAL_STATS"))
        return;
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    FILE *f = fopen(path, "r");
    CHECK(f);
    while (fgets(line, sizeof(line), f))
        if (!strncmp(line, "drm-engine-", 11) || !strncmp(line, "v3d-jobs-", 9))
            printf("stats round %d %s", round, line);
    CHECK(!ferror(f) && !fclose(f));
}
int main(int argc, char **argv) {
    int cpu_only = argc > 1 && !strcmp(argv[1], "--cpu-only");
    int at = cpu_only ? 2 : 1;
    CHECK(argc >= at && argc <= at + 3);
    unsigned batch = argc > at ? number(argv[at], 4096) : 65;
    unsigned local_size = argc > at + 1 ? number(argv[at + 1], 128) : 16;
    unsigned rounds = argc > at + 2 ? number(argv[at + 2], 10000) : 1;
    CHECK(local_size == 16 || local_size == 32 || local_size == 64 || local_size == 128);
    size_t nx = (size_t)batch * INPUTS, nh = (size_t)batch * HIDDEN, ny = (size_t)batch * OUTPUTS;
    float *x = array(nx), *w1 = array(INPUTS * HIDDEN), *b1 = array(HIDDEN), *h = array(nh);
    float *w2 = array(HIDDEN * OUTPUTS), *b2 = array(OUTPUTS), *y = array(ny), *gpu_h = array(nh),
          *gpu_y = array(ny);
    uint32_t seed = 0x4b535442u;
    for (unsigned i = 0; i < INPUTS * HIDDEN; i++)
        w1[i] = random_value(&seed);
    for (unsigned i = 0; i < HIDDEN; i++)
        b1[i] = random_value(&seed) / 4;
    for (unsigned i = 0; i < HIDDEN * OUTPUTS; i++)
        w2[i] = random_value(&seed);
    for (unsigned i = 0; i < OUTPUTS; i++)
        b2[i] = random_value(&seed) / 4;
    for (size_t i = 0; i < nx; i++)
        x[i] = 2 * random_value(&seed);
    double start = now();
    dense(x, w1, b1, h, batch, INPUTS, HIDDEN, 1);
    dense(h, w2, b2, y, batch, HIDDEN, OUTPUTS, 0);
    double cpu_ms = (now() - start) * 1000;
    printf("dense %u -> %u ReLU -> %u, batch %u; CPU %.3f ms; class checksum %#x\n", INPUTS, HIDDEN,
           OUTPUTS, batch, cpu_ms, classes(y, batch));
    unsigned errors = 0;
    if (!cpu_only) {
        int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        CHECK(fd >= 0);
        struct gbm_device *gbm = gbm_create_device(fd);
        CHECK(gbm);
        EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
        CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
        CHECK(eglBindAPI(EGL_OPENGL_ES_API));
        const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1,
                                EGL_NONE};
        EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
        CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        CHECK(renderer && strstr(renderer, "V3D"));
        printf("%s; %s\n", glGetString(GL_VERSION), renderer);
        GLuint programs[] = {program(INPUTS, HIDDEN, 1, local_size),
                             program(HIDDEN, OUTPUTS, 0, local_size)};
        GLuint buffers[] = {upload(x, nx), upload(w1, INPUTS * HIDDEN),  upload(b1, HIDDEN),
                            guarded(nh),   upload(w2, HIDDEN * OUTPUTS), upload(b2, OUTPUTS),
                            guarded(ny)};
        double *times = malloc(rounds * sizeof(*times));
        CHECK(times);
        printf("local size %u; %u rounds; input upload/readback excluded from GPU "
               "timing\n",
               local_size, rounds);
        for (unsigned round = 0; round < rounds; round++) {
            if (round) {
                for (size_t i = 0; i < nx; i++)
                    x[i] = 2 * random_value(&seed);
                start = now();
                dense(x, w1, b1, h, batch, INPUTS, HIDDEN, 1);
                dense(h, w2, b2, y, batch, HIDDEN, OUTPUTS, 0);
                cpu_ms = (now() - start) * 1000;
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, nx * 4, x);
            }
            glFinish();
            if (!round)
                dump_stats(fd, -1);
            start = now();
            glUseProgram(programs[0]);
            GLint location = glGetUniformLocation(programs[0], "batch");
            CHECK(location >= 0);
            glUniform1ui(location, batch);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, buffers[2]);
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 3, buffers[3], GUARD, nh * 4);
            glDispatchCompute((nh + local_size - 1) / local_size, 1, 1);
            double dispatched_first = now();
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glUseProgram(programs[1]);
            location = glGetUniformLocation(programs[1], "batch");
            CHECK(location >= 0);
            glUniform1ui(location, batch);
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffers[3], GUARD, nh * 4);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[4]);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, buffers[5]);
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 3, buffers[6], GUARD, ny * 4);
            glDispatchCompute((ny + local_size - 1) / local_size, 1, 1);
            double dispatched_second = now();
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            double gpu_ms = (now() - start) * 1000;
            times[round] = gpu_ms;
            CHECK(glGetError() == GL_NO_ERROR);
            dump_stats(fd, round);
            errors += verify("hidden", buffers[3], h, gpu_h, nh);
            errors += verify("logits", buffers[6], y, gpu_y, ny);
            for (unsigned b = 0; b < batch; b++)
                if (argmax(y + b * OUTPUTS) != argmax(gpu_y + b * OUTPUTS))
                    errors++;
            printf("round %u (%s): CPU %.3f ms; dispatch1 host %.3f ms; dispatch2 "
                   "host %.3f ms; "
                   "finish %.3f ms; GPU dispatch + synchronization %.3f ms; GPU "
                   "class checksum %#x; %u classification "
                   "checks; %s\n",
                   round, round ? "warm" : "cold", cpu_ms, (dispatched_first - start) * 1000,
                   (dispatched_second - dispatched_first) * 1000,
                   gpu_ms - (dispatched_second - start) * 1000, gpu_ms, classes(gpu_y, batch),
                   batch, errors ? "FAIL" : "PASS");
            fflush(stdout);
            if (errors)
                break;
        }
        if (!errors && rounds > 1) {
            qsort(times + 1, rounds - 1, sizeof(*times), compare_double);
            unsigned count = rounds - 1;
            double median = (times[1 + (count - 1) / 2] + times[1 + count / 2]) / 2;
            printf("warm GPU ms: min %.3f median %.3f max %.3f; %u checked rounds\n", times[1],
                   median, times[rounds - 1], count);
        }
        free(times);
        for (unsigned i = 0; i < 2; i++)
            glDeleteProgram(programs[i]);
        glDeleteBuffers(7, buffers);
        CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
        CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
        gbm_device_destroy(gbm);
        close(fd);
    }
    free(x);
    free(w1);
    free(b1);
    free(h);
    free(w2);
    free(b2);
    free(y);
    free(gpu_h);
    free(gpu_y);
    return errors ? 1 : 0;
}
