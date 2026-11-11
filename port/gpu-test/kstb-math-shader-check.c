// Shader front-end/link checks only. Never dispatches or draws GPU work.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s EGL=%#x\n",#x,eglGetError());exit(1);} }while(0)
static unsigned checks;
static void probe(const char *name,const char *extension,const char *body,int expected) {
    const char *prefix="#version 310 es\n";
    const char *precision="precision highp float;precision highp int;\n";
    const char *parts[]={prefix,extension,precision,body};
    GLuint s=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(s,4,parts,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);GLuint p=0;
    if(ok){p=glCreateProgram();glAttachShader(p,s);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);}
    printf("%s=%s expected=%s\n",name,ok?"accepted":"rejected",expected?"accepted":"rejected");
    char log[2048];GLsizei n=0;
    if(p)glGetProgramInfoLog(p,sizeof(log),&n,log);
    else glGetShaderInfoLog(s,sizeof(log),&n,log);
    if(n)printf("log: %s\n",log);
    CHECK(!!ok==expected);CHECK(glGetError()==GL_NO_ERROR);if(p)glDeleteProgram(p);glDeleteShader(s);checks++;
}
int main(void) {
    int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(fd>=0);
    struct gbm_device *g=gbm_create_device(fd);CHECK(g);
    EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,g,NULL);CHECK(d!=EGL_NO_DISPLAY&&eglInitialize(d,NULL,NULL));CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,a);CHECK(c!=EGL_NO_CONTEXT);CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
    printf("renderer=%s\nversion=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    probe("fp32_int32","","layout(local_size_x=16) in;layout(std430,binding=0) buffer B{float v[];};void main(){uint i=gl_GlobalInvocationID.x;v[i]=sin(v[i])+sqrt(abs(v[i]))+float((i*123456789u)^i);}",1);
    probe("fp64","#extension GL_ARB_gpu_shader_fp64 : require\n","layout(local_size_x=1) in;layout(std430,binding=0) buffer B{double v[];};void main(){v[0]=v[1]*v[2];}",0);
    probe("int64","#extension GL_ARB_gpu_shader_int64 : require\n","layout(local_size_x=1) in;layout(std430,binding=0) buffer B{uint64_t v[];};void main(){v[0]=v[1]+v[2];}",0);
    probe("fp16_explicit","#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n","layout(local_size_x=1) in;layout(std430,binding=0) buffer B{float16_t v[];};void main(){v[0]=v[1]+v[2];}",0);
    probe("float_atomic_add","#extension GL_EXT_shader_atomic_float : require\n","layout(local_size_x=1) in;layout(std430,binding=0) buffer B{float v[];};void main(){atomicAdd(v[0],1.0);}",0);
    probe("subgroups","#extension GL_KHR_shader_subgroup_basic : require\n","layout(local_size_x=16) in;layout(std430,binding=0) buffer B{uint v[];};void main(){v[gl_GlobalInvocationID.x]=gl_SubgroupSize;}",0);
    probe("local_128","","layout(local_size_x=128) in;layout(std430,binding=0) buffer B{uint v[];};void main(){v[gl_GlobalInvocationID.x]=gl_LocalInvocationIndex;}",1);
    probe("local_129","","layout(local_size_x=129) in;layout(std430,binding=0) buffer B{uint v[];};void main(){v[gl_GlobalInvocationID.x]=gl_LocalInvocationIndex;}",0);
    probe("shared_16384","","layout(local_size_x=16) in;shared uint s[4096];layout(std430,binding=0) buffer B{uint v[];};void main(){uint i=gl_LocalInvocationIndex;s[(i*257u)%4096u]=i;barrier();v[i]=s[(i+1u)%4096u];}",1);
    probe("shared_16388","","layout(local_size_x=16) in;shared uint s[4097];layout(std430,binding=0) buffer B{uint v[];};void main(){uint i=gl_LocalInvocationIndex;s[(i*257u)%4097u]=i;barrier();v[i]=s[(i+1u)%4097u];}",0);
    probe("recursion","","layout(local_size_x=1) in;layout(std430,binding=0) buffer B{float v[];};float f(float x);float f(float x){return x<1.0?x:f(x-1.0)+1.0;}void main(){v[0]=f(v[1]);}",0);
    CHECK(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));CHECK(eglDestroyContext(d,c));CHECK(eglTerminate(d));gbm_device_destroy(g);close(fd);
    printf("RESULT=PASS checks=%u dispatches=0\n",checks);return 0;
}
