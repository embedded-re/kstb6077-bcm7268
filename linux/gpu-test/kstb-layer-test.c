// Render into each face/layer of a cube, 2D array or 3D texture, then check
// every texel twice: glReadPixels from the layer's FBO, and sampling the
// layer into a 2D target. Each texel holds (layer + 1, x, y, 7).
//   kstb-layer-test cube|array|3d f|ui [size] [interleave] [srctex]
// f = RGBA32F, ui = RGBA32UI. interleave samples each layer right after
// rendering it (as dEQP fbo.color does) instead of after all layers; srctex
// makes the fill shader also sample a white RGBA8 2D texture (LINEAR) and
// multiply by it, as dEQP's texture-to-FBO shader does.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s (EGL 0x%x, GL 0x%x)\n", \
            __LINE__, #x, eglGetError(), glGetError()); exit(1); \
} } while (0)

#define LAYERS 6

static const GLenum faces[LAYERS] = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

static GLuint program(const char *vs, const char *fs)
{
    const char *src[2] = { vs, fs };
    GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint p = glCreateProgram();
    for (int i = 0; i < 2; i++) {
        GLuint sh = glCreateShader(type[i]);
        glShaderSource(sh, 1, &src[i], NULL);
        glCompileShader(sh);
        GLint ok;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(sh, sizeof(log), NULL, log);
            fprintf(stderr, "shader compile: %s\n", log);
            exit(1);
        }
        glAttachShader(p, sh);
    }
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    CHECK(ok);
    return p;
}

static const char *vs =
    "#version 300 es\n"
    "void main() {\n"
    "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "  gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static int is_ui;
static int size;

static void attach(GLenum target, GLuint tex, int layer)
{
    if (target == GL_TEXTURE_CUBE_MAP)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faces[layer], tex, 0);
    else
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, layer);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
}

// Reads the bound FBO and compares with (layer + 1, x, y, 7). Returns the
// number of wrong texels and prints the first few.
static int check(const char *how, int layer)
{
    int n = size * size, bad = 0;
    GLuint *u = malloc(n * 16);
    GLfloat *f = (GLfloat *)u;
    if (is_ui)
        glReadPixels(0, 0, size, size, GL_RGBA_INTEGER, GL_UNSIGNED_INT, u);
    else
        glReadPixels(0, 0, size, size, GL_RGBA, GL_FLOAT, f);
    CHECK(glGetError() == GL_NO_ERROR);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int i = (y * size + x) * 4;
            double v[4];
            for (int c = 0; c < 4; c++)
                v[c] = is_ui ? u[i + c] : f[i + c];
            if (v[0] == layer + 1 && v[1] == x && v[2] == y && v[3] == 7)
                continue;
            if (bad++ < 4)
                printf("  %s layer %d texel %d,%d: %g %g %g %g\n",
                       how, layer, x, y, v[0], v[1], v[2], v[3]);
        }
    }
    free(u);
    printf("%s layer %d: %d of %d texels wrong\n", how, layer, bad, n);
    return bad;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s cube|array|3d f|ui [size] [interleave]\n", argv[0]);
        return 2;
    }
    GLenum target = !strcmp(argv[1], "cube") ? GL_TEXTURE_CUBE_MAP :
                    !strcmp(argv[1], "array") ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_3D;
    is_ui = !strcmp(argv[2], "ui");
    size = argc > 3 ? atoi(argv[3]) : 128;
    int interleave = 0, srctex = 0;
    for (int i = 4; i < argc; i++) {
        interleave |= !strcmp(argv[i], "interleave");
        srctex |= !strcmp(argv[i], "srctex");
    }

    int fd = open("/dev/dri/renderD128", O_RDWR);
    CHECK(fd >= 0);
    struct gbm_device *gbm = gbm_create_device(fd);
    CHECK(gbm);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL));
    CHECK(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    CHECK(ctx != EGL_NO_CONTEXT);
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));

    GLenum ifmt = is_ui ? GL_RGBA32UI : GL_RGBA32F;
    GLenum fmt = is_ui ? GL_RGBA_INTEGER : GL_RGBA;
    GLenum type = is_ui ? GL_UNSIGNED_INT : GL_FLOAT;
    const char *outtype = is_ui ? "uvec4" : "vec4";
    const char *prec = is_ui ? "precision highp usampler2DArray; precision highp usampler3D; "
                               "precision highp usamplerCube;\n"
                             : "precision highp sampler2DArray; precision highp sampler3D;\n";
    const char *samp = target == GL_TEXTURE_CUBE_MAP ? "samplerCube" :
                       target == GL_TEXTURE_2D_ARRAY ? "sampler2DArray" : "sampler3D";
    char fs_fill[512], fs_sample[2048];
    snprintf(fs_fill, sizeof(fs_fill),
        "#version 300 es\nprecision highp float;\nuniform int layer;\n"
        "uniform sampler2D src;\nuniform float size;\nout highp %s o;\n"
        "void main() { o = %s(vec4(float(layer + 1), floor(gl_FragCoord.xy), 7.0)%s); }\n",
        outtype, outtype, srctex ? " * texture(src, gl_FragCoord.xy / size).a" : "");
    // Cube: the direction through texel (x, y) of the face, per the GL
    // face table (s = sc, t = tc). Array/3D: texelFetch.
    snprintf(fs_sample, sizeof(fs_sample),
        "#version 300 es\nprecision highp float;\nprecision highp int;\n%s"
        "uniform highp %s%s tex;\nuniform int layer;\nuniform float size;\n"
        "out highp %s o;\n"
        "void main() {\n"
        "  ivec2 c = ivec2(gl_FragCoord.xy);\n"
        "#ifdef CUBE\n"
        "  vec2 st = (gl_FragCoord.xy * 2.0) / size - 1.0;\n"
        "  float s = st.x, t = st.y;\n"
        "  vec3 d = layer == 0 ? vec3(1.0, -t, -s) : layer == 1 ? vec3(-1.0, -t, s) :\n"
        "           layer == 2 ? vec3(s, 1.0, t) : layer == 3 ? vec3(s, -1.0, -t) :\n"
        "           layer == 4 ? vec3(s, -t, 1.0) : vec3(-s, -t, -1.0);\n"
        "  o = texture(tex, d);\n"
        "#else\n"
        "  o = texelFetch(tex, ivec3(c, layer), 0);\n"
        "#endif\n"
        "}\n",
        prec, is_ui ? "u" : "", samp, outtype);
    char fs_sample_full[2100];
    snprintf(fs_sample_full, sizeof(fs_sample_full), "%s", fs_sample);
    if (target == GL_TEXTURE_CUBE_MAP) {
        // #define after the #version line
        char *nl = strchr(fs_sample, '\n');
        snprintf(fs_sample_full, sizeof(fs_sample_full), "%.*s\n#define CUBE\n%s",
                 (int)(nl - fs_sample), fs_sample, nl + 1);
    }
    GLuint fill = program(vs, fs_fill);
    GLuint sample = program(vs, fs_sample_full);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(target, tex);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (target == GL_TEXTURE_CUBE_MAP)
        for (int i = 0; i < LAYERS; i++)
            glTexImage2D(faces[i], 0, ifmt, size, size, 0, fmt, type, NULL);
    else
        glTexImage3D(target, 0, ifmt, size, size, LAYERS, 0, fmt, type, NULL);
    CHECK(glGetError() == GL_NO_ERROR);

    GLuint src;
    glGenTextures(1, &src);
    glBindTexture(GL_TEXTURE_2D, src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLubyte *white = malloc(size * size * 4);
    memset(white, 255, size * size * 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    free(white);

    GLuint dst;
    glGenTextures(1, &dst);
    glBindTexture(GL_TEXTURE_2D, dst);
    glTexStorage2D(GL_TEXTURE_2D, 1, ifmt, size, size);
    GLuint fbo[LAYERS], dfbo;
    glGenFramebuffers(LAYERS, fbo);
    glGenFramebuffers(1, &dfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, dfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, size, size);

    int bad_read = 0, bad_sample = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < LAYERS; i++) {
            if (pass == 0) {
                glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
                attach(target, tex, i);
                glUseProgram(fill);
                glUniform1i(glGetUniformLocation(fill, "layer"), i);
                glUniform1f(glGetUniformLocation(fill, "size"), size);
                glUniform1i(glGetUniformLocation(fill, "src"), 1);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, src);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                CHECK(glGetError() == GL_NO_ERROR);
                if (!interleave)
                    continue;
            }
            if (pass == 1 && interleave)
                break;
            glBindFramebuffer(GL_FRAMEBUFFER, dfbo);
            glUseProgram(sample);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(target, tex);
            glUniform1i(glGetUniformLocation(sample, "tex"), 0);
            glUniform1i(glGetUniformLocation(sample, "layer"), i);
            glUniform1f(glGetUniformLocation(sample, "size"), size);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            CHECK(glGetError() == GL_NO_ERROR);
            bad_sample += check("sample", i);
        }
    }
    for (int i = 0; i < LAYERS; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        bad_read += check("read", i);
    }
    printf("%s %s %d%s%s: read %d wrong, sample %d wrong: %s\n", argv[1], argv[2], size,
           interleave ? " interleave" : "", srctex ? " srctex" : "", bad_read, bad_sample,
           bad_read || bad_sample ? "FAIL" : "PASS");
    return bad_read || bad_sample;
}
