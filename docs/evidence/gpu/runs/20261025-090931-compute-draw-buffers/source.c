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


static const char *source = "#version 310 es\n"
"\n"
"precision highp int;\n"
"precision highp float;\n"
"\n"
"struct DrawArraysIndirectCommand {\n"
"    uint count;\n"
"    uint primCount;\n"
"    uint first;\n"
"    uint reservedMustBeZero;\n"
"};\n"
"\n"
"layout(local_size_x = 1, local_size_y = 1) in;\n"
"layout(std430) buffer;\n"
"\n"
"layout(binding = 0) writeonly buffer CommandBuffer {\n"
"    DrawArraysIndirectCommand commands[];\n"
"};\n"
"layout(binding = 1) writeonly buffer DataBuffer {\n"
"    vec4 attribs[];\n"
"};\n"
"\n"
"void main() {\n"
"    const uint gridSize      = 8u;\n"
"    const uint triangleCount = gridSize * gridSize * 2u;\n"
"\n"
"    // command\n"
"    if (gl_GlobalInvocationID.x < 1u && gl_GlobalInvocationID.y == 0u && gl_GlobalInvocationID.z == 0u) {\n"
"        const uint numDrawCallTris = triangleCount / 1u;\n"
"        uint firstTri              = gl_GlobalInvocationID.x * numDrawCallTris;\n"
"\n"
"        commands[gl_GlobalInvocationID.x].count                 = numDrawCallTris*3u;\n"
"        commands[gl_GlobalInvocationID.x].primCount             = 1u;\n"
"        commands[gl_GlobalInvocationID.x].first                 = firstTri*3u;\n"
"        commands[gl_GlobalInvocationID.x].reservedMustBeZero    = 0u;\n"
"    }\n"
"\n"
"    // vertex attribs\n"
"    const vec4 yellow = vec4(1.0, 1.0, 0.0, 1.0);\n"
"    const vec4 green = vec4(0.0, 1.0, 0.0, 1.0);\n"
"    if (gl_GlobalInvocationID.x < gridSize && gl_GlobalInvocationID.y < gridSize && gl_GlobalInvocationID.z == 0u) {\n"
"        uint        y           = gl_GlobalInvocationID.x;\n"
"        uint        x           = gl_GlobalInvocationID.y;\n"
"        float       posX        = (float(x)    / float(gridSize)) * 2.0 - 1.0;\n"
"        float       posXp1      = (float(x+1u) / float(gridSize)) * 2.0 - 1.0;\n"
"        float       posY        = (float(y)    / float(gridSize)) * 2.0 - 1.0;\n"
"        float       posYp1      = (float(y+1u) / float(gridSize)) * 2.0 - 1.0;\n"
"        vec4        color       = ((x + y)%2u != 0u) ? (yellow) : (green);\n"
"\n"
"        attribs[((y * gridSize + x) * 6u + 0u) * 2u + 0u] = vec4(posX,   posY,   0.0, 1.0);\n"
"        attribs[((y * gridSize + x) * 6u + 1u) * 2u + 0u] = vec4(posXp1, posY,   0.0, 1.0);\n"
"        attribs[((y * gridSize + x) * 6u + 2u) * 2u + 0u] = vec4(posXp1, posYp1, 0.0, 1.0);\n"
"        attribs[((y * gridSize + x) * 6u + 3u) * 2u + 0u] = vec4(posX,   posY,   0.0, 1.0);\n"
"        attribs[((y * gridSize + x) * 6u + 4u) * 2u + 0u] = vec4(posXp1, posYp1, 0.0, 1.0);\n"
"        attribs[((y * gridSize + x) * 6u + 5u) * 2u + 0u] = vec4(posX,   posYp1, 0.0, 1.0);\n"
"\n"
"        attribs[((y * gridSize + x) * 6u + 0u) * 2u + 1u] = color;\n"
"        attribs[((y * gridSize + x) * 6u + 1u) * 2u + 1u] = color;\n"
"        attribs[((y * gridSize + x) * 6u + 2u) * 2u + 1u] = color;\n"
"        attribs[((y * gridSize + x) * 6u + 3u) * 2u + 1u] = color;\n"
"        attribs[((y * gridSize + x) * 6u + 4u) * 2u + 1u] = color;\n"
"        attribs[((y * gridSize + x) * 6u + 5u) * 2u + 1u] = color;\n"
"    }\n"
"\n"
"}\n";
int main(void) {
 int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);CHECK(fd>=0);
 struct gbm_device *gbm=gbm_create_device(fd);CHECK(gbm);
 EGLDisplay d=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,gbm,NULL);CHECK(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_ES_API));
 EGLint a[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
 EGLContext c=eglCreateContext(d,EGL_NO_CONFIG_KHR,EGL_NO_CONTEXT,a);CHECK(c!=EGL_NO_CONTEXT&&eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
 printf("%s; %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER));
 GLuint s=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);CHECK(ok);
 GLuint p=glCreateProgram();glAttachShader(p,s);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);glUseProgram(p);
 GLuint buffers[2];glGenBuffers(2,buffers);unsigned sizes[]={16,12288};
 for(unsigned k=0;k<2;k++) { unsigned char *v=malloc(sizes[k]+2*GUARD);CHECK(v);memset(v,0xa5,sizes[k]+2*GUARD);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[k]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizes[k]+2*GUARD,v,GL_DYNAMIC_COPY);free(v);glBindBufferRange(GL_SHADER_STORAGE_BUFFER,k,buffers[k],GUARD,sizes[k]); }
 glDispatchCompute(8,8,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glFinish();CHECK(glGetError()==GL_NO_ERROR);
 uint32_t cmd[]={384,1,0,0};float data[3072];
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) {
 float loX=x/4.0f-1,hiX=(x+1)/4.0f-1,loY=y/4.0f-1,hiY=(y+1)/4.0f-1;
 float points[6][2]={{loX,loY},{hiX,loY},{hiX,hiY},{loX,loY},{hiX,hiY},{loX,hiY}};
 for(unsigned v=0;v<6;v++) { unsigned at=((y*8+x)*6+v)*8; data[at]=points[v][0];data[at+1]=points[v][1];data[at+2]=0;data[at+3]=1;data[at+4]=(x+y)%2!=0;data[at+5]=1;data[at+6]=0;data[at+7]=1; }
 }
 unsigned errors=0;
 for(unsigned k=0;k<2;k++) {
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[k]);const unsigned char*m=glMapBufferRange(GL_SHADER_STORAGE_BUFFER,0,sizes[k]+2*GUARD,GL_MAP_READ_BIT);CHECK(m);
 const uint32_t *words=(const uint32_t*)(m+GUARD),*expected=k?(const uint32_t*)data:cmd;unsigned failures=0;
 for(unsigned i=0;i<sizes[k]/4;i++)if(words[i]!=expected[i]) {if(failures<16)printf("buffer %u word %u got %#x expected %#x\n",k,i,words[i],expected[i]);failures++;}
 for(unsigned i=0;i<GUARD;i++)if(m[i]!=0xa5||m[GUARD+sizes[k]+i]!=0xa5)failures++;
 printf("buffer %u: %u errors, guards checked\n",k,failures);errors+=failures;CHECK(glUnmapBuffer(GL_SHADER_STORAGE_BUFFER));
 }
 printf("%s\n",errors?"FAIL":"PASS");return errors?1:0;
}
