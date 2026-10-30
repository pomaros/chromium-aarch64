// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// DevRandomProbe: /dev/urandom, /dev/random and /dev/zero through posixc
// (RANDOM: and ZERO: of random-handler). Two reads of /dev/urandom must
// differ and not be all zero, /dev/zero must read zeros, and a write to
// /dev/urandom must be accepted. Prints [DEVRANDOM] lines and result=PASS.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
    printf("[DEVRANDOM] %s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static int zeros(const unsigned char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (b[i])
            return 0;
    return 1;
}

int main(void)
{
    unsigned char a[4096], b[4096], z[4096];

    int fd = open("/dev/urandom", O_RDONLY);
    check("open /dev/urandom", fd >= 0);
    check("read 4096", read(fd, a, sizeof a) == (ssize_t)sizeof a);
    check("read again", read(fd, b, sizeof b) == (ssize_t)sizeof b);
    check("not all zero", !zeros(a, sizeof a));
    check("two reads differ", memcmp(a, b, sizeof a) != 0);
    close(fd);

    fd = open("/dev/random", O_RDONLY);
    check("open /dev/random", fd >= 0 && read(fd, a, 32) == 32 && !zeros(a, 32));
    close(fd);

    fd = open("/dev/urandom", O_WRONLY);
    check("write to /dev/urandom", fd >= 0 && write(fd, "aros", 4) == 4);
    close(fd);

    memset(z, 0xa5, sizeof z);
    fd = open("/dev/zero", O_RDONLY);
    check("open /dev/zero", fd >= 0);
    check("zeros", read(fd, z, sizeof z) == (ssize_t)sizeof z && zeros(z, sizeof z));
    close(fd);

    FILE *f = fopen("/dev/urandom", "rb");
    check("stdio fread", f && fread(a, 1, 100, f) == 100);
    if (f)
        fclose(f);

    printf("[DEVRANDOM] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
