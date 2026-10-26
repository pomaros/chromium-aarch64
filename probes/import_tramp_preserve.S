// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Resolver uses ordinary AAPCS64; the final target retains preserve_most GPRs.
.text
.p2align 4
.globl ArosResolveTrampImport
ArosResolveTrampImport:
stp x29,x30,[sp,#-16]!
mov x29,sp
bl ResolveBody
mov x1, #0
mov x2, #0
mov x3, #0
mov x4, #0
mov x5, #0
mov x6, #0
mov x7, #0
mov x8, #0
mov x9, #0
mov x10, #0
mov x11, #0
mov x12, #0
mov x13, #0
mov x14, #0
mov x15, #0
movi v0.16b, #0
movi v1.16b, #0
movi v2.16b, #0
movi v3.16b, #0
movi v4.16b, #0
movi v5.16b, #0
movi v6.16b, #0
movi v7.16b, #0
ldp x29,x30,[sp],#16
ret
.p2align 4
.globl real_preserve
real_preserve:
add x0,x0,#7
ret
.p2align 4
.globl check_preserve
check_preserve:
stp x29,x30,[sp,#-16]!
mov x29,sp
mov x9, #409
mov x10, #410
mov x11, #411
mov x12, #412
mov x13, #413
mov x14, #414
mov x15, #415
mov x0,#55
bl tramp_preserve
mov x16,#0
cmp x0,#62
cset x17,ne
orr x16,x16,x17
cmp x9,#409
cset x17,ne
orr x16,x16,x17,lsl #1
cmp x10,#410
cset x17,ne
orr x16,x16,x17,lsl #2
cmp x11,#411
cset x17,ne
orr x16,x16,x17,lsl #3
cmp x12,#412
cset x17,ne
orr x16,x16,x17,lsl #4
cmp x13,#413
cset x17,ne
orr x16,x16,x17,lsl #5
cmp x14,#414
cset x17,ne
orr x16,x16,x17,lsl #6
cmp x15,#415
cset x17,ne
orr x16,x16,x17,lsl #7
mov x0,x16
ldp x29,x30,[sp],#16
ret
.section .note.GNU-stack,"",@progbits
