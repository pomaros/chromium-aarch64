// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// NoRequesters: turns off DOS requesters ("insert volume ...") for the Shell
// that runs it. A command runs in its Shell's process, so pr_WindowPtr stays
// -1 for every later command of the same script: a missing volume then
// returns an error instead of a requester that blocks until someone answers.
// For programs ported from POSIX that probe paths such as /proc/cpuinfo.

#include <dos/dosextens.h>
#include <proto/exec.h>

int main(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    me->pr_WindowPtr = (APTR)-1;
    return 0;
}
