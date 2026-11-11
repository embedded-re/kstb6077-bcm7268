// Small CPU capability/correctness probes, not an optimized numerical library.
#define _GNU_SOURCE
#include <arm_neon.h>
#include <asm/hwcap.h>
#include <complex.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static double now(void) { struct timespec t; CHECK(!clock_gettime(CLOCK_MONOTONIC,&t)); return t.tv_sec+t.tv_nsec*1e-9; }
static __attribute__((noinline)) void neon_check(double a) {
    double bd[2]={2,4},od[2]; float bf[4]={2,4,6,8},of[4];
    vst1q_f64(od,vfmaq_f64(vdupq_n_f64(1),vdupq_n_f64(a),vld1q_f64(bd)));
    vst1q_f32(of,vfmaq_f32(vdupq_n_f32(1),vdupq_n_f32((float)a),vld1q_f32(bf)));
    for(int i=0;i<2;i++) CHECK(od[i]==fma(a,bd[i],1));
    for(int i=0;i<4;i++) CHECK(of[i]==fmaf((float)a,bf[i],1));
    puts("NEON_FP32_4lane_FMA=PASS\nNEON_FP64_2lane_FMA=PASS");
}
static void fft(double complex *a,unsigned n,int inverse) {
    for(unsigned i=1,j=0;i<n;i++) {
        unsigned b=n>>1;
        for(;j&b;b>>=1) j^=b;
        j^=b;
        if(i<j) { double complex t=a[i];a[i]=a[j];a[j]=t; }
    }
    for(unsigned len=2;len<=n;len*=2) {
        double angle=(inverse?2:-2)*acos(-1.0)/len;
        double complex step=cos(angle)+I*sin(angle);
        for(unsigned base=0;base<n;base+=len) {
            double complex w=1;
            for(unsigned j=0;j<len/2;j++) {
                double complex u=a[base+j],v=w*a[base+j+len/2];
                a[base+j]=u+v;a[base+j+len/2]=u-v;w*=step;
            }
        }
    }
    if(inverse) for(unsigned i=0;i<n;i++) a[i]/=n;
}
static void fft_check(void) {
    const unsigned n=65536,freq=37;
    double complex *a=malloc(n*sizeof(*a)),*ref=malloc(n*sizeof(*ref));CHECK(a&&ref);
    for(unsigned i=0;i<n;i++) { double x=2*acos(-1.0)*freq*i/n; a[i]=ref[i]=cos(x)+I*sin(x); }
    double t=now();fft(a,n,0);double forward=now()-t,maxbin=0;
    for(unsigned i=0;i<n;i++) maxbin=fmax(maxbin,cabs(a[i]-(i==freq?(double)n:0))/n);
    t=now();fft(a,n,1);double inverse=now()-t,maxerr=0;
    for(unsigned i=0;i<n;i++) maxerr=fmax(maxerr,cabs(a[i]-ref[i]));
    CHECK(maxbin<1e-10&&maxerr<1e-10);
    printf("FFT_FP64_N=%u\nFFT_forward_ms=%.6f\nFFT_inverse_ms=%.6f\nFFT_spectrum_relative_max=%.17g\nFFT_roundtrip_max=%.17g\n",n,forward*1000,inverse*1000,maxbin,maxerr);
    free(a);free(ref);
}
static void solve_check(void) {
    enum {N=96};double *a=calloc(N*N,sizeof(double)),*l=calloc(N*N,sizeof(double));
    double b[N],y[N],x[N];CHECK(a&&l);
    for(unsigned i=0;i<N;i++) {
        b[i]=0;
        for(unsigned j=0;j<N;j++) { a[i*N+j]=ldexp(1.0,-abs((int)i-(int)j));b[i]+=a[i*N+j]; }
    }
    double t=now();
    for(unsigned i=0;i<N;i++) for(unsigned j=0;j<=i;j++) {
        double s=a[i*N+j];for(unsigned k=0;k<j;k++) s-=l[i*N+k]*l[j*N+k];
        if(i==j) { CHECK(s>0);l[i*N+j]=sqrt(s); } else l[i*N+j]=s/l[j*N+j];
    }
    for(unsigned i=0;i<N;i++) { double s=b[i];for(unsigned j=0;j<i;j++)s-=l[i*N+j]*y[j];y[i]=s/l[i*N+i]; }
    for(int i=N-1;i>=0;i--) { double s=y[i];for(int j=i+1;j<N;j++)s-=l[j*N+i]*x[j];x[i]=s/l[i*N+i]; }
    double elapsed=now()-t,maxerr=0,residual=0;
    for(unsigned i=0;i<N;i++) { maxerr=fmax(maxerr,fabs(x[i]-1));double s=-b[i];for(unsigned j=0;j<N;j++)s+=a[i*N+j]*x[j];residual=fmax(residual,fabs(s)); }
    CHECK(maxerr<1e-10&&residual<1e-10);
    printf("Cholesky_FP64_N=%d\nCholesky_factor_solve_ms=%.6f\nCholesky_solution_max=%.17g\nCholesky_residual_max=%.17g\n",N,elapsed*1000,maxerr,residual);
    free(a);free(l);
}
int main(void) {
    unsigned long h=getauxval(AT_HWCAP),h2=getauxval(AT_HWCAP2);uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr":"=r"(fpcr));
    printf("online_cpus=%ld\nHWCAP=%#lx\nHWCAP2=%#lx\nFPCR=%#llx\n",sysconf(_SC_NPROCESSORS_ONLN),h,h2,(unsigned long long)fpcr);
#define CAP(n) printf(#n "=%d\n",!!(h&(n)))
    CAP(HWCAP_FP);CAP(HWCAP_ASIMD);CAP(HWCAP_FPHP);CAP(HWCAP_ASIMDHP);CAP(HWCAP_ASIMDDP);CAP(HWCAP_SVE);CAP(HWCAP_ATOMICS);CAP(HWCAP_AES);CAP(HWCAP_PMULL);CAP(HWCAP_SHA1);CAP(HWCAP_SHA2);CAP(HWCAP_CRC32);
#ifdef HWCAP2_BF16
    printf("HWCAP2_BF16=%d\n",!!(h2&HWCAP2_BF16));
#endif
#ifdef HWCAP2_I8MM
    printf("HWCAP2_I8MM=%d\n",!!(h2&HWCAP2_I8MM));
#endif
    printf("sizeof_float=%zu\nsizeof_double=%zu\nsizeof_long_double=%zu\nFLT_MANT_DIG=%d\nDBL_MANT_DIG=%d\nLDBL_MANT_DIG=%d\n",sizeof(float),sizeof(double),sizeof(long double),FLT_MANT_DIG,DBL_MANT_DIG,LDBL_MANT_DIG);
    CHECK((h&HWCAP_FP)&&(h&HWCAP_ASIMD));neon_check(strtod("1.25",NULL));
    volatile float tiny=FLT_MIN;float sub=tiny*0.5f;uint32_t bits;memcpy(&bits,&sub,4);CHECK(bits==0x00400000);
    printf("CPU_FP32_subnormal_bits=%#x\n",bits);
    volatile long double one=1,small=ldexpl(1,-100);long double q=one+small;CHECK(q>one&&LDBL_MANT_DIG==113);
    puts("software_binary128_1_plus_2pow_minus100=PASS");
    volatile unsigned long long lo=3;unsigned __int128 big=((unsigned __int128)1<<100)+lo;CHECK(big*3-9==((unsigned __int128)3<<100));
    puts("integer128_arithmetic=PASS");
    _Atomic uint64_t counter=UINT64_C(0x100000000);
    CHECK(atomic_fetch_add(&counter,7)==UINT64_C(0x100000000));
    CHECK(atomic_load(&counter)==UINT64_C(0x100000007));
    printf("CPU_atomic64=PASS lock_free:%d\n",atomic_is_lock_free(&counter));
    fft_check();solve_check();puts("RESULT=PASS");return 0;
}
