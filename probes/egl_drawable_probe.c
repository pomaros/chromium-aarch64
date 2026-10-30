/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Native public-EGL/GLES pbuffer persistence and separate draw/read regression.
 * No GLA-private headers, HOST provider, context sharing or fallback renderer. */
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <EGL/egl.h>
#include <proto/egl.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

struct Library *EGLBase;
#define REPORT(...) do { printf("[EGLDRAW] "); printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } while(0)
typedef void (GLAPIENTRYP ClearColorFn)(GLclampf,GLclampf,GLclampf,GLclampf);
typedef void (GLAPIENTRYP ClearFn)(GLbitfield);
typedef void (GLAPIENTRYP ReadFn)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,GLvoid *);
typedef GLenum (GLAPIENTRYP ErrorFn)(void);
typedef void (GLAPIENTRYP FinishFn)(void);
typedef const GLubyte *(GLAPIENTRYP StringFn)(GLenum);
struct GLFns { ClearColorFn color; ClearFn clear; ReadFn read; ErrorFn error; FinishFn finish; StringFn string; };
struct Surface { EGLSurface handle; EGLint width,height; };
static int channel(unsigned char actual,unsigned char expected)
{ return abs((int)actual-(int)expected)<=8; }
static int bind(EGLDisplay d,EGLContext context,struct Surface *draw,struct Surface *read,const char *stage)
{
    EGLBoolean made=eglMakeCurrent(d,draw->handle,read->handle,context);
    EGLint error=eglGetError();
    int ok=made && error==EGL_SUCCESS && eglGetCurrentContext()==context &&
        eglGetCurrentDisplay()==d && eglGetCurrentSurface(EGL_DRAW)==draw->handle &&
        eglGetCurrentSurface(EGL_READ)==read->handle;
    REPORT("stage=%s bind=%d egl_error=0x%x context=%p draw=%p read=%p result=%s",
        stage,made,(unsigned)error,(void *)context,(void *)draw->handle,(void *)read->handle,ok?"PASS":"FAIL");
    return ok;
}
static int clear(struct GLFns *gl,const char *stage,unsigned char r,unsigned char g,unsigned char b)
{
    gl->color(r/255.0f,g/255.0f,b/255.0f,1.0f);
    gl->clear(GL_COLOR_BUFFER_BIT); gl->finish();
    GLenum error=gl->error();
    REPORT("stage=%s clear=%u,%u,%u gl_error=0x%x result=%s",stage,r,g,b,(unsigned)error,error==GL_NO_ERROR?"PASS":"FAIL");
    return error==GL_NO_ERROR;
}
static int pixels(struct GLFns *gl,struct Surface *read,const char *stage,unsigned char r,unsigned char g,unsigned char b)
{
    GLint points[3][2]={{0,0},{read->width/2,read->height/2},{read->width-1,read->height-1}};
    for (unsigned i=0;i<3;++i) {
        unsigned char p[4]={17,34,51,68};
        gl->read(points[i][0],points[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        GLenum error=gl->error();
        int ok=error==GL_NO_ERROR && channel(p[0],r) && channel(p[1],g) && channel(p[2],b) && channel(p[3],255);
        REPORT("stage=%s sample=%u xy=%d,%d rgba=%u,%u,%u,%u expected=%u,%u,%u,255 gl_error=0x%x result=%s",
            stage,i,points[i][0],points[i][1],p[0],p[1],p[2],p[3],r,g,b,(unsigned)error,ok?"PASS":"FAIL");
        if (!ok) return 0;
    }
    return 1;
}
static int resolve(struct GLFns *gl)
{
    gl->color=(ClearColorFn)eglGetProcAddress("glClearColor");
    gl->clear=(ClearFn)eglGetProcAddress("glClear");
    gl->read=(ReadFn)eglGetProcAddress("glReadPixels");
    gl->error=(ErrorFn)eglGetProcAddress("glGetError");
    gl->finish=(FinishFn)eglGetProcAddress("glFinish");
    gl->string=(StringFn)eglGetProcAddress("glGetString");
    if (!gl->color || !gl->clear || !gl->read || !gl->error || !gl->finish || !gl->string) return 0;
    const char *version=(const char *)gl->string(GL_VERSION);
    const char *renderer=(const char *)gl->string(GL_RENDERER);
    int major=0,minor=0;
    GLenum error=gl->error();
    REPORT("stage=actual-api version=%s renderer=%s gl_error=0x%x",version?version:"<NULL>",renderer?renderer:"<NULL>",(unsigned)error);
    return error==GL_NO_ERROR && renderer && *renderer && version && sscanf(version,"OpenGL ES %d.%d",&major,&minor)==2 && major>=2 && minor>=0;
}
static int unbind(EGLDisplay d)
{
    EGLBoolean ok=eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    EGLint error=eglGetError();
    return ok && error==EGL_SUCCESS && eglGetCurrentContext()==EGL_NO_CONTEXT &&
        eglGetCurrentSurface(EGL_DRAW)==EGL_NO_SURFACE && eglGetCurrentSurface(EGL_READ)==EGL_NO_SURFACE;
}
static void checked_unbind(EGLDisplay d,const char *name,const char *stage)
{
    if (unbind(d)) return;
    REPORT("case=%s stage=%s cleanup=REFUSED result=FAIL retain=context,surface,library",name,stage);
    /* Unknown binding: never run another suite or destroy/terminate/unload.
     * External supervisor capture and recovery is the only continuation. */
    for (;;) Delay(50);
}
static int run_case(EGLDisplay d,EGLConfig config,int unequal)
{
    const char *name=unequal?"unequal":"equal";
    EGLContext a=EGL_NO_CONTEXT,b=EGL_NO_CONTEXT;
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    struct Surface p={EGL_NO_SURFACE,unequal?29:32,unequal?17:32};
    struct Surface q={EGL_NO_SURFACE,unequal?47:32,unequal?31:32};
    struct GLFns gl;
    int ok=0;
    REPORT("case=%s stage=begin P=%dx%d Q=%dx%d",name,p.width,p.height,q.width,q.height);
    struct Surface *surfaces[2]={&p,&q};
    for (unsigned i=0;i<2;++i) {
        struct Surface *s=surfaces[i];
        const EGLint attrs[]={EGL_WIDTH,s->width,EGL_HEIGHT,s->height,EGL_LARGEST_PBUFFER,EGL_FALSE,EGL_NONE};
        s->handle=eglCreatePbufferSurface(d,config,attrs);
        EGLint width=0,height=0;
        if (s->handle==EGL_NO_SURFACE || !eglQuerySurface(d,s->handle,EGL_WIDTH,&width) ||
            !eglQuerySurface(d,s->handle,EGL_HEIGHT,&height) || width!=s->width || height!=s->height) goto cleanup;
    }
    if (p.handle==q.handle) goto cleanup;
    a=eglCreateContext(d,config,EGL_NO_CONTEXT,context_attrs);
    b=eglCreateContext(d,config,EGL_NO_CONTEXT,context_attrs);
    if (a==EGL_NO_CONTEXT || b==EGL_NO_CONTEXT || a==b) goto cleanup;
    REPORT("case=%s contextA=%p contextB=%p P=%p Q=%p shared=0",name,(void *)a,(void *)b,(void *)p.handle,(void *)q.handle);
    if (!bind(d,a,&p,&p,"A-P-red") || !resolve(&gl) ||
        !clear(&gl,"A-P-red",255,0,0) || !pixels(&gl,&p,"A-P-red",255,0,0)) goto cleanup;
    if (!bind(d,a,&q,&q,"A-Q-green") || !clear(&gl,"A-Q-green",0,255,0) ||
        !pixels(&gl,&q,"A-Q-green",0,255,0)) goto cleanup;
    /* B is not shared with A. Storage must belong to each EGL surface. */
    if (!bind(d,b,&p,&p,"B-P-persist-red") || !pixels(&gl,&p,"B-P-persist-red",255,0,0)) goto cleanup;
    if (!bind(d,b,&q,&q,"B-Q-persist-green") || !pixels(&gl,&q,"B-Q-persist-green",0,255,0)) goto cleanup;
    if (!bind(d,a,&p,&p,"A-P-persist-red") || !pixels(&gl,&p,"A-P-persist-red",255,0,0)) goto cleanup;
    if (!bind(d,b,&q,&q,"B-Q-persist-green-again") || !pixels(&gl,&q,"B-Q-persist-green-again",0,255,0)) goto cleanup;
    /* Separate read surface must stay green while the draw surface turns blue. */
    if (!bind(d,a,&p,&q,"A-drawP-readQ") || !pixels(&gl,&q,"split-before-green",0,255,0) ||
        !clear(&gl,"split-drawP-blue",0,0,255) || !pixels(&gl,&q,"split-after-readQ-green",0,255,0)) goto cleanup;
    if (!bind(d,a,&p,&p,"A-readP-blue") || !pixels(&gl,&p,"A-readP-blue",0,0,255)) goto cleanup;
    if (!bind(d,a,&q,&q,"A-readQ-still-green") || !pixels(&gl,&q,"A-readQ-still-green",0,255,0)) goto cleanup;
    checked_unbind(d,name,"A-unbind");
    if (!eglDestroyContext(d,a)) goto cleanup;
    a=EGL_NO_CONTEXT;
    REPORT("case=%s stage=A-destroyed result=PASS",name);
    if (!bind(d,b,&p,&p,"B-after-A-destroy-P") || !pixels(&gl,&p,"B-after-A-destroy-P",0,0,255)) goto cleanup;
    if (!bind(d,b,&q,&q,"B-after-A-destroy-Q") || !pixels(&gl,&q,"B-after-A-destroy-Q",0,255,0)) goto cleanup;
    ok=1;
cleanup:
    if (!ok) REPORT("case=%s stage=failure egl_error=0x%x",name,(unsigned)eglGetError());
    checked_unbind(d,name,"cleanup-unbind");
    if (a!=EGL_NO_CONTEXT && !eglDestroyContext(d,a)) ok=0;
    if (b!=EGL_NO_CONTEXT && !eglDestroyContext(d,b)) ok=0;
    if (p.handle!=EGL_NO_SURFACE && !eglDestroySurface(d,p.handle)) ok=0;
    if (q.handle!=EGL_NO_SURFACE && !eglDestroySurface(d,q.handle)) ok=0;
    REPORT("case=%s result=%s",name,ok?"PASS":"FAIL");
    return ok;
}
int main(void)
{
    EGLDisplay d=EGL_NO_DISPLAY;
    EGLConfig config;
    EGLint n=0,major=0,minor=0;
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    int initialized=0,equal=0,unequal=0,ok=0;
    REPORT("protocol=1 stage=begin native_execution_required=1");
    EGLBase=OpenLibrary((CONST_STRPTR)"egl.library",2);
    if (!EGLBase) goto cleanup;
    REPORT("stage=library egl=%u.%u",(unsigned)EGLBase->lib_Version,(unsigned)EGLBase->lib_Revision);
    /* 2.2 keeps a surface's pixels with the surface; an older (resident or
     * installed) copy is refused here rather than measured. */
    if (EGLBase->lib_Version==2 && EGLBase->lib_Revision<2) goto cleanup;
    d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d==EGL_NO_DISPLAY || !eglInitialize(d,&major,&minor)) goto cleanup;
    initialized=1;
    REPORT("stage=initialize EGL=%d.%d",major,minor);
    if (major<1 || (major==1 && minor<4) || !eglBindAPI(EGL_OPENGL_ES_API) ||
        eglQueryAPI()!=EGL_OPENGL_ES_API || !eglChooseConfig(d,attrs,&config,1,&n) || n!=1) goto cleanup;
    equal=run_case(d,config,0);
    unequal=run_case(d,config,1); /* Report both suites even when the first fails. */
    ok=equal && unequal;
cleanup:
    if (initialized && !eglTerminate(d)) ok=0;
    if (EGLBase) CloseLibrary(EGLBase);
    REPORT("FINAL result=%s equal=%s unequal=%s rc=%d",ok?"PASS":"FAIL",equal?"PASS":"FAIL",unequal?"PASS":"FAIL",ok?0:20);
    return ok?0:20;
}
