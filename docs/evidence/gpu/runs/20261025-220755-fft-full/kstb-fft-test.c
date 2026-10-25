// Bounded batched FP32 complex FFT on V3D33, versus FFTW float1/float4.
// Independent FFTW double reference, forward/inverse and SSBO guards checked.
// kstb-fft-test N batch warm_rounds; N power of 2 in [128,1024], batch<=64.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <fftw3.h>
#include <gbm.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define GUARD 4096
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static double now(void) {
    struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));
    return t.tv_sec+t.tv_nsec*1e-9;
}
static unsigned number(const char *s, unsigned max) {
    char *e; unsigned long n=strtoul(s,&e,10);
    CHECK(*s && !*e && n && n<=max); return n;
}
static double median(double *v, unsigned n) {
    for(unsigned i=1;i<n;i++) for(unsigned j=i;j && v[j]<v[j-1];j--) {
        double t=v[j];v[j]=v[j-1];v[j-1]=t;
    }
    return v[n/2];
}
static GLuint program(unsigned n, unsigned logn) {
    char text[8192];
    // One workgroup per FFT. Each butterfly owns two elements; all lanes reach
    // every barrier, including lanes with no butterflies at smaller stages.
    snprintf(text,sizeof(text),
        "#version 310 es\nprecision highp float;precision highp int;\n"
        "#define N %uu\n#define LOGN %uu\n"
        "layout(local_size_x=128) in;uniform int inverse;"
        "layout(std430,binding=0) readonly buffer A{vec2 src[];};"
        "layout(std430,binding=1) writeonly buffer B{vec2 dst[];};"
        "layout(std430,binding=2) readonly buffer T{vec2 twiddle[];};"
        "shared vec2 s[N];"
        "void main(){uint lane=gl_LocalInvocationIndex,base=gl_WorkGroupID.x*N;"
        "for(uint i=lane;i<N;i+=128u)s[bitfieldReverse(i)>>(32u-LOGN)]=src[base+i];"
        "barrier();"
        "for(uint len=2u;len<=N;len*=2u){uint halfLen=len/2u;"
        "for(uint p=lane;p<N/2u;p+=128u){"
        "uint j=p%%halfLen,a=(p/halfLen)*len+j,b=a+halfLen;"
        "vec2 w=twiddle[j*(N/len)];if(inverse!=0)w.y=-w.y;"
        "vec2 u=s[a],v=s[b];vec2 z=vec2(v.x*w.x-v.y*w.y,v.x*w.y+v.y*w.x);"
        "s[a]=u+z;s[b]=u-z;}barrier();}"
        "for(uint i=lane;i<N;i+=128u)dst[base+i]=s[i]*(inverse!=0?1.0/float(N):1.0);}",n,logn);
    const char *src=text;GLuint s=glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(s,1,&src,NULL);glCompileShader(s);GLint ok;
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[8192];glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr); }
    CHECK(ok);GLuint p=glCreateProgram();glAttachShader(p,s);glLinkProgram(p);glDeleteShader(s);
    glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) { char log[8192];glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr); }
    CHECK(ok);return p;
}
static void fill(fftwf_complex *a,unsigned n,unsigned batch,unsigned round) {
    uint32_t state=0x1234567u+round*97;
    for(unsigned b=0;b<batch;b++) for(unsigned i=0;i<n;i++) {
        double angle=2*acos(-1.0)*(3+b*13)*i/n;
        for(unsigned c=0;c<2;c++) {
            state^=state<<13;state^=state>>17;state^=state<<5;
            float random=((int)(state&65535)-32768)/32768.0f;
            unsigned mode=(b+round)%4;
            a[b*n+i][c]=mode==0 ? (i==0 && c==0 ? 1.0f : 0.0f) :
                mode==1 ? (c==0 ? cos(angle):sin(angle)) :
                mode==2 ? random : 0.125f+0.3f*(c==0 ? cos(angle):sin(angle))+0.1f*random;
        }
    }
}
static double compare(const fftwf_complex *got,const fftw_complex *ref,size_t count) {
    double error=0,power=0,peak=0,maxerr=0;
    for(size_t i=0;i<count;i++) for(unsigned c=0;c<2;c++) {
        CHECK(isfinite(got[i][c]));double d=got[i][c]-ref[i][c];
        error+=d*d;power+=ref[i][c]*ref[i][c];peak=fmax(peak,fabs(ref[i][c]));maxerr=fmax(maxerr,fabs(d));
    }
    double relative=sqrt(error/fmax(power,1e-30));
    CHECK(relative<3e-5 && maxerr<3e-5*(1+peak));return relative;
}
static void read_buffer(GLuint b,void *out,size_t payload) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,b);
    size_t bytes=GUARD+payload+GUARD;
    const unsigned char *p=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,bytes,GL_MAP_READ_BIT);CHECK(p);
    for(unsigned i=0;i<GUARD;i++)CHECK(p[i]==0xa5 && p[GUARD+payload+i]==0xa5);
    if(out)memcpy(out,p+GUARD,payload);
    CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
}
static void dispatch(GLuint program,GLuint a,GLuint b,GLuint tw,size_t bytes,unsigned batch,int inverse) {
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"inverse"),inverse);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,a,GUARD,bytes);
    glBindBufferRange(GL_SHADER_STORAGE_BUFFER,1,b,GUARD,bytes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,tw);
    glDispatchCompute(batch,1,1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);
    glFinish();CHECK(glGetError()==GL_NO_ERROR);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);CHECK(argc==4);
    unsigned n=number(argv[1],1024),batch=number(argv[2],64),rounds=number(argv[3],15),logn=0;
    CHECK(n>=128 && !(n&(n-1)));for(unsigned t=n;t>1;t>>=1)logn++;
    size_t count=(size_t)n*batch,payload=count*sizeof(fftwf_complex),bytes=GUARD+payload+GUARD;
    fftwf_complex *in=fftwf_malloc(payload),*cpu=fftwf_malloc(payload),*got=fftwf_malloc(payload);
    fftw_complex *din=fftw_malloc(count*sizeof(*din)),*reference=fftw_malloc(count*sizeof(*reference));
    CHECK(in && cpu && got && din && reference && fftwf_init_threads());
    int dim=(int)n;
    fftwf_plan_with_nthreads(1);
    fftwf_plan f1=fftwf_plan_many_dft(1,&dim,batch,in,NULL,1,n,cpu,NULL,1,n,FFTW_FORWARD,FFTW_ESTIMATE);
    fftwf_plan_with_nthreads(4);
    fftwf_plan f4=fftwf_plan_many_dft(1,&dim,batch,in,NULL,1,n,cpu,NULL,1,n,FFTW_FORWARD,FFTW_ESTIMATE);
    fftw_plan d=fftw_plan_many_dft(1,&dim,batch,din,NULL,1,n,reference,NULL,1,n,FFTW_FORWARD,FFTW_ESTIMATE);
    CHECK(f1 && f4 && d);
    int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(fd>=0);
    struct gbm_device *g=gbm_create_device(fd);CHECK(g);
    EGLDisplay display=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,g,NULL);CHECK(display!=EGL_NO_DISPLAY && eglInitialize(display,NULL,NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint attrs[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    EGLContext ctx=eglCreateContext(display,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,attrs);
    CHECK(ctx!=EGL_NO_CONTEXT && eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
    printf("renderer=%s\nversion=%s\nN=%u batch=%u warm_rounds=%u CPU_timing_repeats=32\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),n,batch,rounds);
    CHECK(strstr((const char *)glGetString(GL_RENDERER),"V3D 3.3"));
    GLuint p=program(n,logn),buffers[3];glGenBuffers(3,buffers);
    unsigned char *initial=malloc(bytes);CHECK(initial);memset(initial,0xa5,bytes);
    for(unsigned i=0;i<2;i++) {glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[i]);glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,initial,GL_DYNAMIC_COPY);}
    free(initial);
    fftwf_complex *twiddle=fftwf_malloc(n/2*sizeof(*twiddle));CHECK(twiddle);
    for(unsigned i=0;i<n/2;i++) {double angle=-2*acos(-1.0)*i/n;twiddle[i][0]=cos(angle);twiddle[i][1]=sin(angle);}
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[2]);glBufferData(GL_SHADER_STORAGE_BUFFER,n/2*sizeof(*twiddle),twiddle,GL_STATIC_DRAW);fftwf_free(twiddle);
    CHECK(glGetError()==GL_NO_ERROR);glFinish();
    double cpu1[15],cpu4[15],gpu[15],transfer[15],worst=0,roundtrip=0;
    for(unsigned r=0;r<=rounds;r++) {
        fill(in,n,batch,r);
        for(size_t i=0;i<count;i++) for(unsigned c=0;c<2;c++) din[i][c]=in[i][c];
        fftw_execute(d);
        double t=now();for(unsigned k=0;k<32;k++)fftwf_execute(f1);double c1=(now()-t)/32;
        worst=fmax(worst,compare(cpu,reference,count));
        t=now();for(unsigned k=0;k<32;k++)fftwf_execute(f4);double c4=(now()-t)/32;
        worst=fmax(worst,compare(cpu,reference,count));
        glFinish();double start=now();glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,GUARD,payload,in);double uploaded=now();
        dispatch(p,buffers[0],buffers[1],buffers[2],payload,batch,0);double done=now();
        read_buffer(buffers[1],got,payload);double read=now();
        double err=compare(got,reference,count);worst=fmax(worst,err);
        // Inverse consumes GPU-produced output, testing the dependent shader path.
        dispatch(p,buffers[1],buffers[0],buffers[2],payload,batch,1);
        read_buffer(buffers[0],got,payload);
        double back=0;
        for(size_t i=0;i<count;i++) for(unsigned c=0;c<2;c++) {CHECK(isfinite(got[i][c]));back=fmax(back,fabs(got[i][c]-in[i][c]));}
        CHECK(back<3e-5);roundtrip=fmax(roundtrip,back);
        printf("round=%u cpu1_ms=%.6f cpu4_ms=%.6f gpu_resident_ms=%.6f gpu_transfer_ms=%.6f forward_relative_l2=%.9g inverse_max_abs=%.9g guards=PASS\n",r,c1*1000,c4*1000,(done-uploaded)*1000,(read-start)*1000,err,back);
        if(r) {cpu1[r-1]=c1*1000;cpu4[r-1]=c4*1000;gpu[r-1]=(done-uploaded)*1000;transfer[r-1]=(read-start)*1000;}
    }
    printf("MEDIAN cpu1_ms=%.6f cpu4_ms=%.6f gpu_resident_ms=%.6f gpu_transfer_ms=%.6f\n",median(cpu1,rounds),median(cpu4,rounds),median(gpu,rounds),median(transfer,rounds));
    printf("RESULT=PASS forward_relative_l2_worst=%.9g inverse_max_abs_worst=%.9g dispatches=%u\n",worst,roundtrip,2*(rounds+1));
    glDeleteBuffers(3,buffers);glDeleteProgram(p);
    CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));CHECK(eglDestroyContext(display,ctx));CHECK(eglTerminate(display));gbm_device_destroy(g);close(fd);
    fftwf_destroy_plan(f1);fftwf_destroy_plan(f4);fftw_destroy_plan(d);fftwf_cleanup_threads();
    fftwf_free(in);fftwf_free(cpu);fftwf_free(got);fftw_free(din);fftw_free(reference);
    return 0;
}
