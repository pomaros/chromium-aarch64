// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// RecLockProbe: POSIX advisory record locks through posixc's fcntl(), as
// SQLite and LevelDB use them. The parent locks a file and starts a second
// process (this program with CHILD) that tries the same ranges; a pthread of
// the parent checks that threads of one process share its locks. Prints
// [RECLOCK] lines and result=PASS. Also: a lock follows its file through a
// rename, a new file at the old name is free, closing a dup()ed descriptor
// releases the process's locks, and ranges that overflow are refused.
//
//   RecLockProbe FILE           the test
//   RecLockProbe CHILD FILE OUT one probe of the other process, into OUT

#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(const char *what, int ok)
{
    printf("[RECLOCK] %s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static int setlk(int fd, int cmd, short type, off_t start, off_t len)
{
    struct flock fl = {0};
    fl.l_type = type;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    return fcntl(fd, cmd, &fl);
}

/* The other process: try a write lock on [start, start+len) and report. */
static int child(const char *file, const char *out, const char *range)
{
    long start = 0, len = 0;
    sscanf(range, "%ld,%ld", &start, &len);
    int fd = open(file, O_RDWR);
    FILE *o = fopen(out, "w");
    if (fd < 0 || !o) {
        printf("[RECLOCK] child: open %s fd=%d out=%p errno=%d\n", file, fd, (void *)o, errno);
        return 20;
    }
    struct flock fl = {0};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    int r = fcntl(fd, F_GETLK, &fl);
    fprintf(o, "getlk=%d type=%d start=%ld len=%ld pid=%ld\n", r, fl.l_type,
            (long)fl.l_start, (long)fl.l_len, (long)fl.l_pid);
    int s = setlk(fd, F_SETLK, F_WRLCK, start, len);
    fprintf(o, "setlk=%d errno=%d\n", s, s ? errno : 0);
    fclose(o);
    close(fd);
    return 0;
}

/* Run the other process on [start, start+len) and read its two lines. */
static int other(const char *self, const char *file, long start, long len,
                 char *getlk, char *setlk_line)
{
    char cmd[512], out[] = "RAM:reclock-child.txt";
    DeleteFile((CONST_STRPTR)out);
    snprintf(cmd, sizeof cmd, "%s CHILD %s %s %ld,%ld", self, file, out, start, len);
    LONG rc = SystemTags((CONST_STRPTR)cmd, TAG_END);
    FILE *f = fopen(out, "r");
    if (!f) {
        printf("[RECLOCK] other: rc=%ld ioerr=%ld no %s\n", (long)rc, (long)IoErr(), out);
        return 0;
    }
    int ok = fgets(getlk, 128, f) && fgets(setlk_line, 128, f);
    fclose(f);
    return ok;
}

static int thread_fd;
static int thread_result;
static void *thread_main(void *arg)
{
    (void)arg;
    thread_result = setlk(thread_fd, F_SETLK, F_WRLCK, 0, 10);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "CHILD") == 0)
        return child(argv[2], argv[3], argv[4]);
    if (argc != 2) {
        printf("usage: RecLockProbe FILE\n");
        return 10;
    }
    /* argv[0] may be a POSIX path; the Shell needs the AROS one */
    char self[512], prog[108];
    if (!NameFromLock(GetProgramDir(), (STRPTR)self, sizeof self) ||
        !GetProgramName((STRPTR)prog, sizeof prog) ||
        !AddPart((STRPTR)self, FilePart((STRPTR)prog), sizeof self))
        snprintf(self, sizeof self, "%s", argv[0]);
    const char *file = argv[1];
    char g[128], s[128];

    int fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    check("open", fd >= 0);
    if (fd < 0)
        return 20;
    write(fd, "0123456789abcdefghij", 20);

    check("write lock [0,10)", setlk(fd, F_SETLK, F_WRLCK, 0, 10) == 0);
    struct flock fl = {0};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_len = 10;
    check("own lock does not conflict (F_GETLK)", fcntl(fd, F_GETLK, &fl) == 0 && fl.l_type == F_UNLCK);

    check("other process ran", other(self, file, 0, 10, g, s));
    printf("[RECLOCK] other: %s[RECLOCK] other: %s", g, s);
    check("other process sees the write lock", strstr(g, "type=1 start=0 len=10") != NULL);
    check("other process is refused (EAGAIN)", strstr(s, "setlk=-1") != NULL &&
                                                  strstr(s, "errno=") != NULL && !strstr(s, "errno=0"));

    check("other process gets a free range [10,20)", other(self, file, 10, 10, g, s) && strstr(s, "setlk=0"));

    pthread_t t;
    thread_fd = fd;
    pthread_create(&t, NULL, thread_main, NULL);
    pthread_join(t, NULL);
    check("a thread shares the process's lock", thread_result == 0);

    check("unlock [3,6) splits the lock", setlk(fd, F_SETLK, F_UNLCK, 3, 3) == 0);
    check("other process gets [3,6)", other(self, file, 3, 3, g, s) && strstr(s, "setlk=0"));
    check("other process still refused [0,3)", other(self, file, 0, 3, g, s) && strstr(s, "setlk=-1"));

    close(fd);
    check("close releases the locks", other(self, file, 0, 10, g, s) && strstr(s, "setlk=0"));

    /* A lock follows its file through a rename; the old name is a new file */
    char moved[512];
    snprintf(moved, sizeof moved, "%s.moved", file);
    remove(moved);
    fd = open(file, O_RDWR);
    check("lock before rename", setlk(fd, F_SETLK, F_WRLCK, 0, 10) == 0);
    check("rename the open file", rename(file, moved) == 0);
    check("other process refused at the new name", other(self, moved, 0, 10, g, s) && strstr(s, "setlk=-1"));
    int fresh = open(file, O_RDWR | O_CREAT, 0644);
    check("a new file at the old name", fresh >= 0);
    check("the new file is free", other(self, file, 0, 10, g, s) && strstr(s, "setlk=0"));
    close(fresh);
    remove(file);

    /* Closing any descriptor of the file releases the process's locks */
    char duped[512];
    snprintf(duped, sizeof duped, "%s.dup", file);
    int dfd = open(duped, O_RDWR | O_CREAT, 0644);
    check("lock a second file", dfd >= 0 && setlk(dfd, F_SETLK, F_WRLCK, 0, 10) == 0);
    int twin = dup(dfd);
    check("dup", twin >= 0);
    check("other process refused before the dup is closed", other(self, duped, 0, 10, g, s) && strstr(s, "setlk=-1"));
    close(twin);
    check("closing a dup releases the locks", other(self, duped, 0, 10, g, s) && strstr(s, "setlk=0"));
    close(dfd);
    remove(duped);

    /* Ranges that overflow are refused, the table untouched */
    struct flock big = {0};
    big.l_type = F_WRLCK;
    big.l_whence = SEEK_SET;
    big.l_start = INT64_MAX;
    big.l_len = 2;
    errno = 0;
    check("start + len overflow refused", fcntl(fd, F_SETLK, &big) == -1 && errno == EOVERFLOW);
    big.l_start = 0;
    big.l_len = INT64_MIN;
    errno = 0;
    check("negative length before 0 refused", fcntl(fd, F_SETLK, &big) == -1 && errno == EINVAL);
    big.l_whence = SEEK_CUR;
    big.l_start = INT64_MAX;
    big.l_len = 1;
    lseek(fd, 5, SEEK_SET);
    errno = 0;
    check("position + start overflow refused", fcntl(fd, F_SETLK, &big) == -1 && errno == EOVERFLOW);
    close(fd);
    remove(moved);

    printf("[RECLOCK] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
