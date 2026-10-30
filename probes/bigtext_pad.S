/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * 28 MiB of code: measures the executable page arena, nothing else. */
.text
.globl bigtext_pad
.p2align 2
bigtext_pad:
.rept 7340032
nop
.endr
ret
