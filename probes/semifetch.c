// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// SemiFetch: copy a file from the QEMU host into the guest through Arm
// semihosting (SYS_OPEN, SYS_FLEN, SYS_READ). Only under QEMU with
// -semihosting-config enable=on,target=native,userspace=on: on hardware the
// HLT instruction traps. Lets a guest receive files larger than its boot
// bundle can carry.
//
//   SemiFetch HOSTPATH AROSPATH [VERIFY]
//
// VERIFY reads the copy back and compares it with the host file.

#include <proto/dos.h>
#include <proto/exec.h>
#include <exec/memory.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYS_OPEN 0x01
#define SYS_CLOSE 0x02
#define SYS_READ 0x06
#define SYS_FLEN 0x0C

static intptr_t semihost(uintptr_t op, void *args)
{
    register uintptr_t x0 __asm__("x0") = op;
    register void *x1 __asm__("x1") = args;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return (intptr_t)x0;
}

int main(int argc, char **argv)
{
    enum { CHUNK = 4 << 20 };

    if (argc != 3 && argc != 4) {
        printf("usage: SemiFetch HOSTPATH AROSPATH [VERIFY]\n");
        return 10;
    }
    const char *host = argv[1];
    uintptr_t open_args[3] = { (uintptr_t)host, 1 /* "rb" */, strlen(host) };
    intptr_t handle = semihost(SYS_OPEN, open_args);
    if (handle < 0) {
        printf("SemiFetch: cannot open host file %s\n", host);
        return 20;
    }
    uintptr_t flen_args[1] = { (uintptr_t)handle };
    intptr_t length = semihost(SYS_FLEN, flen_args);
    if (length < 0) {
        printf("SemiFetch: no length for %s\n", host);
        return 20;
    }
    UBYTE *buffer = AllocVec(CHUNK, MEMF_ANY);
    BPTR out = Open((CONST_STRPTR)argv[2], MODE_NEWFILE);
    if (!buffer || !out) {
        printf("SemiFetch: cannot create %s\n", argv[2]);
        return 20;
    }
    intptr_t done = 0;
    while (done < length) {
        size_t want = length - done < CHUNK ? (size_t)(length - done) : CHUNK;
        uintptr_t read_args[3] = { (uintptr_t)handle, (uintptr_t)buffer, want };
        intptr_t left = semihost(SYS_READ, read_args);   /* bytes NOT read */
        size_t got = want - (size_t)left;
        if (left < 0 || got == 0 || Write(out, buffer, got) != (LONG)got) {
            printf("SemiFetch: read or write failed at %ld of %ld\n", (long)done, (long)length);
            Close(out);
            return 20;
        }
        done += got;
    }
    Close(out);
    if (argc == 4) {
        /* Read both again and compare */
        UBYTE *copy = AllocVec(CHUNK, MEMF_ANY);
        BPTR in = Open((CONST_STRPTR)argv[2], MODE_OLDFILE);
        uintptr_t seek_args[2] = { (uintptr_t)handle, 0 };
        semihost(0x0A /* SYS_SEEK */, seek_args);
        intptr_t at = 0;
        while (copy && in && at < length) {
            size_t want = length - at < CHUNK ? (size_t)(length - at) : CHUNK;
            uintptr_t read_args[3] = { (uintptr_t)handle, (uintptr_t)buffer, want };
            size_t got = want - (size_t)semihost(SYS_READ, read_args);
            LONG mine = Read(in, copy, got);
            if (got == 0 || mine != (LONG)got || memcmp(buffer, copy, got) != 0) {
                size_t i = 0;
                while (mine > 0 && i < got && buffer[i] == copy[i]) i++;
                printf("SemiFetch: VERIFY FAIL at offset %ld (host %lu bytes, guest %ld)\n",
                       (long)(at + i), (unsigned long)got, (long)mine);
                return 20;
            }
            at += got;
        }
        printf("SemiFetch: VERIFY ok %ld bytes\n", (long)at);
        if (in) Close(in);
    }
    uintptr_t close_args[1] = { (uintptr_t)handle };
    semihost(SYS_CLOSE, close_args);
    FreeVec(buffer);
    printf("SemiFetch: %s -> %s %ld bytes\n", host, argv[2], (long)done);
    return 0;
}
