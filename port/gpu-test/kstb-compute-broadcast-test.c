// Conditional shared-memory broadcast, all intermediate values checked.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

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
        fprintf(stderr, "%s\n", log);
        exit(1);
    }
    return sh;
}

int main(void) {
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    printf("%s\n", glGetString(GL_VERSION));


    const char *source="#version 310 es\nlayout(local_size_x=64) in;"
        "shared highp float s; uniform highp float val[15]; uniform highp float ref[15];"
        "layout(std430,binding=0) buffer D { uvec4 result[]; };"
        "void main(){ uint local=gl_LocalInvocationIndex;"
        "for(uint i=0u;i<64u;i++){ if(i==local) s=val[i%15u]; barrier();"
        "result[i*64u+local]=uvec4(floatBitsToUint(s),floatBitsToUint(ref[i%15u]),i,local); barrier(); }}";
    int boolean=getenv("KSTB_BROADCAST_BOOL")!=NULL;
    int aggregate=getenv("KSTB_BROADCAST_AGGREGATE")!=NULL || boolean;
    if(aggregate) source="#version 310 es\nlayout(local_size_x=64) in;"
        "shared highp float s; uniform highp float val[15]; uniform highp float ref[15];"
        "uniform highp uint iters; layout(std430,binding=0) buffer D { uvec4 result[]; };"
        "void main(){ uint local=gl_LocalInvocationIndex; uint bad=0u; uvec3 first=uvec3(0);"
        "for(uint i=0u;i<iters;i++){ if(i==local) s=val[i%15u]; barrier();"
        "if(s!=ref[i%15u]) { if(bad==0u) first=uvec3(floatBitsToUint(s),floatBitsToUint(ref[i%15u]),i); bad++; } barrier(); }"
        "result[local]=uvec4(bad,first); }";
    if(boolean) source="#version 310 es\nlayout(local_size_x=64) in;"
        "shared highp float s; uniform highp float val[15]; uniform highp float ref[15];"
        "uniform highp uint iters; layout(std430,binding=0) buffer D { uvec4 result[]; };"
        "void main(){ uint local=gl_LocalInvocationIndex; bool allOk=true;"
        "for(uint i=0u;i<iters;i++){ if(i==local) s=val[i%15u]; barrier();"
        "if(s!=ref[i%15u]) allOk=false; barrier(); }"
        "result[local]=uvec4(allOk?1u:0u,floatBitsToUint(s),floatBitsToUint(ref[(iters-1u)%15u]),local); }";
    GLuint prog=glCreateProgram(); glAttachShader(prog,shader(GL_COMPUTE_SHADER,source));
    glLinkProgram(prog); GLint linked; glGetProgramiv(prog,GL_LINK_STATUS,&linked); CHECK(linked);
    glUseProgram(prog); float values[64]; for(int i=0;i<64;i++) values[i]=i-32;
    glUniform1fv(glGetUniformLocation(prog,"val"),boolean?64:15,values);
    glUniform1fv(glGetUniformLocation(prog,"ref"),boolean?64:15,values);
    glUniform1ui(glGetUniformLocation(prog,"iters"),64);
    GLuint buffer; glGenBuffers(1,&buffer); glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    uint32_t initial[64*64*4]; memset(initial,0xa5,sizeof(initial));
    glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(initial),initial,GL_DYNAMIC_COPY);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffer); glDispatchCompute(1,1,1);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT); glFinish(); CHECK(glGetError()==GL_NO_ERROR);
    const uint32_t *data=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,sizeof(initial),GL_MAP_READ_BIT); CHECK(data);
    unsigned errors=0;
    for(unsigned i=0;i<(aggregate?1:64);i++) for(unsigned local=0;local<64;local++) {
        uint32_t bits; memcpy(&bits,&values[i%15],4);
        uint32_t expected[4]={bits,bits,i,local};
        if(aggregate) memset(expected,0,sizeof(expected));
        if(boolean) { uint32_t last; memcpy(&last,&values[63%15],4);
            expected[0]=1; expected[1]=last; expected[2]=last; expected[3]=local; }
        if(memcmp(data+(i*64+local)*4,expected,16)) {
            if(errors<24) printf("round %u local %u: got (%#x,%#x,%#x,%#x), expected (%#x,%#x,%#x,%#x)\n",
                i,local,data[(i*64+local)*4],data[(i*64+local)*4+1],
                data[(i*64+local)*4+2],data[(i*64+local)*4+3],
                expected[0],expected[1],expected[2],expected[3]);
            errors++;
        }
    }
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    printf("shared broadcast intermediate checks: %u errors\n",errors); CHECK(!errors);
    glDeleteBuffers(1,&buffer); glDeleteProgram(prog);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm); close(fd);
    return 0;
}
