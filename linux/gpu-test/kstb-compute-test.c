// glDispatchCompute ID/SSBO test. Experimental driver, no shared memory.
// kstb-compute-test [local_x local_y local_z grid_x grid_y grid_z]
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

int main(int argc, char **argv)
{
    CHECK(argc == 1 || argc == 7);
    unsigned b[3] = {8,2,2}, g[3] = {3,2,2};
    if (argc == 7) for (int d=0; d<3; d++) {
        b[d]=strtoul(argv[d+1],NULL,0); g[d]=strtoul(argv[d+4],NULL,0);
    }
    CHECK(b[0] && b[1] && b[2] && b[0]*b[1]*b[2]<=128);
    CHECK(g[0] && g[1] && g[2] && g[0]<=65535 && g[1]<=65535 && g[2]<=65535);
    size_t n=(size_t)b[0]*b[1]*b[2]*g[0]*g[1]*g[2]; CHECK(n<=1048576);
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

    char source[4096];
    snprintf(source,sizeof(source),
        "#version 310 es\nprecision highp int;\n"
        "layout(local_size_x=%u,local_size_y=%u,local_size_z=%u) in;\n"
        "layout(std430,binding=0) writeonly buffer D { uvec4 result[]; };\n"
        "void main() { uvec3 v=gl_GlobalInvocationID;"
        "uvec3 size=gl_NumWorkGroups*gl_WorkGroupSize;"
        "uint i=3u*(v.x+size.x*(v.y+size.y*v.z));"
        "result[i]=uvec4(v,gl_LocalInvocationIndex);"
        "result[i+1u]=uvec4(gl_WorkGroupID,gl_NumWorkGroups.x);"
        "result[i+2u]=uvec4(gl_LocalInvocationID,"
        "gl_NumWorkGroups.y+65536u*gl_NumWorkGroups.z); }\n",b[0],b[1],b[2]);
    GLuint prog=glCreateProgram(); glAttachShader(prog,shader(GL_COMPUTE_SHADER,source));
    glLinkProgram(prog); GLint linked; glGetProgramiv(prog,GL_LINK_STATUS,&linked);
    if(!linked) { char log[4096]; glGetProgramInfoLog(prog,sizeof(log),NULL,log); fprintf(stderr,"%s\n",log); }
    CHECK(linked); glUseProgram(prog);
    size_t bytes=4096+n*48+4096;
    uint32_t *initial=malloc(bytes); CHECK(initial); memset(initial,0xa5,bytes);
    GLuint buffer; glGenBuffers(1,&buffer); glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,initial,GL_DYNAMIC_COPY);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,buffer,4096,n*48);
    CHECK(glGetError()==GL_NO_ERROR);
    GLuint indirect=0;
    if (getenv("KSTB_INDIRECT")) {
        uint32_t dispatch[8]={0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,
                              g[0],g[1],g[2],0xa5a5a5a5};
        glGenBuffers(1,&indirect); glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,indirect);
        glBufferData(GL_DISPATCH_INDIRECT_BUFFER,sizeof(dispatch),dispatch,GL_DYNAMIC_COPY);
        if (getenv("KSTB_GPU_INDIRECT")) {
            // Clear dimensions so a missing producer cannot pass accidentally.
            memset(dispatch+4,0,12);
            glBufferSubData(GL_DISPATCH_INDIRECT_BUFFER,0,sizeof(dispatch),dispatch);
            char setup[1024]; snprintf(setup,sizeof(setup),
                "#version 310 es\nlayout(local_size_x=1) in;"
                "layout(std430,binding=1) buffer I { uint dims[]; };"
                "void main() { dims[4]=%uu; dims[5]=%uu; dims[6]=%uu; }",g[0],g[1],g[2]);
            GLuint producer=glCreateProgram();
            glAttachShader(producer,shader(GL_COMPUTE_SHADER,setup));
            glLinkProgram(producer); glGetProgramiv(producer,GL_LINK_STATUS,&linked); CHECK(linked);
            glUseProgram(producer); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,indirect);
            glDispatchCompute(1,1,1); glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
            glUseProgram(prog); glDeleteProgram(producer);
        }
        glDispatchComputeIndirect(16);
    } else {
        glDispatchCompute(g[0],g[1],g[2]);
    }
    CHECK(glGetError()==GL_NO_ERROR);
    int chain=getenv("KSTB_CHAIN")!=NULL;
    if(chain) {
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        char transform[1024]; snprintf(transform,sizeof(transform),
            "#version 310 es\nlayout(local_size_x=128) in;"
            "layout(std430,binding=0) buffer D { uint words[]; };"
            "void main() { uint i=gl_GlobalInvocationID.x;"
            "if(i<%zuu) words[i]^=0x13579bdfu; }",n*12);
        GLuint consumer=glCreateProgram();
        glAttachShader(consumer,shader(GL_COMPUTE_SHADER,transform));
        glLinkProgram(consumer); glGetProgramiv(consumer,GL_LINK_STATUS,&linked); CHECK(linked);
        glUseProgram(consumer); glDispatchCompute((n*12+127)/128,1,1);
        glDeleteProgram(consumer);
    }
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT|GL_SHADER_STORAGE_BARRIER_BIT); glFinish();
    CHECK(glGetError()==GL_NO_ERROR);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffer);
    const uint32_t *data=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,bytes,GL_MAP_READ_BIT); CHECK(data);
    size_t errors=0;
    for(size_t i=0;i<bytes/4;i++) {
        uint32_t expected=0xa5a5a5a5;
        if(i>=1024 && i<1024+n*12) {
            size_t record=(i-1024)/12; unsigned field=(i-1024)%12;
            unsigned x=record%(b[0]*g[0]);
            unsigned y=(record/(b[0]*g[0]))%(b[1]*g[1]);
            unsigned z=record/((size_t)b[0]*g[0]*b[1]*g[1]);
            uint32_t words[12]={x,y,z,x%b[0]+b[0]*(y%b[1]+b[1]*(z%b[2])),
                x/b[0],y/b[1],z/b[2],g[0],x%b[0],y%b[1],z%b[2],g[1]+65536*g[2]};
            expected=words[field]^(chain?0x13579bdfu:0);
        }
        if(data[i]!=expected) { if(errors<12) fprintf(stderr,"word %zu: %#x expected %#x\n",i,data[i],expected); errors++; }
    }
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
    printf("compute IDs local=%ux%ux%u grid=%ux%ux%u invocations=%zu: %s (%zu errors, guards checked)\n",b[0],b[1],b[2],g[0],g[1],g[2],n,errors?"FAIL":"exact",errors);
    if(indirect) glDeleteBuffers(1,&indirect);
    CHECK(!errors); glDeleteBuffers(1,&buffer); glDeleteProgram(prog); free(initial);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(dpy, ctx) && eglTerminate(dpy));
    gbm_device_destroy(gbm); close(fd);
    return 0;
}
