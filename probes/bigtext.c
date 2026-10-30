/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper */
#define DEBUG 1
#include <aros/debug.h>
#include <stdio.h>
extern void bigtext_pad(void);
int main(void)
{
    bigtext_pad();
    printf("[BIGTEXT] result=PASS\n");
    bug("[BIGTEXT] result=PASS\n");
    return 0;
}
