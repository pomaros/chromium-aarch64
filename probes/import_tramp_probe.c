/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 *
 * The AArch64 import trampolines of the shared engine libraries
 * (chromium-aros build/config/aros/import_trampolines_arm64.py), exercised
 * against local targets: every import is called twice, the first time
 * through the slow path that resolves it, the second through the cached
 * slot. The targets check the full AAPCS64 argument set: eight integer and
 * eight floating-point registers, stack arguments, the indirect-result
 * register x8 and a variadic call. A separate assembly caller retains seven
 * live values in x9-x15 across a preserve_most import; the resolver deliberately
 * clobbers them under ordinary AAPCS64. Cold and cached calls must both retain
 * those values. Link import_tramp_preserve.S with this source and five imports.
 * Prints [TRAMP] lines and result=PASS.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct big { long a, b, c, d; };

/* The imports, defined by the generated trampolines. */
long tramp_ints(long, long, long, long, long, long, long, long, long, long);
double tramp_floats(double, double, double, double, double, double, double,
                    double, double, long);
struct big tramp_big(long, double, long);
long tramp_variadic(int, ...);

static long real_ints(long a, long b, long c, long d, long e, long f, long g,
                      long h, long i, long j)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + 10 * j;
}

static double real_floats(double a, double b, double c, double d, double e,
                          double f, double g, double h, double i, long j)
{
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h + 9 * i + (double)j;
}

static struct big real_big(long x, double y, long z)
{
    struct big r = { x, (long)y, z, x + (long)y + z };
    return r;
}

static long real_variadic(int n, ...)
{
    va_list ap;
    long sum = 0;
    va_start(ap, n);
    for (int k = 0; k < n; k++)
        sum += (k + 1) * va_arg(ap, long);
    va_end(ap);
    return sum;
}

extern const size_t g_tramp_import_count;
extern const char *const g_tramp_import_names[];
extern void *g_tramp_import_slots[];

extern long real_preserve(long);
extern long check_preserve(void);
static int resolved;

void *ResolveBody(size_t index)
{
    static const struct { const char *name; void *target; } table[] = {
        { "tramp_ints", (void *)real_ints },
        { "tramp_floats", (void *)real_floats },
        { "tramp_big", (void *)real_big },
        { "tramp_variadic", (void *)real_variadic },
        { "tramp_preserve", (void *)real_preserve },
    };
    if (index >= g_tramp_import_count)
        return NULL;
    for (size_t k = 0; k < sizeof table / sizeof table[0]; k++) {
        if (strcmp(table[k].name, g_tramp_import_names[index]) == 0) {
            resolved++;
            __atomic_store_n(&g_tramp_import_slots[index], table[k].target, __ATOMIC_RELEASE);
            return table[k].target;
        }
    }
    return NULL;
}

static int failures;

static void check(const char *what, int round, int ok)
{
    printf("[TRAMP] %s round=%d %s\n", what, round, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int main(void)
{
    for (int round = 1; round <= 2; round++) {
        long mask = check_preserve();
        check("preserve-x9-x15-live-handle", round, mask == 0);
        printf("[TRAMP] preserve round=%d failure-mask=0x%lx\n", round, mask);
        long i = tramp_ints(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
        check("ints", round, i == 385);
        double f = tramp_floats(1.5, 2, 3, 4, 5, 6, 7, 8, 9.25, 11);
        check("floats", round, f == 1.5 + 4 + 9 + 16 + 25 + 36 + 49 + 64 + 83.25 + 11);
        struct big b = tramp_big(40, 2.0, 100);
        check("big", round, b.a == 40 && b.b == 2 && b.c == 100 && b.d == 142);
        long v = tramp_variadic(5, 1L, 2L, 3L, 4L, 5L);
        check("variadic", round, v == 55);
    }
    check("resolved-once", 0, resolved == 5);
    printf("[TRAMP] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
