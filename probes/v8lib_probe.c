// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// V8LibProbe: V8 through v8.library alone, without Chromium. Creates isolates
// and contexts and runs a script long enough for V8 to compile it beyond the
// interpreter, ROUNDS times. Every step is announced in the kernel log first
// (KPutStr), so a machine that stops leaves its last step in a live kernel
// log stream (tools/m1-klog-stream). Prints [V8LIB] lines and result=PASS.
//
//   V8LibProbe [ROUNDS]

#include <proto/exec.h>
#include <proto/v8.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *V8Base;
extern void KPutStr(const char *) __asm__("KPutStr");

static void step(const char *what)
{
    char line[160];
    snprintf(line, sizeof line, "[V8LIB] %s\n", what);
    KPutStr(line);
    printf("%s", line);
    fflush(stdout);
}

static const char *script =
    "function f(n){let s=0;for(let i=0;i<n;i++){s=(s+i*i)%1000003;}return s;}"
    "let t=0;for(let k=0;k<200;k++){t=(t+f(20000))%1000003;}t";

int main(int argc, char **argv)
{
    int rounds = argc > 1 ? atoi(argv[1]) : 3;
    char result[128];
    int failures = 0;

    step("open v8.library");
    V8Base = OpenLibrary((CONST_STRPTR)"v8.library", 0);
    if (!V8Base) {
        step("result=FAIL no v8.library");
        return 20;
    }
    step("V8Initialize");
    if (!V8Initialize()) {
        step("result=FAIL V8Initialize");
        CloseLibrary(V8Base);
        return 20;
    }
    for (int r = 0; r < rounds; r++) {
        char line[96];
        snprintf(line, sizeof line, "round %d: create isolate", r);
        step(line);
        V8IsolateHandle iso = V8CreateIsolate();
        if (!iso) {
            step("create isolate FAIL");
            failures++;
            continue;
        }
        step("create context");
        V8ContextHandle ctx = V8CreateContext(iso);
        if (!ctx) {
            step("create context FAIL");
            failures++;
            V8DestroyIsolate(iso);
            continue;
        }
        step("eval");
        memset(result, 0, sizeof result);
        LONG ok = V8Eval(iso, ctx, (CONST_STRPTR)script, (STRPTR)result, sizeof result);
        snprintf(line, sizeof line, "eval rc=%ld result=%s", (long)ok, result);
        step(line);
        /* V8_SUCCESS is 0; the script's value is computed, not guessed */
        if (ok != 0 || strcmp(result, "124798") != 0)
            failures++;
        step("destroy");
        V8DestroyContext(iso, ctx);
        V8DestroyIsolate(iso);
    }
    step("V8Cleanup");
    V8Cleanup();
    CloseLibrary(V8Base);
    printf("[V8LIB] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
