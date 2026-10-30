// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// NoVolumeProbe: a POSIX path whose volume does not exist fails with ENOENT,
// from the main task and from a pthread, without an "insert volume"
// requester (which would block until someone answers it). A path on an
// existing volume still works. Prints [NOVOLUME] lines and result=PASS.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
    printf("[NOVOLUME] %s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void probes(const char *who)
{
    char what[96];
    struct stat st;
    errno = 0;
    snprintf(what, sizeof what, "%s: open /proc/cpuinfo is ENOENT", who);
    check(what, open("/proc/cpuinfo", O_RDONLY) == -1 && errno == ENOENT);
    errno = 0;
    snprintf(what, sizeof what, "%s: stat /dev/shm/x is ENOENT", who);
    check(what, stat("/dev/shm/x", &st) == -1 && errno == ENOENT);
    errno = 0;
    snprintf(what, sizeof what, "%s: access /nosuchvolume/x is ENOENT", who);
    check(what, access("/nosuchvolume/x", F_OK) == -1 && errno == ENOENT);
}

static void *thread_main(void *arg)
{
    (void)arg;
    probes("thread");
    return NULL;
}

int main(void)
{
    probes("main");
    pthread_t t;
    pthread_create(&t, NULL, thread_main, NULL);
    pthread_join(t, NULL);
    static const char *existing[] = {"/RAM/novolume-probe.txt", "/T/novolume-probe.txt", "/SYS/novolume-probe.txt"};
    for (unsigned i = 0; i < 3; i++) {
        errno = 0;
        int fd = open(existing[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        char what[96];
        snprintf(what, sizeof what, "existing volume opens %s (errno=%d)", existing[i], fd >= 0 ? 0 : errno);
        check(what, fd >= 0);
        if (fd >= 0) {
            close(fd);
            unlink(existing[i]);
        }
    }
    printf("[NOVOLUME] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
