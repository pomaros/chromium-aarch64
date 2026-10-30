/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Native AROS EGL two-task presentation test. No linked desktop GL provider.
 * Requires real EGL/GLA vectors and an independent native hang supervisor. */
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/dos.h>
#include <EGL/egl.h>
#include <proto/egl.h>
#include <GL/gl.h>
#include <GL/gla.h>
#include <proto/gl.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#ifndef WA_ScaleAware
#error "Actual native WA_ScaleAware headers are required; do not invent a tag"
#endif
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *CyberGfxBase, *EGLBase, *GLBase;
static pthread_mutex_t state_lock;
static pthread_mutex_t log_lock;
static pthread_cond_t changed;
static struct timespec deadline;
static int failed, phase;
static EGLDisplay display=EGL_NO_DISPLAY;
struct Worker {
    unsigned id;
    struct Window *window;
    EGLContext context;
    EGLSurface surface;
    GLAContext owner;
    int ok;
};
static struct Worker workers[2];
#define REPORT(...) do { pthread_mutex_lock(&log_lock); \
    printf("[EGLTASK] "); printf(__VA_ARGS__); putchar('\n'); fflush(stdout); \
    pthread_mutex_unlock(&log_lock); } while(0)
typedef void (GLAPIENTRYP ClearColorFn)(GLclampf,GLclampf,GLclampf,GLclampf);
typedef void (GLAPIENTRYP ClearFn)(GLbitfield);
typedef void (GLAPIENTRYP ReadPixelsFn)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,GLvoid *);
typedef GLenum (GLAPIENTRYP GetErrorFn)(void);
typedef void (GLAPIENTRYP FinishFn)(void);
struct GLFns { ClearColorFn color; ClearFn clear; ReadPixelsFn read; GetErrorFn error; FinishFn finish; };
static void fail(unsigned id,const char *stage)
{
    pthread_mutex_lock(&state_lock);
    failed=1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&state_lock);
    REPORT("worker=%u stage=%s result=FAIL",id,stage);
}
static int wait_phase(unsigned id,int wanted)
{
    int rc=0,ok;
    pthread_mutex_lock(&state_lock);
    while (!failed && phase<wanted && rc==0)
        rc=pthread_cond_timedwait(&changed,&state_lock,&deadline);
    if (rc) { failed=1; pthread_cond_broadcast(&changed); }
    ok=!failed && phase>=wanted;
    pthread_mutex_unlock(&state_lock);
    if (!ok) REPORT("worker=%u wait=%d rc=%d result=FAIL",id,wanted,rc);
    return ok;
}
static void advance(int value)
{
    pthread_mutex_lock(&state_lock);
    if (!failed) phase=value;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&state_lock);
}
static int still_current(struct Worker *w)
{
    GLAContext owner=glAGetCurrentContext();
    int ok=eglGetCurrentContext()==w->context &&
        eglGetCurrentDisplay()==display &&
        eglGetCurrentSurface(EGL_DRAW)==w->surface &&
        eglGetCurrentSurface(EGL_READ)==w->surface && owner && owner==w->owner;
    REPORT("worker=%u egl=%p native_owner=%p expected_owner=%p current=%d",
        w->id,(void *)eglGetCurrentContext(),(void *)owner,(void *)w->owner,ok);
    return ok;
}
static int bind(struct Worker *w,struct GLFns *gl)
{
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglMakeCurrent(display,w->surface,w->surface,w->context)) return 0;
    w->owner=glAGetCurrentContext(); /* public native GLA handle, NOT an EGL cast */
    gl->color=(ClearColorFn)eglGetProcAddress("glClearColor");
    gl->clear=(ClearFn)eglGetProcAddress("glClear");
    gl->read=(ReadPixelsFn)eglGetProcAddress("glReadPixels");
    gl->error=(GetErrorFn)eglGetProcAddress("glGetError");
    gl->finish=(FinishFn)eglGetProcAddress("glFinish");
    return gl->color && gl->clear && gl->read && gl->error && gl->finish && still_current(w);
}
static int channel(unsigned char a,unsigned char b) { return abs((int)a-(int)b)<=8; }
static int render(struct Worker *w,struct GLFns *gl,unsigned char r,unsigned char g,unsigned char b)
{
    unsigned char pixel[4]={17,34,51,68};
    if (!still_current(w)) return 0;
    gl->color(r/255.0f,g/255.0f,b/255.0f,1.0f);
    gl->clear(GL_COLOR_BUFFER_BIT);
    gl->finish();
    gl->read(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=gl->error();
    REPORT("worker=%u readback=%u,%u,%u,%u gl_error=0x%x expected=%u,%u,%u",
        w->id,pixel[0],pixel[1],pixel[2],pixel[3],(unsigned)error,r,g,b);
    return error==GL_NO_ERROR && channel(pixel[0],r) && channel(pixel[1],g) && channel(pixel[2],b);
}
/* Read the visible SCREEN bitmap, not a window/backing/context bitmap.
 * Scale-aware windows use physical geometry. No guessed scaling conversion. */
static int presented(struct Worker *w,unsigned char r,unsigned char g,unsigned char b)
{
    struct Screen *s=w->window->WScreen;
    long x=(long)w->window->LeftEdge+w->window->BorderLeft+32;
    long y=(long)w->window->TopEdge+w->window->BorderTop+32;
    ULONG width=GetBitMapAttr(s->RastPort.BitMap,BMA_WIDTH);
    ULONG height=GetBitMapAttr(s->RastPort.BitMap,BMA_HEIGHT);
    unsigned char pixel[3]={17,34,51};
    ULONG count=0;
    if (x<0 || y<0 || x>USHRT_MAX || y>USHRT_MAX || (ULONG)x>=width || (ULONG)y>=height) return 0;
    for (unsigned frame=0;frame<120;++frame) {
        count=ReadPixelArray(pixel,0,0,3,&s->RastPort,(UWORD)x,(UWORD)y,1,1,RECTFMT_RGB);
        if (count==1 && channel(pixel[0],r) && channel(pixel[1],g) && channel(pixel[2],b)) {
            REPORT("window=%u screen_pixel=%u,%u,%u xy=%ld,%ld frame=%u result=PASS",
                w->id,pixel[0],pixel[1],pixel[2],x,y,frame);
            return 1;
        }
        WaitTOF();
    }
    REPORT("window=%u screen_pixel=%u,%u,%u pixels=%lu xy=%ld,%ld expected=%u,%u,%u result=FAIL",
        w->id,pixel[0],pixel[1],pixel[2],(unsigned long)count,x,y,r,g,b);
    return 0;
}
static int unbind(struct Worker *w)
{
    int ok=eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT) &&
        eglGetCurrentContext()==EGL_NO_CONTEXT && glAGetCurrentContext()==NULL;
    REPORT("worker=%u stage=unbind result=%s",w->id,ok?"PASS":"FAIL");
    return ok;
}
static void *run_worker(void *arg)
{
    struct Worker *w=arg;
    struct GLFns gl;
    int bound=0;
    if (w->id==1 && !wait_phase(1,1)) goto out;
    int bind_ok=bind(w,&gl);
    bound=eglGetCurrentContext()==w->context;
    if (!bind_ok) { fail(w->id,"bind"); goto out; }
    if (w->id==0) {
        if (!render(w,&gl,255,0,0)) { fail(0,"red-readback"); goto out; }
        advance(1); /* A-MakeCurrent, before B-MakeCurrent. */
        if (!wait_phase(0,2)) goto out;
        if (w->owner==workers[1].owner || w->context==workers[1].context ||
            !still_current(w) || !eglSwapBuffers(display,w->surface) || !still_current(w) ||
            !presented(w,255,0,0) || !presented(&workers[1],0,255,0)) {
            fail(0,"A-swap-after-B-current"); goto out;
        }
        if (!unbind(w)) { fail(0,"A-unbind"); goto out; }
        bound=0;
        advance(3); /* B remains current; main joins A before releasing B. */
    } else {
        if (!render(w,&gl,0,255,0) || !eglSwapBuffers(display,w->surface) || !still_current(w) || !presented(w,0,255,0)) {
            fail(1,"green-baseline"); goto out;
        }
        advance(2);
        if (!wait_phase(1,4)) goto out;
        if (!still_current(w) || !render(w,&gl,0,0,255) ||
            !eglSwapBuffers(display,w->surface) || !still_current(w) || !presented(w,0,0,255) ||
            !presented(&workers[0],255,0,0)) { fail(1,"B-swap-after-A-unbind-and-exit"); goto out; }
        if (!unbind(w)) { fail(1,"B-unbind"); goto out; }
        bound=0;
    }
    w->ok=1;
out:
    if (bound && !unbind(w)) fail(w->id,"failure-unbind");
    REPORT("worker=%u stage=return result=%s",w->id,w->ok?"PASS":"FAIL");
    return NULL;
}
int main(void)
{
    EGLConfig config;
    EGLint count=0,major=0,minor=0;
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    const EGLint ctxattrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    pthread_t threads[2];
    unsigned started=0,joined=0;
    int initialized=0,result=20;
    if (pthread_mutex_init(&state_lock,NULL)) return 20;
    if (pthread_mutex_init(&log_lock,NULL)) { pthread_mutex_destroy(&state_lock); return 20; }
    if (pthread_cond_init(&changed,NULL)) {
        pthread_mutex_destroy(&log_lock); pthread_mutex_destroy(&state_lock); return 20;
    }
    REPORT("protocol=1 stage=begin target=AROS native_execution_required=1");
    IntuitionBase=(struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library",0);
    GfxBase=(struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library",0);
    CyberGfxBase=OpenLibrary((CONST_STRPTR)"cybergraphics.library",0);
    GLBase=OpenLibrary((CONST_STRPTR)"gl.library",20);
    EGLBase=OpenLibrary((CONST_STRPTR)"egl.library",2);
    if (!IntuitionBase || !GfxBase || !CyberGfxBase || !GLBase || !EGLBase) goto cleanup;
    if (EGLBase->lib_Version==2 && EGLBase->lib_Revision<2) goto cleanup;
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display==EGL_NO_DISPLAY || !eglInitialize(display,&major,&minor)) goto cleanup;
    initialized=1;
    if (!eglChooseConfig(display,attrs,&config,1,&count) || count<1 || !eglBindAPI(EGL_OPENGL_ES_API)) goto cleanup;
    for (unsigned i=0;i<2;++i) {
        struct Worker *w=&workers[i]; w->id=i;
        w->window=OpenWindowTags(NULL,WA_Title,(IPTR)(i?"EGL task B":"EGL task A"),
            WA_Left,40+160*i,WA_Top,60,WA_InnerWidth,96,WA_InnerHeight,96,
            WA_ScaleAware,TRUE,WA_DragBar,FALSE,WA_Activate,FALSE,
            WA_SimpleRefresh,TRUE,WA_NoCareRefresh,TRUE,TAG_DONE);
        if (!w->window) goto cleanup;
        w->surface=eglCreateWindowSurface(display,config,(EGLNativeWindowType)w->window,NULL);
        w->context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxattrs);
        if (w->surface==EGL_NO_SURFACE || w->context==EGL_NO_CONTEXT) goto cleanup;
        EGLint width=0,height=0;
        if (!eglQuerySurface(display,w->surface,EGL_WIDTH,&width) ||
            !eglQuerySurface(display,w->surface,EGL_HEIGHT,&height) || width!=96 || height!=96) goto cleanup;
        WindowToFront(w->window);
        REPORT("window=%u xywh=%d,%d,%d,%d scale_aware=1 egl_context=%p",
            i,w->window->LeftEdge,w->window->TopEdge,w->window->Width,w->window->Height,(void *)w->context);
    }
    struct Window *a=workers[0].window,*b=workers[1].window;
    if (a->WScreen!=b->WScreen ||
        !(a->LeftEdge+a->Width<=b->LeftEdge || b->LeftEdge+b->Width<=a->LeftEdge ||
          a->TopEdge+a->Height<=b->TopEdge || b->TopEdge+b->Height<=a->TopEdge)) goto cleanup;
    if (clock_gettime(CLOCK_REALTIME,&deadline)) goto cleanup;
    deadline.tv_sec+=30;
    for (unsigned i=0;i<2;++i) {
        int rc=pthread_create(&threads[i],NULL,run_worker,&workers[i]);
        if (rc) { fail(i,"pthread-create"); break; }
        ++started;
    }
    if (started==2 && wait_phase(2,3)) {
        int rc=pthread_join(threads[0],NULL);
        if (!rc) { joined=1; advance(4); }
        else fail(2,"join-A");
    }
    for (unsigned i=joined;i<started;++i) {
        int rc=pthread_join(threads[i],NULL);
        if (rc) {
            REPORT("stage=join worker=%u rc=%d cleanup=REFUSED result=FAIL",i,rc);
            /* Live worker may still use every resource. Never unload its code. */
            for (;;) Delay(50);
        }
    }
    if (started==2 && !failed && workers[0].ok && workers[1].ok &&
        workers[0].owner && workers[1].owner && workers[0].owner!=workers[1].owner &&
        eglGetCurrentContext()==EGL_NO_CONTEXT && glAGetCurrentContext()==NULL) result=0;
cleanup:
    /* No rendering worker is live here: every successfully created thread joined. */
    for (unsigned i=0;i<2;++i) {
        if (workers[i].context && !eglDestroyContext(display,workers[i].context)) result=20;
        if (workers[i].surface && !eglDestroySurface(display,workers[i].surface)) result=20;
        if (workers[i].window) CloseWindow(workers[i].window);
    }
    if (initialized && !eglTerminate(display)) result=20;
    if (EGLBase) CloseLibrary(EGLBase);
    if (GLBase) CloseLibrary(GLBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (pthread_cond_destroy(&changed)) result=20;
    if (pthread_mutex_destroy(&state_lock)) result=20;
    if (pthread_mutex_destroy(&log_lock)) result=20;
    printf("[EGLTASK] FINAL result=%s rc=%d started=%u\n",result?"FAIL":"PASS",result,started);
    fflush(stdout);
    return result;
}
