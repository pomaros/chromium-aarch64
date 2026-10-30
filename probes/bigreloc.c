/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Checks that every relocated table entry points at the table. */
#define DEBUG 1
#include <aros/debug.h>
#include <stdio.h>
extern void *bigreloc_table[];
extern unsigned long bigreloc_count;
int main(void)
{
    unsigned long bad = 0, i;
    for (i = 0; i < bigreloc_count; i++)
        if (bigreloc_table[i] != (void *)bigreloc_table) bad++;
    printf("[BIGRELOC] entries=%lu bad=%lu\n", bigreloc_count, bad);
    bug("[BIGRELOC] entries=%lu bad=%lu\n", bigreloc_count, bad);
    if (bad) { printf("[BIGRELOC] result=FAIL\n"); bug("[BIGRELOC] result=FAIL\n"); return 10; }
    printf("[BIGRELOC] result=PASS\n"); bug("[BIGRELOC] result=PASS\n");
    return 0;
}
