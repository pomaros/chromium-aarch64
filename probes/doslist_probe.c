// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// DosListProbe NAME...: whether each name is a DOS device, volume or assign,
// read from the DOS list as posixc's path check does (no requester can
// open). Prints [DOSLIST] NAME found|missing.

#include <dos/dosextens.h>
#include <proto/dos.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        struct DosList *dl = LockDosList(LDF_ALL | LDF_READ);
        int found = FindDosEntry(dl, (CONST_STRPTR)argv[i], LDF_ALL) != NULL;
        UnLockDosList(LDF_ALL | LDF_READ);
        printf("[DOSLIST] %s %s\n", argv[i], found ? "found" : "missing");
    }
    return 0;
}
