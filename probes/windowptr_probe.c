// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// WindowPtrProbe: a pthread inherits its creator's requester policy. main()
// turns DOS requesters off (pr_WindowPtr -1); a thread, and a thread it
// creates, must see -1 too. Prints [WINDOWPTR] lines and result=PASS.

#include <dos/dosextens.h>
#include <proto/exec.h>
#include <pthread.h>
#include <stdio.h>

static APTR seen1, seen2;

static void *inner(void *arg)
{
    (void)arg;
    seen2 = ((struct Process *)FindTask(NULL))->pr_WindowPtr;
    return NULL;
}

static void *outer(void *arg)
{
    pthread_t t;
    (void)arg;
    seen1 = ((struct Process *)FindTask(NULL))->pr_WindowPtr;
    pthread_create(&t, NULL, inner, NULL);
    pthread_join(t, NULL);
    return NULL;
}

int main(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR saved = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    pthread_t t;
    pthread_create(&t, NULL, outer, NULL);
    pthread_join(t, NULL);
    me->pr_WindowPtr = saved;
    int ok1 = seen1 == (APTR)-1, ok2 = seen2 == (APTR)-1;
    printf("[WINDOWPTR] thread sees %p %s\n", seen1, ok1 ? "ok" : "FAIL");
    printf("[WINDOWPTR] thread of a thread sees %p %s\n", seen2, ok2 ? "ok" : "FAIL");
    printf("[WINDOWPTR] result=%s\n", ok1 && ok2 ? "PASS" : "FAIL");
    return ok1 && ok2 ? 0 : 20;
}
