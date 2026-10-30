/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 *
 * The AArch64 stdc.library vector stubs of the Chromium allocator shim
 * (queue src 0694, allocator_shim_aros_libc.cc): malloc, calloc, realloc,
 * aligned_alloc and free reached through base[-LVO] with the base in x16,
 * as the genmodule stack-call thunks expect. Memory from the stubs must be
 * usable, calloc zeroed, realloc must keep the contents, aligned_alloc must
 * align, and the stdc free() a program calls normally must accept it.
 * Prints [STDCVEC] lines and result=PASS.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <defines/stdc_LVO.h>

struct StdCBase *__aros_getbase_StdCBase(void);

void **aros_stdc_shim_base;

#define STR_(x) #x
#define STR(x) STR_(x)
#define STUB(name, lvo)                                   \
  __asm__(".text\n\t.p2align 2\n\t.type " #name ", @function\n" #name ":\n" \
      "\tmovz x16, #:abs_g3:aros_stdc_shim_base\n"         \
      "\tmovk x16, #:abs_g2_nc:aros_stdc_shim_base\n"      \
      "\tmovk x16, #:abs_g1_nc:aros_stdc_shim_base\n"      \
      "\tmovk x16, #:abs_g0_nc:aros_stdc_shim_base\n"      \
      "\tldr x16, [x16]\n"                                 \
      "\tmov x17, #(" STR(lvo) "*8)\n"                     \
      "\tsub x17, x16, x17\n"                              \
      "\tldr x17, [x17]\n"                                 \
      "\tbr x17\n"                                         \
      "\t.size " #name ", .-" #name "\n");

void shim_free(void *);
void *shim_realloc(void *, size_t);
void *shim_malloc(size_t);
void *shim_calloc(size_t, size_t);
void *shim_aligned_alloc(size_t, size_t);
STUB(shim_free, LVOfree)
STUB(shim_realloc, LVOrealloc)
STUB(shim_malloc, LVOmalloc)
STUB(shim_calloc, LVOcalloc)
STUB(shim_aligned_alloc, LVOaligned_alloc)

static int failures;
static void check(const char *what, int ok)
{
    printf("[STDCVEC] %s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int main(void)
{
    aros_stdc_shim_base = (void **)__aros_getbase_StdCBase();
    check("base", aros_stdc_shim_base != NULL);
    if (!aros_stdc_shim_base)
        return 20;

    for (int round = 0; round < 1000; round++) {
        unsigned char *m = shim_malloc(4096);
        if (!m) { check("malloc", 0); break; }
        memset(m, round & 0xff, 4096);
        unsigned char *c = shim_calloc(64, 16);
        int zero = c != NULL;
        for (int k = 0; c && k < 1024; k++) zero &= c[k] == 0;
        unsigned char *r = shim_realloc(m, 65536);
        int kept = r != NULL;
        for (int k = 0; r && k < 4096; k++) kept &= r[k] == (round & 0xff);
        void *a = shim_aligned_alloc(256, 1000);
        int aligned = a != NULL && ((uintptr_t)a & 255) == 0;
        if (!zero || !kept || !aligned) {
            check(!zero ? "calloc" : !kept ? "realloc" : "aligned_alloc", 0);
            break;
        }
        shim_free(r);
        free(c);   /* the ordinary C free accepts stub memory */
        shim_free(a);
        if (round == 999) check("1000 rounds", 1);
    }
    void *p = malloc(128);   /* and the stub free accepts ordinary memory */
    shim_free(p);
    check("cross free", 1);
    printf("[STDCVEC] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
