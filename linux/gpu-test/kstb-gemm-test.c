// kstb-gemm-test [naive|shared1|tiled4|tiled2|scalar|neon1|neon4|neon1p|neon4p] [N [rounds]]
// C=A*B+bias, FP32. Cold round then warm rounds; all outputs/guards checked.
// CPU blocked 4x4 kernels use scalar FMA or explicit NEON FMA on four workers.
// Build with auto-vectorization disabled so the scalar baseline stays scalar.
// KSTB_GEMM_TRANSFER=1 times input uploads and output copy-back on each GPU round.
// KSTB_GEMM_GATE/READY optionally synchronize processes after their cold round.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <arm_neon.h>
#include <fcntl.h>
#include <gbm.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define GUARD 4096
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static double clock_seconds(clockid_t clock) {
    struct timespec t;
    CHECK(!clock_gettime(clock, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static unsigned number(const char *s, unsigned max) {
    char *e;
    unsigned long n = strtoul(s, &e, 10);
    CHECK(*s && !*e && n && n <= max);
    return n;
}
static float random_value(uint32_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return ((int)(*s & 15) - 8) / 16.0f;
}
static void reference(const float *a, const float *b, float *out, unsigned n) {
    float *bt = malloc((size_t)n * n * 4);
    CHECK(bt);
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < n; j++)
            bt[(size_t)j * n + i] = b[(size_t)i * n + j];
    for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < n; j++) {
            float v = 0;
            for (unsigned k = 0; k < n; k++)
                v += a[(size_t)i * n + k] * bt[(size_t)j * n + k];
            out[(size_t)i * n + j] = v;
        }
    free(bt);
}
static __attribute__((noinline)) void scalar4(const float *a, const float *b, float *c, unsigned n,
                                              unsigned i, unsigned j, unsigned lo, unsigned hi) {
    float s00 = c[(size_t)i * n + j], s01 = c[(size_t)i * n + j + 1],
          s02 = c[(size_t)i * n + j + 2], s03 = c[(size_t)i * n + j + 3];
    float s10 = c[(size_t)(i + 1) * n + j], s11 = c[(size_t)(i + 1) * n + j + 1],
          s12 = c[(size_t)(i + 1) * n + j + 2], s13 = c[(size_t)(i + 1) * n + j + 3];
    float s20 = c[(size_t)(i + 2) * n + j], s21 = c[(size_t)(i + 2) * n + j + 1],
          s22 = c[(size_t)(i + 2) * n + j + 2], s23 = c[(size_t)(i + 2) * n + j + 3];
    float s30 = c[(size_t)(i + 3) * n + j], s31 = c[(size_t)(i + 3) * n + j + 1],
          s32 = c[(size_t)(i + 3) * n + j + 2], s33 = c[(size_t)(i + 3) * n + j + 3];
    for (unsigned k = lo; k < hi; k++) {
        float a0 = a[(size_t)i * n + k], a1 = a[(size_t)(i + 1) * n + k],
              a2 = a[(size_t)(i + 2) * n + k], a3 = a[(size_t)(i + 3) * n + k];
        float b0 = b[(size_t)k * n + j], b1 = b[(size_t)k * n + j + 1],
              b2 = b[(size_t)k * n + j + 2], b3 = b[(size_t)k * n + j + 3];
        s00 += a0 * b0;
        s01 += a0 * b1;
        s02 += a0 * b2;
        s03 += a0 * b3;
        s10 += a1 * b0;
        s11 += a1 * b1;
        s12 += a1 * b2;
        s13 += a1 * b3;
        s20 += a2 * b0;
        s21 += a2 * b1;
        s22 += a2 * b2;
        s23 += a2 * b3;
        s30 += a3 * b0;
        s31 += a3 * b1;
        s32 += a3 * b2;
        s33 += a3 * b3;
    }
    c[(size_t)i * n + j] = s00;
    c[(size_t)i * n + j + 1] = s01;
    c[(size_t)i * n + j + 2] = s02;
    c[(size_t)i * n + j + 3] = s03;
    c[(size_t)(i + 1) * n + j] = s10;
    c[(size_t)(i + 1) * n + j + 1] = s11;
    c[(size_t)(i + 1) * n + j + 2] = s12;
    c[(size_t)(i + 1) * n + j + 3] = s13;
    c[(size_t)(i + 2) * n + j] = s20;
    c[(size_t)(i + 2) * n + j + 1] = s21;
    c[(size_t)(i + 2) * n + j + 2] = s22;
    c[(size_t)(i + 2) * n + j + 3] = s23;
    c[(size_t)(i + 3) * n + j] = s30;
    c[(size_t)(i + 3) * n + j + 1] = s31;
    c[(size_t)(i + 3) * n + j + 2] = s32;
    c[(size_t)(i + 3) * n + j + 3] = s33;
}
static __attribute__((noinline)) void neon4(const float *a, const float *b, float *c, unsigned n,
                                            unsigned i, unsigned j, unsigned lo, unsigned hi, int packed) {
    const float *bp = packed ? b + (size_t)j * n : b + j;
    unsigned bstep = packed ? 4 : n;
    float32x4_t c0 = vld1q_f32(c + (size_t)i * n + j), c1 = vld1q_f32(c + (size_t)(i + 1) * n + j);
    float32x4_t c2 = vld1q_f32(c + (size_t)(i + 2) * n + j),
                c3 = vld1q_f32(c + (size_t)(i + 3) * n + j);
    for (unsigned k = lo; k < hi; k++) {
        float32x4_t v = vld1q_f32(bp + (size_t)k * bstep);
        c0 = vfmaq_n_f32(c0, v, a[(size_t)i * n + k]);
        c1 = vfmaq_n_f32(c1, v, a[(size_t)(i + 1) * n + k]);
        c2 = vfmaq_n_f32(c2, v, a[(size_t)(i + 2) * n + k]);
        c3 = vfmaq_n_f32(c3, v, a[(size_t)(i + 3) * n + k]);
    }
    vst1q_f32(c + (size_t)i * n + j, c0);
    vst1q_f32(c + (size_t)(i + 1) * n + j, c1);
    vst1q_f32(c + (size_t)(i + 2) * n + j, c2);
    vst1q_f32(c + (size_t)(i + 3) * n + j, c3);
}
static void blocked(const float *a, const float *b, float *c, unsigned n, unsigned tid,
                    unsigned threads, int neon, int packed) {
    for (unsigned ib = tid * 32; ib < n; ib += threads * 32)
        for (unsigned jb = 0; jb < n; jb += 64)
            for (unsigned kb = 0; kb < n; kb += 128)
                for (unsigned i = ib; i < MIN(n, ib + 32); i += 4)
                    for (unsigned j = jb; j < MIN(n, jb + 64); j += 4) {
                        unsigned hi = MIN(n, kb + 128);
                        if (i + 4 <= n && j + 4 <= n) {
                            if (neon)
                                neon4(a, b, c, n, i, j, kb, hi, packed);
                            else
                                scalar4(a, b, c, n, i, j, kb, hi);
                        } else
                            for (unsigned r = i; r < MIN(n, i + 4); r++)
                                for (unsigned col = j; col < MIN(n, j + 4); col++) {
                                    float s = c[(size_t)r * n + col];
                                    for (unsigned k = kb; k < hi; k++)
                                        s += a[(size_t)r * n + k] * b[(size_t)k * n + col];
                                    c[(size_t)r * n + col] = s;
                                }
                    }
}
struct team {
    const float *a, *b;
    float *c;
    unsigned n, workers;
    int quit, packed;
    pthread_barrier_t start, end;
};
struct worker {
    struct team *team;
    unsigned tid;
};
static void barrier(pthread_barrier_t *b) {
    int r = pthread_barrier_wait(b);
    CHECK(r == 0 || r == PTHREAD_BARRIER_SERIAL_THREAD);
}
static void *work(void *data) {
    struct worker *w = data;
    struct team *t = w->team;
    for (;;) {
        barrier(&t->start);
        if (t->quit)
            break;
        blocked(t->a, t->b, t->c, t->n, w->tid, t->workers, 1, t->packed);
        barrier(&t->end);
    }
    return NULL;
}
static const char *naive_shader =
    "#version 310 es\nprecision highp float;precision highp int;\n"
    "layout(local_size_x=128) in;uniform highp uint n;uniform highp float bias;\n"
    "layout(std430,binding=0) readonly buffer A{float a[];};\n"
    "layout(std430,binding=1) readonly buffer B{float b[];};\n"
    "layout(std430,binding=2) writeonly buffer C{float c[];};\n"
    "void main(){uint id=gl_GlobalInvocationID.x;if(id>=n*n)return;uint r=id/n,col=id%n;float "
    "s=0.0;"
    "for(uint k=0u;k<n;k++)s+=a[r*n+k]*b[k*n+col];c[id]=s+bias;}\n";
static const char *tiled4_shader =
    "#version 310 es\nprecision highp float;precision highp int;\n"
    "layout(local_size_x=8,local_size_y=8) in;uniform highp uint n;uniform highp float bias;\n"
    "layout(std430,binding=0) readonly buffer A{float a[];};\n"
    "layout(std430,binding=1) readonly buffer B{float b[];};\n"
    "layout(std430,binding=2) writeonly buffer C{float c[];};\n"
    "shared float sa[512];shared float sb[512];\n"
    "void main(){uint lx=gl_LocalInvocationID.x,ly=gl_LocalInvocationID.y;"
    "uint row=gl_WorkGroupID.y*32u+ly*4u,col=gl_WorkGroupID.x*32u+lx*4u;"
    "vec4 c0=vec4(0.0),c1=vec4(0.0),c2=vec4(0.0),c3=vec4(0.0);"
    "for(uint base=0u;base<n;base+=16u){"
    "for(uint p=gl_LocalInvocationIndex;p<512u;p+=64u){"
    "uint ar=gl_WorkGroupID.y*32u+p/16u,ak=base+p%16u;"
    "uint bk=base+p/32u,bc=gl_WorkGroupID.x*32u+p%32u;"
    "sa[p]=a[ar*n+ak];sb[p]=b[bk*n+bc];}barrier();"
    "for(uint k=0u;k<16u;k++){vec4 "
    "bv=vec4(sb[k*32u+lx*4u],sb[k*32u+lx*4u+1u],sb[k*32u+lx*4u+2u],sb[k*32u+lx*4u+3u]);"
    "c0+=sa[(ly*4u)*16u+k]*bv;c1+=sa[(ly*4u+1u)*16u+k]*bv;"
    "c2+=sa[(ly*4u+2u)*16u+k]*bv;c3+=sa[(ly*4u+3u)*16u+k]*bv;}barrier();}"
    "c[(row+0u)*n+col+0u]=c0.x+bias;"
    "c[(row+0u)*n+col+1u]=c0.y+bias;"
    "c[(row+0u)*n+col+2u]=c0.z+bias;"
    "c[(row+0u)*n+col+3u]=c0.w+bias;"
    "c[(row+1u)*n+col+0u]=c1.x+bias;"
    "c[(row+1u)*n+col+1u]=c1.y+bias;"
    "c[(row+1u)*n+col+2u]=c1.z+bias;"
    "c[(row+1u)*n+col+3u]=c1.w+bias;"
    "c[(row+2u)*n+col+0u]=c2.x+bias;"
    "c[(row+2u)*n+col+1u]=c2.y+bias;"
    "c[(row+2u)*n+col+2u]=c2.z+bias;"
    "c[(row+2u)*n+col+3u]=c2.w+bias;"
    "c[(row+3u)*n+col+0u]=c3.x+bias;"
    "c[(row+3u)*n+col+1u]=c3.y+bias;"
    "c[(row+3u)*n+col+2u]=c3.z+bias;"
    "c[(row+3u)*n+col+3u]=c3.w+bias;"
    "}\n";
static const char *tiled2_shader =
    "#version 310 es\nprecision highp float;precision highp int;\n"
    "layout(local_size_x=8,local_size_y=8) in;uniform highp uint n;uniform highp float bias;\n"
    "layout(std430,binding=0) readonly buffer A{float a[];};\n"
    "layout(std430,binding=1) readonly buffer B{float b[];};\n"
    "layout(std430,binding=2) writeonly buffer C{float c[];};\n"
    "shared float sa[256];shared float sb[256];\n"
    "void main(){uint lx=gl_LocalInvocationID.x,ly=gl_LocalInvocationID.y;"
    "uint row=gl_WorkGroupID.y*16u+ly*2u,col=gl_WorkGroupID.x*16u+lx*2u;"
    "vec2 c0=vec2(0.0),c1=vec2(0.0);"
    "for(uint base=0u;base<n;base+=16u){"
    "for(uint p=gl_LocalInvocationIndex;p<256u;p+=64u){"
    "uint ar=gl_WorkGroupID.y*16u+p/16u,ak=base+p%16u;"
    "uint bk=base+p/16u,bc=gl_WorkGroupID.x*16u+p%16u;"
    "sa[p]=a[ar*n+ak];sb[p]=b[bk*n+bc];}barrier();"
    "for(uint k=0u;k<16u;k++){vec2 bv=vec2(sb[k*16u+lx*2u],sb[k*16u+lx*2u+1u]);"
    "c0+=sa[(ly*2u)*16u+k]*bv;c1+=sa[(ly*2u+1u)*16u+k]*bv;}barrier();}"
    "c[row*n+col]=c0.x+bias;c[row*n+col+1u]=c0.y+bias;"
    "c[(row+1u)*n+col]=c1.x+bias;c[(row+1u)*n+col+1u]=c1.y+bias;}\n";
// Small shared-memory/barrier control: one result per invocation, 8x8 tiles.
static const char *shared1_shader =
    "#version 310 es\nprecision highp float;precision highp int;\n"
    "layout(local_size_x=8,local_size_y=8) in;uniform highp uint n;uniform highp float bias;"
    "layout(std430,binding=0) readonly buffer A{float a[];};"
    "layout(std430,binding=1) readonly buffer B{float b[];};"
    "layout(std430,binding=2) writeonly buffer C{float c[];};"
    "shared float sa[64];shared float sb[64];"
    "void main(){uint x=gl_LocalInvocationID.x,y=gl_LocalInvocationID.y;"
    "uint row=gl_WorkGroupID.y*8u+y,col=gl_WorkGroupID.x*8u+x;float sum=0.0;"
    "for(uint base=0u;base<n;base+=8u){"
    "sa[y*8u+x]=a[row*n+base+x];sb[y*8u+x]=b[(base+y)*n+col];barrier();"
    "for(uint k=0u;k<8u;k++)sum+=sa[y*8u+k]*sb[k*8u+x];barrier();}"
    "c[row*n+col]=sum+bias;}";
static GLuint program(const char *source) {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &source, NULL);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
        exit(1);
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, sh);
    glLinkProgram(p);
    glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log);
    }
    CHECK(ok);
    return p;
}
struct stats {
    uint64_t render, clean, jobs;
};
static struct stats stats(int fd) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/self/fdinfo/%d", fd);
    FILE *f = fopen(path, "r");
    CHECK(f);
    struct stats s = {0};
    unsigned found = 0;
    while (fgets(line, sizeof(line), f)) {
        uint64_t v;
        if (sscanf(line, "drm-engine-render: %" SCNu64, &v) == 1) {
            s.render = v;
            found |= 1;
        }
        if (sscanf(line, "drm-engine-cache_clean: %" SCNu64, &v) == 1) {
            s.clean = v;
            found |= 2;
        }
        if (sscanf(line, "v3d-jobs-render: %" SCNu64, &v) == 1) {
            s.jobs = v;
            found |= 4;
        }
    }
    CHECK(!ferror(f) && !fclose(f) && found == 7);
    return s;
}
static GLuint upload(const void *data, size_t bytes) {
    GLuint b;
    glGenBuffers(1, &b);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_STATIC_DRAW);
    CHECK(glGetError() == GL_NO_ERROR);
    return b;
}
static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
int main(int argc, char **argv) {
    CHECK(argc >= 2 && argc <= 4);
    const char *mode = argv[1];
    int tiled4 = !strcmp(mode, "tiled4") || !strcmp(mode, "tiled"),
        tiled2 = !strcmp(mode, "tiled2"), shared1 = !strcmp(mode, "shared1"),
        tiled = tiled4 || tiled2 || shared1,
        gpu = !strcmp(mode, "naive") || tiled,
        packed = !strcmp(mode, "neon1p") || !strcmp(mode, "neon4p"),
        neon = !strcmp(mode, "neon4") || !strcmp(mode, "neon1") || packed;
    unsigned cpu_workers = (!strcmp(mode, "neon1") || !strcmp(mode, "neon1p")) ? 1 : 4;
    int transfer = gpu && getenv("KSTB_GEMM_TRANSFER") != NULL;
    unsigned tile = shared1 ? 8 : tiled4 ? 32 : 16;
    CHECK(gpu || neon || !strcmp(mode, "scalar"));
    unsigned n = argc > 2 ? number(argv[2], 1024) : 1024,
             rounds = argc > 3 ? number(argv[3], 101) : 4;
    CHECK(!packed || n % 4 == 0);
    CHECK(!tiled || n % tile == 0); // Aligned benchmark kernel; no edge-tile branches.
    size_t count = (size_t)n * n, bytes = count * 4, guarded_bytes = GUARD + bytes + GUARD;
    float *a = malloc(bytes), *b = malloc(bytes), *ref = malloc(bytes);
    unsigned char *storage = malloc(guarded_bytes);
    CHECK(a && b && ref && storage);
    memset(storage, 0xa5, guarded_bytes);
    float *c = (float *)(storage + GUARD);
    uint32_t seed = 0x4b535442u;
    for (size_t i = 0; i < count; i++) {
        a[i] = random_value(&seed);
        b[i] = random_value(&seed);
    }
    double begin = clock_seconds(CLOCK_MONOTONIC);
    reference(a, b, ref, n);
    printf("reference N %u %.3f ms; binary-fraction inputs, full exact checks\n", n,
           (clock_seconds(CLOCK_MONOTONIC) - begin) * 1000);
    fflush(stdout);
    int fd = -1;
    struct gbm_device *gbm = NULL;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    GLuint prog = 0, buffers[3] = {0};
    GLint bias_location = -1;
    if (gpu) {
        fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        CHECK(fd >= 0);
        gbm = gbm_create_device(fd);
        CHECK(gbm);
        dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
        CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
        CHECK(eglBindAPI(EGL_OPENGL_ES_API));
        const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1,
                                EGL_NONE};
        ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attrs);
        CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        CHECK(renderer && strstr(renderer, "V3D"));
        printf("%s; %s\n", glGetString(GL_VERSION), renderer);
        prog = program(tiled4 ? tiled4_shader : tiled2 ? tiled2_shader :
                       shared1 ? shared1_shader : naive_shader);
        glUseProgram(prog);
        GLint loc = glGetUniformLocation(prog, "n");
        CHECK(loc >= 0);
        glUniform1ui(loc, n);
        bias_location = glGetUniformLocation(prog, "bias");
        CHECK(bias_location >= 0);
        buffers[0] = upload(a, bytes);
        buffers[1] = upload(b, bytes);
        buffers[2] = upload(storage, guarded_bytes);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 2, buffers[2], GUARD, bytes);
        glFinish();
        CHECK(glGetError() == GL_NO_ERROR);
    }
    float *packed_b = packed ? malloc(bytes) : NULL;
    CHECK(!packed || packed_b);
    struct team team = {.a = a, .b = packed ? packed_b : b, .c = c, .n = n,
                        .workers = cpu_workers, .packed = packed};
    struct worker workers[4];
    pthread_t threads[4];
    if (neon) {
        CHECK(!pthread_barrier_init(&team.start, NULL, cpu_workers + 1) &&
              !pthread_barrier_init(&team.end, NULL, cpu_workers + 1));
        for (unsigned i = 0; i < cpu_workers; i++) {
            workers[i] = (struct worker){.team = &team, .tid = i};
            CHECK(!pthread_create(&threads[i], NULL, work, &workers[i]));
        }
    }
    printf("data_path %s CPU_workers %u\n", gpu ? (transfer ? "upload-compute-copyback" : "resident") : "host", neon ? cpu_workers : 1);
    double *timings = malloc(rounds * sizeof(*timings));
    CHECK(timings);
    double flops = 2.0 * n * n * n;
    for (unsigned round = 0; round < rounds; round++) {
        const char *gate = getenv("KSTB_GEMM_GATE");
        if (round == 1 && gate) {
            const char *ready = getenv("KSTB_GEMM_READY");
            CHECK(ready);
            int signal_fd = open(ready, O_WRONLY | O_CREAT | O_EXCL, 0600);
            CHECK(signal_fd >= 0 && !close(signal_fd));
            signal_fd = open(gate, O_RDONLY);
            char token;
            CHECK(signal_fd >= 0 && read(signal_fd, &token, 1) == 1);
            CHECK(!close(signal_fd));
        }
        float bias = (round + 1) / 1024.0f;
        struct stats before = {0}, after = {0};
        if (gpu) {
            glUniform1f(bias_location, bias);
            before = stats(fd);
        }
        double cpu_start = clock_seconds(CLOCK_PROCESS_CPUTIME_ID),
               start = clock_seconds(CLOCK_MONOTONIC), submitted;
        if (gpu) {
            if (transfer) {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, a);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, b);
            }
            if (tiled)
                glDispatchCompute(n / tile, n / tile, 1);
            else
                glDispatchCompute((count + 127) / 128, 1, 1);
            submitted = clock_seconds(CLOCK_MONOTONIC);
            glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
            glFinish();
            if (transfer) {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
                const void *readback = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0,
                                                        guarded_bytes, GL_MAP_READ_BIT);
                CHECK(readback);
                memcpy(storage, readback, guarded_bytes);
                CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
            }
        } else {
            // Pack contiguous K-by-four column panels; include this cost every round.
            if (packed)
                for (unsigned j = 0; j < n; j += 4)
                    for (unsigned k = 0; k < n; k++)
                        memcpy(packed_b + (size_t)j * n + k * 4,
                               b + (size_t)k * n + j, 4 * sizeof(float));
            for (size_t i = 0; i < count; i++)
                c[i] = bias;
            if (neon) {
                barrier(&team.start);
                barrier(&team.end);
            } else
                blocked(a, b, c, n, 0, 1, 0, 0);
            submitted = clock_seconds(CLOCK_MONOTONIC);
        }
        double finish = clock_seconds(CLOCK_MONOTONIC),
               cpu_ms = (clock_seconds(CLOCK_PROCESS_CPUTIME_ID) - cpu_start) * 1000,
               ms = (finish - start) * 1000;
        timings[round] = ms;
        const unsigned char *mapping = storage;
        if (gpu) {
            CHECK(glGetError() == GL_NO_ERROR);
            after = stats(fd);
            unsigned groups = tiled ? (n / tile) * (n / tile) : (count + 127) / 128;
            CHECK(after.jobs - before.jobs == (groups + 15) / 16);
            if (!transfer) {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
                mapping = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, guarded_bytes, GL_MAP_READ_BIT);
                CHECK(mapping);
            }
        }
        const float *values = (const float *)(mapping + GUARD);
        unsigned errors = 0;
        float worst = 0;
        uint64_t checksum = 1469598103934665603ull;
        for (size_t i = 0; i < count; i++) {
            float expected = ref[i] + bias, err = fabsf(values[i] - expected);
            worst = fmaxf(worst, err);
            if (!isfinite(values[i]) || values[i] != expected) {
                if (errors < 4)
                    fprintf(stderr, "value %zu %.9g expected %.9g\n", i, values[i], expected);
                errors++;
            }
            uint32_t bits;
            memcpy(&bits, values + i, 4);
            checksum = (checksum ^ bits) * 1099511628211ull;
        }
        for (size_t i = 0; i < GUARD; i++)
            if (mapping[i] != 0xa5 || mapping[GUARD + bytes + i] != 0xa5)
                errors++;
        if (gpu && !transfer)
            CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
        printf("round %u %s mode %s N %u wall_ms %.6f host_submit_ms %.6f host_finish_ms %.6f "
               "process_cpu_ms %.6f render_ms %.6f clean_ms %.6f render_jobs %" PRIu64
               " GFLOPS %.6f max_error %.9g errors %u checksum %#" PRIx64 " %s\n",
               round, round ? "warm" : "cold", mode, n, ms, (submitted - start) * 1000,
               (finish - submitted) * 1000, cpu_ms, (after.render - before.render) / 1e6,
               (after.clean - before.clean) / 1e6, after.jobs - before.jobs, flops / ms / 1e6,
               worst, errors, checksum, errors ? "FAIL" : "PASS");
        fflush(stdout);
        CHECK(!errors);
    }
    if (rounds > 1) {
        qsort(timings + 1, rounds - 1, sizeof(*timings), compare_double);
        unsigned k = rounds - 1;
        double median = (timings[1 + (k - 1) / 2] + timings[1 + k / 2]) / 2;
        printf("summary mode %s N %u warm_rounds %u median_ms %.6f GFLOPS %.6f\n", mode, n, k,
               median, flops / median / 1e6);
    }
    if (neon) {
        team.quit = 1;
        barrier(&team.start);
        for (unsigned i = 0; i < cpu_workers; i++)
            CHECK(!pthread_join(threads[i], NULL));
        CHECK(!pthread_barrier_destroy(&team.start) && !pthread_barrier_destroy(&team.end));
    }
    if (gpu) {
        glDeleteBuffers(3, buffers);
        glDeleteProgram(prog);
        CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
        CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
        gbm_device_destroy(gbm);
        close(fd);
    }
    free(packed_b);
    free(timings);
    free(a);
    free(b);
    free(ref);
    free(storage);
    puts("PASS: full matrix exact, guards intact");
    return 0;
}
