/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 *
 * AROS loads an ET_REL executable one allocatable section at a time, each
 * section becoming a page-aligned hunk in the W^X arena. A large C++ program
 * keeps tens of thousands of COMDAT sections, which exhausts the arena and
 * slows LoadSeg. Merge the section families once, at a relocatable pre-link,
 * keeping unwind tables and constants beside the code that references them
 * PC-relatively and writable data on its own.
 */
SECTIONS
{
  .text : {
    *(.text .text.* __lcxx_override)
    *(.rodata .rodata.*)
    *(.eh_frame)
    *(.gcc_except_table .gcc_except_table.*)
  }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
}
