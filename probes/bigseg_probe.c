// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// BigSegProbe: can the system hold a very large code segment? Repeats, for
// growing sizes, the two operations the ELF loader performs on an executable
// section on a W^X host: allocate it with KrnAllocPages(MEMF_EXECUTABLE),
// as LoadSeg's AllocFunc does, and turn it read+execute with
// KATTR_PageProtect. Prints one
// [BIGSEG] line per size and step, then result=PASS when every size works.

#include <stdint.h>
#include <stdio.h>

#include <aros/kernel.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/kernel.h>

void *KernelBase;

int main(void)
{
    static const unsigned mib[] = {64, 128, 192, 256, 320, 400};
    int failures = 0;

    KernelBase = OpenResource((CONST_STRPTR)"kernel.resource");
    intptr_t op = KernelBase ? KrnGetSystemAttr(KATTR_PageProtect) : -1;
    printf("[BIGSEG] PageProtect=%s\n", (op == -1 || op == 0) ? "absent" : "present");
    KRN_PageProtect protect = (op == -1 || op == 0) ? NULL : (KRN_PageProtect)op;

    for (unsigned i = 0; i < sizeof mib / sizeof mib[0]; i++) {
        ULONG size = mib[i] << 20;
        APTR p = KrnAllocPages(NULL, size, MEMF_EXECUTABLE | MEMF_PUBLIC | MEMF_CLEAR);
        printf("[BIGSEG] %u MiB alloc=%s typeofmem=%lu\n", mib[i], p ? "ok" : "FAIL",
               p ? (unsigned long)TypeOfMem(p) : 0UL);
        if (!p) {
            failures++;
            continue;
        }
        if (protect) {
            int ok = protect(p, size, MAP_Readable | MAP_Executable);
            printf("[BIGSEG] %u MiB protect rx=%s\n", mib[i], ok ? "ok" : "FAIL");
            if (!ok)
                failures++;
            protect(p, size, MAP_Readable | MAP_Writable);
        }
        KrnFreePages(p, size);
    }
    printf("[BIGSEG] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
