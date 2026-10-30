/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Native EGL smoke test. Requires the real eglGetProcAddress vector
 * and regenerated proto/egl.h + libEGL.a; a Khronos declaration alone is
 * insufficient. No linked GL entry points or renderer substitution.
 * Cases: a WINDOW surface in GLES2 and desktop GL; then, in GLES2, a
 * PBUFFER surface, a context current without a surface rendering to a
 * framebuffer object (SURFACELESS), a texture made in one context sampled
 * by a shader in a context sharing it (SHARED), and the EGL_EXTENSIONS
 * string against what egl.library implements (EXTENSIONS).
 */
#define DEBUG 1
#include <aros/debug.h>
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <EGL/egl.h>
#include <proto/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct Library *EGLBase;

/* Each line also goes to LOG_PATH, opened and closed per line so that the
 * host can read it while a case still runs or hangs. */
#define LOG_PATH "RAM:eglprobe.log"
static void log_line(const char *line)
{
    BPTR file = Open((CONST_STRPTR)LOG_PATH, MODE_READWRITE);

    if (file) {
        Seek(file, 0, OFFSET_END);
        Write(file, (APTR)line, strlen(line));
        Close(file);
    }
}

#define REPORT(fmt, ...) do { \
    char line_[512]; \
    snprintf(line_, sizeof(line_), "[EGLPROBE] " fmt "\n", ##__VA_ARGS__); \
    fputs(line_, stdout); \
    fflush(stdout); \
    log_line(line_); \
    bug("%s", line_); \
} while (0)

typedef const GLubyte *(GLAPIENTRYP GetStringFn)(GLenum);
typedef void (GLAPIENTRYP ClearColorFn)(GLclampf, GLclampf, GLclampf, GLclampf);
typedef void (GLAPIENTRYP ClearFn)(GLbitfield);
typedef void (GLAPIENTRYP ReadPixelsFn)(GLint, GLint, GLsizei, GLsizei,
                                      GLenum, GLenum, GLvoid *);
typedef GLenum (GLAPIENTRYP GetErrorFn)(void);

static void egl_failure(const char *name, const char *stage)
{
    REPORT("case=%s stage=%s result=FAIL egl_error=0x%04x",
           name, stage, (unsigned)eglGetError());
}

static int check_attribute(const char *name, EGLDisplay display,
                           EGLConfig config, EGLint attribute,
                           EGLint required, int bitmask)
{
    EGLint value = 0;
    if (!eglGetConfigAttrib(display, config, attribute, &value)) {
        egl_failure(name, "config-attribute");
        return 0;
    }
    REPORT("case=%s config_attribute=0x%04x value=%d required=%d",
           name, (unsigned)attribute, value, required);
    return bitmask ? (value & required) == required : value >= required;
}

static int gl_version_matches(const char *version, int gles)
{
    int major = 0, minor = 0;
    if (gles)
        return version && sscanf(version, "OpenGL ES %d.%d", &major, &minor) == 2
               && major >= 2 && minor >= 0;
    return version && sscanf(version, "%d.%d", &major, &minor) == 2
           && major >= 1 && minor >= 0;
}

static int run_case(int gles)
{
    const char *name = gles ? "GLES2" : "GL";
    EGLenum api = gles ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
    EGLint render_bit = gles ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT;
    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, render_bit,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    const EGLint es_context[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    const EGLint gl_context[] = { EGL_NONE };
    struct Window *window = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config;
    EGLint count = 0, major = 0, minor = 0, width = 0, height = 0;
    EGLint context_type = 0, context_version = 0;
    int initialized = 0, current = 0, ok = 0;
    unsigned char pixel[4] = { 17, 34, 51, 68 };
    GetStringFn get_string;
    ClearColorFn clear_color;
    ClearFn clear;
    ReadPixelsFn read_pixels;
    GetErrorFn get_error;
    const char *version, *renderer;
    GLenum error;

    REPORT("case=%s stage=begin api=0x%04x", name, (unsigned)api);
    window = OpenWindowTags(NULL,
        WA_Title, (IPTR)"EGL probe", WA_InnerWidth, 64, WA_InnerHeight, 64,
        WA_DragBar, TRUE, WA_Activate, FALSE, WA_SimpleRefresh, TRUE,
        WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!window) {
        REPORT("case=%s stage=window result=FAIL", name);
        goto cleanup;
    }
    REPORT("case=%s stage=window inner=%dx%d", name,
           window->Width - window->BorderLeft - window->BorderRight,
           window->Height - window->BorderTop - window->BorderBottom);
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        egl_failure(name, "get-display");
        goto cleanup;
    }
    if (!eglInitialize(display, &major, &minor)) {
        egl_failure(name, "initialize");
        goto cleanup;
    }
    initialized = 1;
    REPORT("case=%s stage=initialize egl=%d.%d", name, major, minor);
    if (major < 1 || (major == 1 && minor < 4)) {
        REPORT("case=%s stage=egl-version result=FAIL", name);
        goto cleanup;
    }
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count < 1) {
        egl_failure(name, "choose-config");
        goto cleanup;
    }
    REPORT("case=%s stage=choose-config matches=%d", name, count);
    if (!check_attribute(name, display, config, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, 1)
        || !check_attribute(name, display, config, EGL_RENDERABLE_TYPE, render_bit, 1)
        || !check_attribute(name, display, config, EGL_RED_SIZE, 8, 0)
        || !check_attribute(name, display, config, EGL_GREEN_SIZE, 8, 0)
        || !check_attribute(name, display, config, EGL_BLUE_SIZE, 8, 0)
        || !check_attribute(name, display, config, EGL_ALPHA_SIZE, 8, 0)) {
        REPORT("case=%s stage=config-contract result=FAIL", name);
        goto cleanup;
    }
    if (!eglBindAPI(api) || eglQueryAPI() != api) {
        egl_failure(name, "bind-api");
        goto cleanup;
    }
    surface = eglCreateWindowSurface(display, config,
                                     (EGLNativeWindowType)window, NULL);
    if (surface == EGL_NO_SURFACE) {
        egl_failure(name, "window-surface");
        goto cleanup;
    }
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width)
        || !eglQuerySurface(display, surface, EGL_HEIGHT, &height)
        || width != 64 || height != 64) {
        egl_failure(name, "surface-size");
        REPORT("case=%s surface=%dx%d required=64x64", name, width, height);
        goto cleanup;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                               gles ? es_context : gl_context);
    if (context == EGL_NO_CONTEXT) {
        egl_failure(name, "create-context");
        goto cleanup;
    }
    if (!eglQueryContext(display, context, EGL_CONTEXT_CLIENT_TYPE, &context_type)
        || (EGLenum)context_type != api
        || (gles && (!eglQueryContext(display, context,
                                     EGL_CONTEXT_CLIENT_VERSION, &context_version)
                     || context_version < 2))) {
        egl_failure(name, "context-api-version");
        goto cleanup;
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        egl_failure(name, "make-current");
        goto cleanup;
    }
    current = 1;
    if (eglGetCurrentContext() != context || eglGetCurrentDisplay() != display
        || eglGetCurrentSurface(EGL_DRAW) != surface
        || eglGetCurrentSurface(EGL_READ) != surface) {
        egl_failure(name, "current-identities");
        goto cleanup;
    }
    REPORT("case=%s stage=current surface=%dx%d", name, width, height);
    get_string = (GetStringFn)eglGetProcAddress("glGetString");
    clear_color = (ClearColorFn)eglGetProcAddress("glClearColor");
    clear = (ClearFn)eglGetProcAddress("glClear");
    read_pixels = (ReadPixelsFn)eglGetProcAddress("glReadPixels");
    get_error = (GetErrorFn)eglGetProcAddress("glGetError");
    REPORT("case=%s resolved get_string=%d clear_color=%d clear=%d read_pixels=%d get_error=%d",
           name, get_string != NULL, clear_color != NULL, clear != NULL,
           read_pixels != NULL, get_error != NULL);
    if (!get_string || !clear_color || !clear || !read_pixels || !get_error) {
        egl_failure(name, "resolve-gl");
        goto cleanup;
    }
    error = get_error();
    if (error != GL_NO_ERROR) {
        REPORT("case=%s stage=initial-gl-error result=FAIL gl_error=0x%04x",
               name, (unsigned)error);
        goto cleanup;
    }
    version = (const char *)get_string(GL_VERSION);
    error = get_error();
    REPORT("case=%s gl_version=%s gl_error=0x%04x", name,
           version ? version : "<NULL>", (unsigned)error);
    if (error != GL_NO_ERROR || !gl_version_matches(version, gles)) {
        REPORT("case=%s stage=actual-gl-api-version result=FAIL", name);
        goto cleanup;
    }
    renderer = (const char *)get_string(GL_RENDERER);
    error = get_error();
    REPORT("case=%s gl_renderer=%s gl_error=0x%04x", name,
           renderer ? renderer : "<NULL>", (unsigned)error);
    if (!renderer || error != GL_NO_ERROR) {
        REPORT("case=%s stage=renderer-identity result=FAIL", name);
        goto cleanup;
    }
    clear_color(1.0f, 0.0f, 0.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    error = get_error();
    if (error != GL_NO_ERROR) {
        REPORT("case=%s stage=clear result=FAIL gl_error=0x%04x", name, (unsigned)error);
        goto cleanup;
    }
    /* ReadPixels synchronizes rendering; read the back buffer before swapping. */
    read_pixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    error = get_error();
    REPORT("case=%s stage=read-pixel rgba=%u,%u,%u,%u expected=255,0,0,255 tolerance=1 gl_error=0x%04x",
           name, (unsigned)pixel[0], (unsigned)pixel[1], (unsigned)pixel[2],
           (unsigned)pixel[3], (unsigned)error);
    if (error != GL_NO_ERROR || pixel[0] < 254 || pixel[1] > 1
        || pixel[2] > 1 || pixel[3] < 254) {
        REPORT("case=%s stage=pixel-contract result=FAIL", name);
        goto cleanup;
    }
    ok = 1;

cleanup:
    if (current && !eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        egl_failure(name, "cleanup-unbind");
        ok = 0;
    }
    if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context)) {
        egl_failure(name, "cleanup-context");
        ok = 0;
    }
    if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface)) {
        egl_failure(name, "cleanup-surface");
        ok = 0;
    }
    if (initialized && !eglTerminate(display)) {
        egl_failure(name, "cleanup-terminate");
        ok = 0;
    }
    if (window)
        CloseWindow(window);
    REPORT("case=%s result=%s rc=%d", name, ok ? "PASS" : "FAIL", ok ? 0 : 20);
    return ok;
}


/* GLES2 entry points the offscreen cases use, all through eglGetProcAddress. */
struct gles2 {
    ClearColorFn clear_color;
    ClearFn clear;
    ReadPixelsFn read_pixels;
    GetErrorFn get_error;
    void (GLAPIENTRYP gen_framebuffers)(GLsizei, GLuint *);
    void (GLAPIENTRYP bind_framebuffer)(GLenum, GLuint);
    void (GLAPIENTRYP framebuffer_texture_2d)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (GLAPIENTRYP check_framebuffer_status)(GLenum);
    void (GLAPIENTRYP delete_framebuffers)(GLsizei, const GLuint *);
    void (GLAPIENTRYP gen_textures)(GLsizei, GLuint *);
    void (GLAPIENTRYP bind_texture)(GLenum, GLuint);
    void (GLAPIENTRYP tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                                    GLenum, GLenum, const void *);
    void (GLAPIENTRYP tex_parameteri)(GLenum, GLenum, GLint);
    void (GLAPIENTRYP delete_textures)(GLsizei, const GLuint *);
    GLboolean (GLAPIENTRYP is_texture)(GLuint);
    GLuint (GLAPIENTRYP create_shader)(GLenum);
    void (GLAPIENTRYP shader_source)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void (GLAPIENTRYP compile_shader)(GLuint);
    void (GLAPIENTRYP get_shaderiv)(GLuint, GLenum, GLint *);
    GLuint (GLAPIENTRYP create_program)(void);
    void (GLAPIENTRYP attach_shader)(GLuint, GLuint);
    void (GLAPIENTRYP bind_attrib_location)(GLuint, GLuint, const GLchar *);
    void (GLAPIENTRYP link_program)(GLuint);
    void (GLAPIENTRYP get_programiv)(GLuint, GLenum, GLint *);
    void (GLAPIENTRYP use_program)(GLuint);
    GLint (GLAPIENTRYP get_uniform_location)(GLuint, const GLchar *);
    void (GLAPIENTRYP uniform1i)(GLint, GLint);
    void (GLAPIENTRYP vertex_attrib_pointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
    void (GLAPIENTRYP enable_vertex_attrib_array)(GLuint);
    void (GLAPIENTRYP draw_arrays)(GLenum, GLint, GLsizei);
    void (GLAPIENTRYP viewport)(GLint, GLint, GLsizei, GLsizei);
    void (GLAPIENTRYP finish)(void);
    void (GLAPIENTRYP delete_program)(GLuint);
    void (GLAPIENTRYP delete_shader)(GLuint);
};

static int resolve_gles2(const char *name, struct gles2 *g)
{
    int missing = 0;
#define RESOLVE(field, symbol) do { \
    *(void **)&g->field = (void *)eglGetProcAddress(symbol); \
    if (!g->field) { REPORT("case=%s unresolved=%s", name, symbol); missing++; } \
} while (0)
    RESOLVE(clear_color, "glClearColor");
    RESOLVE(clear, "glClear");
    RESOLVE(read_pixels, "glReadPixels");
    RESOLVE(get_error, "glGetError");
    RESOLVE(gen_framebuffers, "glGenFramebuffers");
    RESOLVE(bind_framebuffer, "glBindFramebuffer");
    RESOLVE(framebuffer_texture_2d, "glFramebufferTexture2D");
    RESOLVE(check_framebuffer_status, "glCheckFramebufferStatus");
    RESOLVE(delete_framebuffers, "glDeleteFramebuffers");
    RESOLVE(gen_textures, "glGenTextures");
    RESOLVE(bind_texture, "glBindTexture");
    RESOLVE(tex_image_2d, "glTexImage2D");
    RESOLVE(tex_parameteri, "glTexParameteri");
    RESOLVE(delete_textures, "glDeleteTextures");
    RESOLVE(is_texture, "glIsTexture");
    RESOLVE(create_shader, "glCreateShader");
    RESOLVE(shader_source, "glShaderSource");
    RESOLVE(compile_shader, "glCompileShader");
    RESOLVE(get_shaderiv, "glGetShaderiv");
    RESOLVE(create_program, "glCreateProgram");
    RESOLVE(attach_shader, "glAttachShader");
    RESOLVE(bind_attrib_location, "glBindAttribLocation");
    RESOLVE(link_program, "glLinkProgram");
    RESOLVE(get_programiv, "glGetProgramiv");
    RESOLVE(use_program, "glUseProgram");
    RESOLVE(get_uniform_location, "glGetUniformLocation");
    RESOLVE(uniform1i, "glUniform1i");
    RESOLVE(vertex_attrib_pointer, "glVertexAttribPointer");
    RESOLVE(enable_vertex_attrib_array, "glEnableVertexAttribArray");
    RESOLVE(draw_arrays, "glDrawArrays");
    RESOLVE(viewport, "glViewport");
    RESOLVE(finish, "glFinish");
    RESOLVE(delete_program, "glDeleteProgram");
    RESOLVE(delete_shader, "glDeleteShader");
#undef RESOLVE
    return missing == 0;
}

/* Reads the pixel at X,Y of the bound framebuffer against R,G,B,A (+-1). */
static int pixel_is(const char *name, const char *what, struct gles2 *g,
                    int x, int y, int r, int gr, int b, int a)
{
    unsigned char p[4] = { 17, 34, 51, 68 };
    GLenum error;

    g->read_pixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    error = g->get_error();
    REPORT("case=%s stage=%s rgba=%u,%u,%u,%u expected=%d,%d,%d,%d tolerance=1 gl_error=0x%04x",
           name, what, p[0], p[1], p[2], p[3], r, gr, b, a, (unsigned)error);
    return error == GL_NO_ERROR && abs(p[0] - r) <= 1 && abs(p[1] - gr) <= 1
           && abs(p[2] - b) <= 1 && abs(p[3] - a) <= 1;
}

/* An initialized display and a GLES2 pbuffer configuration. */
static int offscreen_display(const char *name, EGLDisplay *display, EGLConfig *config)
{
    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint major = 0, minor = 0, count = 0;

    *display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (*display == EGL_NO_DISPLAY || !eglInitialize(*display, &major, &minor)) {
        egl_failure(name, "initialize");
        *display = EGL_NO_DISPLAY;
        return 0;
    }
    if (!eglChooseConfig(*display, attributes, config, 1, &count) || count < 1) {
        egl_failure(name, "choose-config");
        return 0;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        egl_failure(name, "bind-api");
        return 0;
    }
    return 1;
}

static const EGLint es2_context[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

/* PBUFFER: a 64x64 pbuffer cleared red and read back; swapping it is a no-op. */
static int run_pbuffer(void)
{
    const char *name = "PBUFFER";
    EGLint size[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLint width = 0, height = 0;
    struct gles2 g;
    int ok = 0, current = 0;

    REPORT("case=%s stage=begin", name);
    if (!offscreen_display(name, &display, &config))
        goto cleanup;
    surface = eglCreatePbufferSurface(display, config, size);
    if (surface == EGL_NO_SURFACE) {
        egl_failure(name, "pbuffer-surface");
        goto cleanup;
    }
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width)
        || !eglQuerySurface(display, surface, EGL_HEIGHT, &height)
        || width != 64 || height != 64) {
        REPORT("case=%s stage=surface-size result=FAIL surface=%dx%d", name, width, height);
        goto cleanup;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, es2_context);
    if (context == EGL_NO_CONTEXT) {
        egl_failure(name, "create-context");
        goto cleanup;
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        egl_failure(name, "make-current");
        goto cleanup;
    }
    current = 1;
    if (!resolve_gles2(name, &g))
        goto cleanup;
    g.clear_color(1.0f, 0.0f, 0.0f, 1.0f);
    g.clear(GL_COLOR_BUFFER_BIT);
    if (!pixel_is(name, "read-pixel", &g, 63, 63, 255, 0, 0, 255))
        goto cleanup;
    if (!eglSwapBuffers(display, surface)) {
        egl_failure(name, "swap");
        goto cleanup;
    }
    REPORT("case=%s stage=current surface=%dx%d", name, width, height);
    ok = 1;
cleanup:
    if (current && !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
        ok = 0;
    if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context))
        ok = 0;
    if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface))
        ok = 0;
    if (display != EGL_NO_DISPLAY && !eglTerminate(display))
        ok = 0;
    REPORT("case=%s result=%s rc=%d", name, ok ? "PASS" : "FAIL", ok ? 0 : 20);
    return ok;
}

/* A complete 16x16 RGBA texture framebuffer; 0 on failure. */
static GLuint texture_framebuffer(const char *name, struct gles2 *g, GLuint texture)
{
    GLuint fbo = 0;
    GLenum status;

    g->gen_framebuffers(1, &fbo);
    g->bind_framebuffer(GL_FRAMEBUFFER, fbo);
    g->framebuffer_texture_2d(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    status = g->check_framebuffer_status(GL_FRAMEBUFFER);
    REPORT("case=%s stage=framebuffer status=0x%04x complete=%d", name,
           (unsigned)status, status == GL_FRAMEBUFFER_COMPLETE);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        g->bind_framebuffer(GL_FRAMEBUFFER, 0);
        g->delete_framebuffers(1, &fbo);
        return 0;
    }
    return fbo;
}

static GLuint new_texture(struct gles2 *g, const unsigned char *rgba)
{
    GLuint texture = 0;
    static unsigned char texels[16 * 16 * 4];
    int i;

    for (i = 0; i < 16 * 16; i++)
        memcpy(texels + 4 * i, rgba, 4);
    g->gen_textures(1, &texture);
    g->bind_texture(GL_TEXTURE_2D, texture);
    g->tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g->tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g->tex_image_2d(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    return texture;
}

/* SURFACELESS: current with EGL_NO_SURFACE, rendering to a texture FBO. */
static int run_surfaceless(void)
{
    const char *name = "SURFACELESS";
    static const unsigned char black[4] = { 0, 0, 0, 255 };
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config;
    EGLContext context = EGL_NO_CONTEXT;
    const char *extensions;
    struct gles2 g;
    GLuint texture = 0, fbo = 0;
    int ok = 0, current = 0;

    REPORT("case=%s stage=begin", name);
    if (!offscreen_display(name, &display, &config))
        goto cleanup;
    extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions || !strstr(extensions, "EGL_KHR_surfaceless_context")) {
        REPORT("case=%s stage=extension result=FAIL", name);
        goto cleanup;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, es2_context);
    if (context == EGL_NO_CONTEXT) {
        egl_failure(name, "create-context");
        goto cleanup;
    }
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        egl_failure(name, "make-current");
        goto cleanup;
    }
    current = 1;
    if (eglGetCurrentContext() != context || eglGetCurrentSurface(EGL_DRAW) != EGL_NO_SURFACE) {
        egl_failure(name, "current-identities");
        goto cleanup;
    }
    if (!resolve_gles2(name, &g))
        goto cleanup;
    texture = new_texture(&g, black);
    if (!(fbo = texture_framebuffer(name, &g, texture)))
        goto cleanup;
    g.viewport(0, 0, 16, 16);
    g.clear_color(0.0f, 1.0f, 0.0f, 1.0f);
    g.clear(GL_COLOR_BUFFER_BIT);
    if (!pixel_is(name, "read-pixel", &g, 8, 8, 0, 255, 0, 255))
        goto cleanup;
    ok = 1;
cleanup:
    if (fbo) {
        g.bind_framebuffer(GL_FRAMEBUFFER, 0);
        g.delete_framebuffers(1, &fbo);
    }
    if (texture)
        g.delete_textures(1, &texture);
    if (current && !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
        ok = 0;
    if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context))
        ok = 0;
    if (display != EGL_NO_DISPLAY && !eglTerminate(display))
        ok = 0;
    REPORT("case=%s result=%s rc=%d", name, ok ? "PASS" : "FAIL", ok ? 0 : 20);
    return ok;
}

/* SHARED: a blue texture made in context A, drawn by a shader in context B
 * (created with A as its share context) onto B's pbuffer. */
static int run_shared(void)
{
    const char *name = "SHARED";
    static const unsigned char blue[4] = { 0, 0, 255, 255 };
    static const GLchar *vertex_source =
        "attribute vec2 position;\n"
        "varying vec2 coord;\n"
        "void main() { coord = position * 0.5 + 0.5; gl_Position = vec4(position, 0.0, 1.0); }\n";
    static const GLchar *fragment_source =
        "precision mediump float;\n"
        "uniform sampler2D image;\n"
        "varying vec2 coord;\n"
        "void main() { gl_FragColor = texture2D(image, coord); }\n";
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    EGLint size[] = { EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE };
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config;
    EGLSurface surface_a = EGL_NO_SURFACE, surface_b = EGL_NO_SURFACE;
    EGLContext a = EGL_NO_CONTEXT, b = EGL_NO_CONTEXT;
    struct gles2 g;
    GLuint texture = 0, vs = 0, fs = 0, program = 0;
    GLint status = 0;
    int ok = 0, current = 0;

    REPORT("case=%s stage=begin", name);
    if (!offscreen_display(name, &display, &config))
        goto cleanup;
    surface_a = eglCreatePbufferSurface(display, config, size);
    surface_b = eglCreatePbufferSurface(display, config, size);
    a = eglCreateContext(display, config, EGL_NO_CONTEXT, es2_context);
    if (surface_a == EGL_NO_SURFACE || surface_b == EGL_NO_SURFACE || a == EGL_NO_CONTEXT) {
        egl_failure(name, "create-a");
        goto cleanup;
    }
    b = eglCreateContext(display, config, a, es2_context);
    if (b == EGL_NO_CONTEXT) {
        egl_failure(name, "create-shared-context");
        goto cleanup;
    }
    if (!eglMakeCurrent(display, surface_a, surface_a, a)) {
        egl_failure(name, "make-current-a");
        goto cleanup;
    }
    current = 1;
    if (!resolve_gles2(name, &g))
        goto cleanup;
    texture = new_texture(&g, blue);
    g.finish();
    REPORT("case=%s stage=texture-in-a name=%u gl_error=0x%04x", name, texture,
           (unsigned)g.get_error());
    if (!eglMakeCurrent(display, surface_b, surface_b, b)) {
        egl_failure(name, "make-current-b");
        goto cleanup;
    }
    if (!g.is_texture(texture)) {
        REPORT("case=%s stage=texture-name-in-b result=FAIL", name);
        goto cleanup;
    }
    vs = g.create_shader(GL_VERTEX_SHADER);
    g.shader_source(vs, 1, &vertex_source, NULL);
    g.compile_shader(vs);
    g.get_shaderiv(vs, GL_COMPILE_STATUS, &status);
    if (!status) {
        REPORT("case=%s stage=vertex-shader result=FAIL", name);
        goto cleanup;
    }
    fs = g.create_shader(GL_FRAGMENT_SHADER);
    g.shader_source(fs, 1, &fragment_source, NULL);
    g.compile_shader(fs);
    g.get_shaderiv(fs, GL_COMPILE_STATUS, &status);
    if (!status) {
        REPORT("case=%s stage=fragment-shader result=FAIL", name);
        goto cleanup;
    }
    program = g.create_program();
    g.attach_shader(program, vs);
    g.attach_shader(program, fs);
    g.bind_attrib_location(program, 0, "position");
    g.link_program(program);
    g.get_programiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        REPORT("case=%s stage=link result=FAIL", name);
        goto cleanup;
    }
    g.viewport(0, 0, 32, 32);
    g.clear_color(1.0f, 0.0f, 0.0f, 1.0f);
    g.clear(GL_COLOR_BUFFER_BIT);
    g.use_program(program);
    g.bind_texture(GL_TEXTURE_2D, texture);
    g.uniform1i(g.get_uniform_location(program, "image"), 0);
    g.vertex_attrib_pointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    g.enable_vertex_attrib_array(0);
    g.draw_arrays(GL_TRIANGLE_STRIP, 0, 4);
    if (!pixel_is(name, "sampled-in-b", &g, 16, 16, 0, 0, 255, 255))
        goto cleanup;
    ok = 1;
cleanup:
    if (program)
        g.delete_program(program);
    if (vs)
        g.delete_shader(vs);
    if (fs)
        g.delete_shader(fs);
    if (texture)
        g.delete_textures(1, &texture);
    if (current && !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
        ok = 0;
    if (b != EGL_NO_CONTEXT && !eglDestroyContext(display, b))
        ok = 0;
    if (a != EGL_NO_CONTEXT && !eglDestroyContext(display, a))
        ok = 0;
    if (surface_a != EGL_NO_SURFACE)
        eglDestroySurface(display, surface_a);
    if (surface_b != EGL_NO_SURFACE)
        eglDestroySurface(display, surface_b);
    if (display != EGL_NO_DISPLAY && !eglTerminate(display))
        ok = 0;
    REPORT("case=%s result=%s rc=%d", name, ok ? "PASS" : "FAIL", ok ? 0 : 20);
    return ok;
}

/* EXTENSIONS: what egl.library implements is announced, nothing else of the
 * display extensions it used to claim. */
static int run_extensions(void)
{
    const char *name = "EXTENSIONS";
    static const char *const required[] = {
        "EGL_KHR_create_context", "EGL_KHR_surfaceless_context", "EGL_KHR_config_attribs" };
    static const char *const refused[] = {
        "EGL_KHR_image_base", "EGL_KHR_create_context_no_error" };
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config;
    const char *extensions;
    unsigned i;
    int ok = 0;

    if (!offscreen_display(name, &display, &config))
        goto cleanup;
    extensions = eglQueryString(display, EGL_EXTENSIONS);
    REPORT("case=%s extensions=%s", name, extensions ? extensions : "<NULL>");
    if (!extensions)
        goto cleanup;
    ok = 1;
    for (i = 0; i < sizeof(required) / sizeof(required[0]); i++)
        if (!strstr(extensions, required[i])) {
            REPORT("case=%s missing=%s", name, required[i]);
            ok = 0;
        }
    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
        if (strstr(extensions, refused[i])) {
            REPORT("case=%s unimplemented=%s", name, refused[i]);
            ok = 0;
        }
cleanup:
    if (display != EGL_NO_DISPLAY && !eglTerminate(display))
        ok = 0;
    REPORT("case=%s result=%s rc=%d", name, ok ? "PASS" : "FAIL", ok ? 0 : 20);
    return ok;
}

/* Without an argument every case runs; with one (WINDOW, PBUFFER,
 * SURFACELESS, SHARED or EXTENSIONS) only that case, the others count as
 * passed for the exit code and print as SKIP. */
int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : NULL;
    int es_ok = 1, gl_ok = 1, pb_ok = 1, sl_ok = 1, sh_ok = 1, ex_ok = 1;
    REPORT("stage=begin required=%s size=64x64",
           only ? only : "WINDOW,GLES2,GL,PBUFFER,SURFACELESS,SHARED,EXTENSIONS");
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) {
        REPORT("stage=open-intuition result=FAIL");
        goto cleanup;
    }
    EGLBase = OpenLibrary((CONST_STRPTR)"egl.library", 2);
    if (!EGLBase) {
        REPORT("stage=open-egl result=FAIL");
        goto cleanup;
    }
    /* Revision 2.1 appends the real resolver vector; 2.2 keeps a surface's
     * pixels with the surface, whichever context draws to it. An older
     * base (a resident or installed earlier copy) is refused, not tested. */
    REPORT("egl_library=%u.%u", (unsigned)EGLBase->lib_Version,
           (unsigned)EGLBase->lib_Revision);
    if (EGLBase->lib_Version == 2 && EGLBase->lib_Revision < 2) {
        REPORT("stage=resolver-abi result=FAIL required_library=2.2");
        goto cleanup;
    }
#define WANT(n) (!only || !strcmp(only, n))
    if (WANT("WINDOW")) {
        es_ok = run_case(1);
        gl_ok = run_case(0);
    }
    if (WANT("PBUFFER"))
        pb_ok = run_pbuffer();
    if (WANT("SURFACELESS"))
        sl_ok = run_surfaceless();
    if (WANT("SHARED"))
        sh_ok = run_shared();
    if (WANT("EXTENSIONS"))
        ex_ok = run_extensions();
#undef WANT
cleanup:
    if (EGLBase)
        CloseLibrary(EGLBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
#define PF(x) ((x) ? "PASS" : "FAIL")
    {
        int all = es_ok && gl_ok && pb_ok && sl_ok && sh_ok && ex_ok;

        REPORT("result=%s gles2=%s gl=%s pbuffer=%s surfaceless=%s shared=%s extensions=%s rc=%d",
               PF(all), PF(es_ok), PF(gl_ok), PF(pb_ok), PF(sl_ok), PF(sh_ok), PF(ex_ok),
               all ? 0 : 20);
        return all ? 0 : 20;
    }
#undef PF
}
