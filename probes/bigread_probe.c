// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// BigReadProbe FILE: reads FILE the way the ELF loader does, one Read() per
// section: seeks to each section of the ELF64 file and reads it whole into
// one allocation, printing [BIGREAD] lines with the bytes obtained and IoErr().
// Shows whether a filesystem or driver limit stops reads of hundreds of MB.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: BigReadProbe FILE\n");
        return 10;
    }
    BPTR fh = Open((CONST_STRPTR)argv[1], MODE_OLDFILE);
    if (!fh) {
        printf("[BIGREAD] open FAIL ioerr=%ld\n", (long)IoErr());
        return 20;
    }
    unsigned char eh[64];
    LONG n = Read(fh, eh, sizeof eh);
    uint64_t shoff;
    uint16_t shentsize, shnum;
    memcpy(&shoff, eh + 0x28, 8);
    memcpy(&shentsize, eh + 0x3A, 2);
    memcpy(&shnum, eh + 0x3C, 2);
    printf("[BIGREAD] header read=%ld shoff=%llu shnum=%u\n", (long)n, (unsigned long long)shoff, shnum);
    int failures = 0;
    for (unsigned i = 1; i < shnum; i++) {
        unsigned char sh[64];
        Seek(fh, (LONG)(shoff + i * shentsize), OFFSET_BEGINNING);
        if (Read(fh, sh, 64) != 64) {
            printf("[BIGREAD] section header %u read FAIL ioerr=%ld\n", i, (long)IoErr());
            failures++;
            continue;
        }
        uint32_t type;
        uint64_t off, size;
        memcpy(&type, sh + 4, 4);
        memcpy(&off, sh + 24, 8);
        memcpy(&size, sh + 32, 8);
        if (type == 8 || size == 0)          /* NOBITS or empty */
            continue;
        APTR buf = AllocMem(size, MEMF_ANY);
        if (!buf) {
            printf("[BIGREAD] section %u size=%llu alloc FAIL\n", i, (unsigned long long)size);
            failures++;
            continue;
        }
        LONG pos = Seek(fh, (LONG)off, OFFSET_BEGINNING);
        LONG got = Read(fh, buf, (LONG)size);
        LONG err = IoErr();
        printf("[BIGREAD] section %u type=%u off=%llu size=%llu seek=%ld read=%ld ioerr=%ld %s\n", i, type,
               (unsigned long long)off, (unsigned long long)size, (long)pos, (long)got, (long)err,
               got == (LONG)size ? "ok" : "FAIL");
        if (got != (LONG)size)
            failures++;
        FreeMem(buf, size);
    }
    Close(fh);
    printf("[BIGREAD] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
