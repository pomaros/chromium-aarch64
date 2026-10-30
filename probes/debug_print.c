/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 *
 * Kernel debug channel output for C++ programs whose headers clash with
 * <aros/debug.h>.
 */
#define DEBUG 1
#include <aros/debug.h>

void v8hello_debug(const char *line)
{
    bug("%s", line);
}
