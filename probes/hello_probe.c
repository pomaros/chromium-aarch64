/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Smallest guest probe: proves the link recipe, console and debug channel. */
#define DEBUG 1
#include <aros/debug.h>
#include <stdio.h>
int main(void)
{
    printf("[HELLOPROBE] result=PASS\n");
    bug("[HELLOPROBE] result=PASS\n");
    return 0;
}
