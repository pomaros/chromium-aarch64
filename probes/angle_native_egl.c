/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * The native EGL resolver ANGLE's GL backend asks for every native EGL and
 * GL entry point (ANGLEArosSetNativeEGLResolver): egl.library's own
 * eglGetProcAddress, which answers for core EGL functions too
 * (EGL_KHR_get_all_proc_addresses) and for GL through the owning mesa3dgl.
 * Apart from the ANGLE side because <proto/egl.h> defines the egl* names
 * as library calls, which ANGLE's own exports also use. The calls must be
 * the inline vector calls: a plain eglGetProcAddress symbol binds, at link
 * time, to ANGLE's own export, which hands ANGLE its own EGL_Initialize as
 * the native one and recurses until the stack runs out.
 */
#define __INLINE_EGL_STACKCALL__
#include <exec/libraries.h>
#include <proto/exec.h>
#include <EGL/egl.h>
#include <proto/egl.h>

struct Library *EGLBase;

void *angle_native_egl(const char *name)
{
    if (!EGLBase) {
        EGLBase = OpenLibrary((CONST_STRPTR)"egl.library", 2);
        /* 2.1 appends the resolver vector, 2.2 keeps pixels with surfaces. */
        if (EGLBase && EGLBase->lib_Version == 2 && EGLBase->lib_Revision < 2) {
            CloseLibrary(EGLBase);
            EGLBase = NULL;
        }
    }
    return EGLBase ? (void *)eglGetProcAddress(name) : NULL;
}

void angle_native_egl_close(void)
{
    if (EGLBase)
        CloseLibrary(EGLBase);
    EGLBase = NULL;
}
