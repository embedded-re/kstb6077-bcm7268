// Image format conversion, independently checked by native texelFetch.
// MESA_GLES_VERSION_OVERRIDE=3.1 kstb-image-format-test FORMAT [W H]
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <fcntl.h>
#include <gbm.h>
#include <limits.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char *vs =
    "#version 310 es\n"
    "void main() { gl_Position = vec4(gl_VertexID == 1 ? 3.0 : -1.0,\n"
    " gl_VertexID == 2 ? 3.0 : -1.0, 0.0, 1.0); }\n";

enum kind { UINT_KIND, SINT_KIND, FLOAT_KIND, UNORM_KIND, SNORM_KIND };
struct format { const char *name; GLenum internal; unsigned bits, channels; enum kind kind; };
static const struct format formats[] = {
    {"r32ui",GL_R32UI,32,1,UINT_KIND}, {"r32i",GL_R32I,32,1,SINT_KIND},
    {"r32f",GL_R32F,32,1,FLOAT_KIND}, {"rgba32ui",GL_RGBA32UI,32,4,UINT_KIND},
    {"rgba32i",GL_RGBA32I,32,4,SINT_KIND}, {"rgba32f",GL_RGBA32F,32,4,FLOAT_KIND},
    {"rgba16ui",GL_RGBA16UI,16,4,UINT_KIND}, {"rgba16i",GL_RGBA16I,16,4,SINT_KIND},
    {"rgba16f",GL_RGBA16F,16,4,FLOAT_KIND}, {"rgba8ui",GL_RGBA8UI,8,4,UINT_KIND},
    {"rgba8i",GL_RGBA8I,8,4,SINT_KIND}, {"rgba8",GL_RGBA8,8,4,UNORM_KIND},
    {"rgba8_snorm",GL_RGBA8_SNORM,8,4,SNORM_KIND},
    {"rg32f",GL_RG32F,32,2,FLOAT_KIND}, {"rg16f",GL_RG16F,16,2,FLOAT_KIND},
    {"r11f_g11f_b10f",GL_R11F_G11F_B10F,11,3,FLOAT_KIND}, {"r16f",GL_R16F,16,1,FLOAT_KIND},
    {"rgb10_a2ui",GL_RGB10_A2UI,10,4,UINT_KIND},
    {"rg32ui",GL_RG32UI,32,2,UINT_KIND}, {"rg16ui",GL_RG16UI,16,2,UINT_KIND},
    {"rg8ui",GL_RG8UI,8,2,UINT_KIND}, {"r16ui",GL_R16UI,16,1,UINT_KIND},
    {"r8ui",GL_R8UI,8,1,UINT_KIND}, {"rg32i",GL_RG32I,32,2,SINT_KIND},
    {"rg16i",GL_RG16I,16,2,SINT_KIND}, {"rg8i",GL_RG8I,8,2,SINT_KIND},
    {"r16i",GL_R16I,16,1,SINT_KIND}, {"r8i",GL_R8I,8,1,SINT_KIND},
    {"rgb10_a2",GL_RGB10_A2,10,4,UNORM_KIND},
    {"rg8",GL_RG8,8,2,UNORM_KIND}, {"r8",GL_R8,8,1,UNORM_KIND},
    {"rg8_snorm",GL_RG8_SNORM,8,2,SNORM_KIND}, {"r8_snorm",GL_R8_SNORM,8,1,SNORM_KIND},
};
static const uint32_t salts[4] = {0xdecafbad,0x13579bdf,0x8642feca,0x10293847};
static const float adds[4] = {0.5f,1.0f,-0.5f,2.0f};
static uint32_t fbits(float f) { uint32_t u; memcpy(&u,&f,4); return u; }
static float asfloat(uint32_t u) { float f; memcpy(&f,&u,4); return f; }
static uint32_t mask(const struct format *fmt) { return fmt->bits==32?UINT32_MAX:(1u<<fmt->bits)-1; }
static uint32_t cmask(const struct format *fmt, unsigned c)
{
    return (fmt->internal==GL_RGB10_A2||fmt->internal==GL_RGB10_A2UI)&&c==3?3:mask(fmt);
}
static uint32_t raw(const struct format *fmt, uint32_t i, unsigned c)
{
    return ((i*4+c)*1664525u+1013904223u)&cmask(fmt,c);
}
static int32_t sint(uint32_t value, unsigned bits)
{
    if (bits==8) return (int8_t)value;
    if (bits==16) return (int16_t)value;
    return (int32_t)value;
}
static float fp(const struct format *fmt, uint32_t i, unsigned c, int stored)
{
    if (fmt->kind==FLOAT_KIND) {
        float v=fmt->internal==GL_R11F_G11F_B10F?
                ((i+c*7)%32)*0.125f:((int)(i%128)*4+(int)c-256)*0.125f;
        v+=stored?adds[c]:0.0f;
        return fmt->internal==GL_R11F_G11F_B10F&&v<0?0:v;
    }
    if (fmt->kind==UNORM_KIND) {
        uint32_t u=raw(fmt,i,c);
        uint32_t max=cmask(fmt,c);
        return (float)(stored?max-u:u)/(float)max;
    }
    int32_t v=sint(raw(fmt,i,c),8);
    if (v < -127) v=-127;
    return (float)(stored?-v:v)/127.0f;
}
static uint32_t expected(const struct format *fmt, uint32_t i, unsigned c, int stored)
{
    if (c>=fmt->channels)
        return fmt->kind>=FLOAT_KIND?fbits(c==3?1.0f:0.0f):(c==3?1:0);
    if (fmt->kind>=FLOAT_KIND) return fbits(fp(fmt,i,c,stored));
    uint32_t u=raw(fmt,i,c);
    if (stored) u=(u^salts[c])&cmask(fmt,c);
    return fmt->kind==SINT_KIND?(uint32_t)sint(u,fmt->bits):u;
}
static void fill(const struct format *fmt, void *pixels, size_t count)
{
    if (fmt->internal==GL_RGB10_A2||fmt->internal==GL_RGB10_A2UI) {
        for (uint32_t i=0;i<count;i++)
            ((uint32_t *)pixels)[i]=raw(fmt,i,0)|(raw(fmt,i,1)<<10)|
                                     (raw(fmt,i,2)<<20)|(raw(fmt,i,3)<<30);
        return;
    }
    for (uint32_t i=0;i<count;i++) for (unsigned c=0;c<fmt->channels;c++) {
        size_t k=(size_t)i*fmt->channels+c;
        if (fmt->kind==FLOAT_KIND) ((float *)pixels)[k]=fp(fmt,i,c,0);
        else {
            uint32_t u=raw(fmt,i,c);
            if (fmt->bits==32) ((uint32_t *)pixels)[k]=u;
            else if (fmt->bits==16) ((uint16_t *)pixels)[k]=u;
            else ((uint8_t *)pixels)[k]=u;
        }
    }
}
static size_t check(const struct format *fmt, const uint32_t *data, size_t count, int stored)
{
    size_t errors=0;
    for (uint32_t i=0;i<count;i++) for (unsigned c=0;c<4;c++) {
        uint32_t e=expected(fmt,i,c,stored),got=data[(size_t)i*4+c];
        int good=got==e;
        if (!good && (fmt->kind==UNORM_KIND||fmt->kind==SNORM_KIND)) {
            float delta=asfloat(got)-asfloat(e); if (delta<0) delta=-delta;
            good=delta <= (stored?0.0005f:0.0000004f);
        }
        if (!good) {
            if (errors<8) fprintf(stderr,"%s %s[%u].%u=%#x expected %#x\n",fmt->name,stored?"store/native-fetch":"image-load",i,c,got,e);
            errors++;
        }
    }
    return errors;
}
static GLuint program(const struct format *fmt, int fetch)
{
    char fs[8192],expr[1024];
    const char *prefix=fmt->kind==UINT_KIND?"u":fmt->kind==SINT_KIND?"i":"";
    const char *vec=fmt->kind==UINT_KIND?"uvec4":fmt->kind==SINT_KIND?"ivec4":"vec4";
    const char *convert=fmt->kind>=FLOAT_KIND?"floatBitsToUint(v)":"uvec4(v)";
    if (fmt->kind==FLOAT_KIND) strcpy(expr,"v+vec4(0.5,1.0,-0.5,2.0)");
    else if (fmt->kind==UNORM_KIND) strcpy(expr,"vec4(1.0)-v");
    else if (fmt->kind==SNORM_KIND) strcpy(expr,"-v");
    else if (fmt->kind==UINT_KIND)
        snprintf(expr,sizeof(expr),"(v^uvec4(0xdecafbadu,0x13579bdfu,0x8642fecau,0x10293847u))&uvec4(0x%xu,0x%xu,0x%xu,0x%xu)",cmask(fmt,0),cmask(fmt,1),cmask(fmt,2),cmask(fmt,3));
    else {
        unsigned shift=32-fmt->bits;
        snprintf(expr,sizeof(expr),"(ivec4((uvec4(v)^uvec4(0xdecafbadu,0x13579bdfu,0x8642fecau,0x10293847u))<<%uu)>>%uu)",shift,shift);
    }
    if (fetch)
        snprintf(fs,sizeof(fs),"#version 310 es\n#extension GL_NV_image_formats : enable\nprecision highp float; precision highp int;\n"
            "uniform highp %ssampler2D tex; uniform uint width;\n"
            "layout(std430,binding=0) writeonly buffer B { uvec4 result[]; }; out vec4 color;\n"
            "void main(){ ivec2 p=ivec2(gl_FragCoord.xy); %s v=texelFetch(tex,p,0);\n"
            "result[uint(p.y)*width+uint(p.x)]=%s; color=vec4(1.0); }\n",prefix,vec,convert);
    else
        snprintf(fs,sizeof(fs),"#version 310 es\n#extension GL_NV_image_formats : enable\nprecision highp float; precision highp int;\n"
            "layout(%s,binding=0) readonly uniform highp %simage2D src;\n"
            "layout(%s,binding=1) writeonly uniform highp %simage2D dst; uniform uint width;\n"
            "layout(std430,binding=0) writeonly buffer B { uvec4 result[]; }; out vec4 color;\n"
            "void main(){ ivec2 p=ivec2(gl_FragCoord.xy); %s v=imageLoad(src,p);\n"
            "result[uint(p.y)*width+uint(p.x)]=%s; imageStore(dst,p,%s); color=vec4(1.0); }\n",
            fmt->name,prefix,fmt->name,prefix,vec,convert,expr);
    GLuint p=glCreateProgram(); glAttachShader(p,shader(GL_VERTEX_SHADER,vs)); glAttachShader(p,shader(GL_FRAGMENT_SHADER,fs));
    glLinkProgram(p); GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok) {char log[4096];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(ok); return p;
}
int main(int argc,char **argv)
{
    CHECK(argc==2||argc==4);
    const struct format *fmt=NULL;
    for(unsigned i=0;i<sizeof(formats)/sizeof(formats[0]);i++) if(!strcmp(argv[1],formats[i].name)) fmt=&formats[i];
    CHECK(fmt);
    int w=argc==4?atoi(argv[2]):17,h=argc==4?atoi(argv[3]):13;
    CHECK(w>0&&w<=512&&h>0&&h<=512);
    size_t n=(size_t)w*h; uint32_t *pixels=malloc(n*16); CHECK(pixels);fill(fmt,pixels,n);
    int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(fd>=0);
    struct gbm_device *gbm=gbm_create_device(fd);CHECK(gbm);
    EGLDisplay dpy=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,gbm,NULL);
    CHECK(dpy!=EGL_NO_DISPLAY&&eglInitialize(dpy,NULL,NULL));CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    EGLContext ctx=eglCreateContext(dpy,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,ca);
    CHECK(ctx!=EGL_NO_CONTEXT&&eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
    printf("%s, %s %dx%d\n",glGetString(GL_VERSION),fmt->name,w,h);
    const GLenum integer_formats[4]={GL_RED_INTEGER,GL_RG_INTEGER,GL_RGB_INTEGER,GL_RGBA_INTEGER};
    const GLenum float_formats[4]={GL_RED,GL_RG,GL_RGB,GL_RGBA};
    GLenum format=(fmt->kind<=SINT_KIND?integer_formats:float_formats)[fmt->channels-1];
    GLenum type=(fmt->internal==GL_RGB10_A2||fmt->internal==GL_RGB10_A2UI)?GL_UNSIGNED_INT_2_10_10_10_REV:
        fmt->kind==FLOAT_KIND?GL_FLOAT:fmt->kind==SINT_KIND||fmt->kind==SNORM_KIND?
        (fmt->bits==32?GL_INT:fmt->bits==16?GL_SHORT:GL_BYTE):(fmt->bits==32?GL_UNSIGNED_INT:fmt->bits==16?GL_UNSIGNED_SHORT:GL_UNSIGNED_BYTE);
    GLuint tex[2];glGenTextures(2,tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=0;i<2;i++) {
        glBindTexture(GL_TEXTURE_2D,tex[i]);glTexStorage2D(GL_TEXTURE_2D,1,fmt->internal,w,h);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        if(!i) glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,format,type,pixels);
        glBindImageTexture(i,tex[i],0,GL_FALSE,0,i?GL_WRITE_ONLY:GL_READ_ONLY,fmt->internal);
    }
    CHECK(glGetError()==GL_NO_ERROR);
    GLuint buf;glGenBuffers(1,&buf);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buf);
    for(size_t i=0;i<n*4;i++) pixels[i]=0xdeadbeef;
    glBufferData(GL_SHADER_STORAGE_BUFFER,n*16,pixels,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buf);
    GLuint rb,fbo;glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,w,h);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,w,h);
    size_t errors=0;
    for(int pass=0;pass<2;pass++) {
        GLuint p=program(fmt,pass);glUseProgram(p);glUniform1ui(glGetUniformLocation(p,"width"),w);
        if(pass) {glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[1]);glUniform1i(glGetUniformLocation(p,"tex"),0);}
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,n*16,pixels);
        glDrawArrays(GL_TRIANGLES,0,3);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_BUFFER_UPDATE_BARRIER_BIT);glFinish();CHECK(glGetError()==GL_NO_ERROR);
        const uint32_t *data=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,n*16,GL_MAP_READ_BIT);CHECK(data);
        size_t e=check(fmt,data,n,pass);errors+=e;
        CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
        printf("%s %s: %s (%zu errors, %zu words)\n",fmt->name,pass?"store/native-fetch":"image-load",e?"FAIL":"verified",e,n*4);
        glDeleteProgram(p);
    }
    glDeleteTextures(2,tex);glDeleteBuffers(1,&buf);glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&rb);
    CHECK(eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));CHECK(eglDestroyContext(dpy,ctx)&&eglTerminate(dpy));
    gbm_device_destroy(gbm);close(fd);free(pixels);return errors?1:0;
}
