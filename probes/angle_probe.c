/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * ANGLE over AROS's EGL: the path Chromium takes with --use-gl=angle. ANGLE
 * is linked in (libEGL_static, libGLESv2_static), its GL backend reaches
 * egl.library through angle_native_egl.c, and this program only talks to
 * ANGLE's EGL and GLES. Cases: ANGLE's display on the native GLES backend;
 * a GLES 3 context (an ES 2 one, reported as a failure,
 * when ANGLE refuses ES 3) on a 64x64 window drawing a green quad with a
 * GLSL program, read back; a 32x32 pbuffer cleared blue, read back. ANGLE's own
 * debug messages are printed. Each line also goes to RAM:angleprobe.log,
 * closed after each line.
 */
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *angle_native_egl(const char *name);
void angle_native_egl_close(void);
void ANGLEArosSetNativeEGLResolver(void *(*resolver)(const char *name));

struct IntuitionBase *IntuitionBase;

#define LOG_PATH "RAM:angleprobe.log"
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
    snprintf(line_, sizeof(line_), "[ANGLEPROBE] " fmt "\n", ##__VA_ARGS__); \
    fputs(line_, stdout); \
    fflush(stdout); \
    log_line(line_); \
} while (0)

static int pixel_is(const char *name, int x, int y, int r, int g, int b, int a)
{
    unsigned char p[4] = { 17, 34, 51, 68 };
    GLenum error;

    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    error = glGetError();
    REPORT("case=%s stage=read-pixel rgba=%u,%u,%u,%u expected=%d,%d,%d,%d gl_error=0x%04x",
           name, p[0], p[1], p[2], p[3], r, g, b, a, (unsigned)error);
    return error == GL_NO_ERROR && abs(p[0] - r) <= 1 && abs(p[1] - g) <= 1 &&
           abs(p[2] - b) <= 1 && abs(p[3] - a) <= 1;
}

static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[256] = "";
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        REPORT("stage=compile result=FAIL log=%s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* A full-window green quad through a GLSL ES program. */
static int draw_green(const char *name, int width, int height)
{
    static const char *vs =
        "attribute vec2 position;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
    static const char *fs =
        "precision mediump float;\n"
        "void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n";
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs), program;
    GLint linked = 0;
    int ok;

    if (!v || !f)
        return 0;
    program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        REPORT("case=%s stage=link result=FAIL", name);
        return 0;
    }
    glViewport(0, 0, width, height);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ok = pixel_is(name, width / 2, height / 2, 0, 255, 0, 255);
    glDeleteProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    return ok;
}

/* ANGLE's own messages (EGL_KHR_debug): why it refuses what it refuses. */
static void EGLAPIENTRY angle_message(EGLenum error, const char *command, EGLint type,
                                      EGLLabelKHR thread, EGLLabelKHR object, const char *message)
{
    (void)thread;
    (void)object;
    REPORT("angle-message type=0x%04x error=0x%04x command=%s message=%s", (unsigned)type,
           (unsigned)error, command ? command : "-", message ? message : "-");
}

static int probe(void)
{
    const EGLAttrib display_attributes[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE,
        EGL_NONE
    };
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    const EGLint pbuffer_attributes[] = { EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE };
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface window_surface = EGL_NO_SURFACE, pbuffer = EGL_NO_SURFACE;
    struct Window *window = NULL;
    EGLint major = 0, minor = 0, count = 0;
    int display_ok = 0, window_ok = 0, pbuffer_ok = 0, es3 = 1;

    REPORT("stage=begin");
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase)
        goto cleanup;
    ANGLEArosSetNativeEGLResolver(angle_native_egl);
    {
        PFNEGLDEBUGMESSAGECONTROLKHRPROC control =
            (PFNEGLDEBUGMESSAGECONTROLKHRPROC)eglGetProcAddress("eglDebugMessageControlKHR");
        const EGLAttrib all[] = {
            EGL_DEBUG_MSG_CRITICAL_KHR, EGL_TRUE, EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE,
            EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE, EGL_DEBUG_MSG_INFO_KHR, EGL_TRUE, EGL_NONE
        };

        if (control)
            control(angle_message, all);
    }

    display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY,
                                    display_attributes);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
        REPORT("case=DISPLAY stage=initialize result=FAIL egl_error=0x%04x", (unsigned)eglGetError());
        goto cleanup;
    }
    REPORT("case=DISPLAY egl=%d.%d vendor=%s version=%s", major, minor,
           eglQueryString(display, EGL_VENDOR), eglQueryString(display, EGL_VERSION));
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count < 1) {
        REPORT("case=DISPLAY stage=choose-config result=FAIL egl_error=0x%04x", (unsigned)eglGetError());
        goto cleanup;
    }
    display_ok = 1;
    REPORT("case=DISPLAY result=PASS");

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT) {
        /* Chromium needs ES 3 (WebGL 2): no PASS without it. An ES 2 context
         * still runs the cases, to show what ANGLE made of the native GL. */
        const EGLint es2_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

        REPORT("stage=create-context-es3 result=FAIL egl_error=0x%04x", (unsigned)eglGetError());
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, es2_attributes);
        if (context == EGL_NO_CONTEXT) {
            REPORT("stage=create-context-es2 result=FAIL egl_error=0x%04x", (unsigned)eglGetError());
            goto cleanup;
        }
        es3 = 0;
    }

    window = OpenWindowTags(NULL, WA_Title, (IPTR)"ANGLE probe", WA_InnerWidth, 64,
        WA_InnerHeight, 64, WA_DragBar, TRUE, WA_Activate, FALSE, WA_SimpleRefresh, TRUE,
        WA_NoCareRefresh, TRUE, TAG_DONE);
    if (window)
        window_surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)window, NULL);
    if (window_surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, window_surface, window_surface, context)) {
        REPORT("case=WINDOW stage=current result=FAIL egl_error=0x%04x", (unsigned)eglGetError());
    } else {
        REPORT("case=WINDOW gl_renderer=%s", (const char *)glGetString(GL_RENDERER));
        REPORT("case=WINDOW gl_version=%s", (const char *)glGetString(GL_VERSION));
        window_ok = draw_green("WINDOW", 64, 64) && eglSwapBuffers(display, window_surface);
        REPORT("case=WINDOW result=%s", window_ok ? "PASS" : "FAIL");
    }

    pbuffer = eglCreatePbufferSurface(display, config, pbuffer_attributes);
    if (pbuffer == EGL_NO_SURFACE || !eglMakeCurrent(display, pbuffer, pbuffer, context)) {
        REPORT("case=PBUFFER stage=current result=FAIL egl_error=0x%04x", (unsigned)eglGetError());
    } else {
        glViewport(0, 0, 32, 32);
        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        pbuffer_ok = pixel_is("PBUFFER", 16, 16, 0, 0, 255, 255);
        REPORT("case=PBUFFER result=%s", pbuffer_ok ? "PASS" : "FAIL");
    }

cleanup:
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (pbuffer != EGL_NO_SURFACE)
            eglDestroySurface(display, pbuffer);
        if (window_surface != EGL_NO_SURFACE)
            eglDestroySurface(display, window_surface);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        eglTerminate(display);
    }
    if (window)
        CloseWindow(window);
    angle_native_egl_close();
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    {
        int all = display_ok && es3 && window_ok && pbuffer_ok;
        REPORT("result=%s display=%s es3=%s window=%s pbuffer=%s rc=%d", all ? "PASS" : "FAIL",
               display_ok ? "PASS" : "FAIL", es3 ? "PASS" : "FAIL", window_ok ? "PASS" : "FAIL",
               pbuffer_ok ? "PASS" : "FAIL", all ? 0 : 20);
        return all ? 0 : 20;
    }
}

/* ANGLE's initialisation and shader compiler need several megabytes of stack,
 * more than a Shell gives a command by default: the probe runs on a stack of
 * its own, whatever the caller's Stack setting. */
#define PROBE_STACK (32 * 1024 * 1024)

int main(void)
{
    struct StackSwapStruct sss;
    struct StackSwapArgs args = { { 0 } };
    APTR stack = AllocVec(PROBE_STACK, MEMF_ANY);
    int rc;

    if (!stack) {
        REPORT("stage=stack result=FAIL bytes=%d", PROBE_STACK);
        return 20;
    }
    sss.stk_Lower = stack;
    sss.stk_Upper = (UBYTE *)stack + PROBE_STACK;
    sss.stk_Pointer = sss.stk_Upper;
    rc = (int)NewStackSwap(&sss, probe, &args);
    FreeVec(stack);
    return rc;
}
