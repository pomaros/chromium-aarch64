// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// Launch "COMMAND LINE": starts the command line in a detached Shell process
// (SystemTags with SYS_Asynch, NIL: in and out, a 32 MB stack, DOS
// requesters off, current directory = Launch's own directory, so files
// delivered beside it are reached by name) and returns at once. A long-running program started from
// usb-link exec this way does not hold the exec until it exits.

#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: Launch \"COMMAND LINE\"\n");
        return 10;
    }
    BPTR in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    BPTR out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out) {
        printf("Launch: cannot open NIL:\n");
        return 20;
    }
    BPTR here = DupLock(GetProgramDir());
    LONG rc = SystemTags((CONST_STRPTR)argv[1], SYS_Input, in, SYS_Output, out,
                         NP_CurrentDir, (IPTR)here,
                         SYS_Asynch, TRUE, NP_StackSize, 32 * 1024 * 1024,
                         /* no "insert volume" requester may block a program
                          * that probes POSIX paths */
                         NP_WindowPtr, (IPTR)-1, TAG_END);
    if (rc == -1) {
        Close(in);
        Close(out);
        UnLock(here);
        printf("Launch: SystemTags failed ioerr=%ld\n", (long)IoErr());
        return 20;
    }
    printf("Launch: started\n");
    return 0;
}
