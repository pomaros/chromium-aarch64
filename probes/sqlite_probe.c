// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// SQLiteProbe: SQLite's unix VFS over posixc, built with Chromium's SQLite
// configuration (exclusive locking, TRUNCATE journal, 4 KiB pages, no mmap,
// no WAL). Creates a database, fills it in a transaction, closes it, reopens
// it and reads it back the way Chromium's History database starts. Prints
// [SQLITE] lines and result=PASS.
//
//   SQLiteProbe PATH       a POSIX path, e.g. /T/sqlite-probe.db

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "sqlite3.h"

static int failures;

static void check(const char *what, int ok, sqlite3 *db)
{
    printf("[SQLITE] %s %s", what, ok ? "ok" : "FAIL");
    if (!ok && db)
        printf(" (%d %s)", sqlite3_extended_errcode(db), sqlite3_errmsg(db));
    printf("\n");
    if (!ok)
        failures++;
}

static int exec(sqlite3 *db, const char *sql)
{
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

static long count(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st;
    long n = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    if (sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

int main(int argc, char **argv)
{
    sqlite3 *db = NULL;
    char header[16] = {0};

    if (argc != 2) {
        printf("usage: SQLiteProbe PATH\n");
        return 10;
    }
    const char *path = argv[1];
    unlink(path);
    check("initialize", sqlite3_initialize() == SQLITE_OK, NULL);

    check("open", sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK, db);
    check("page_size", exec(db, "PRAGMA page_size=4096"), db);
    check("journal_mode", exec(db, "PRAGMA journal_mode=TRUNCATE"), db);
    check("schema probe on an empty file",
          count(db, "SELECT COUNT(*) FROM sqlite_schema") == 0, db);
    check("create", exec(db, "CREATE TABLE urls(id INTEGER PRIMARY KEY, url TEXT, visits INTEGER)"), db);
    check("begin", exec(db, "BEGIN"), db);
    for (int i = 0; i < 2000; i++) {
        char sql[160];
        snprintf(sql, sizeof sql, "INSERT INTO urls(url, visits) VALUES('https://example.org/%d', %d)", i, i);
        if (!exec(db, sql)) {
            check("insert", 0, db);
            break;
        }
    }
    check("commit", exec(db, "COMMIT"), db);
    check("count before close", count(db, "SELECT COUNT(*) FROM urls") == 2000, db);
    check("close", sqlite3_close(db) == SQLITE_OK, NULL);

    FILE *f = fopen(path, "rb");
    check("file readable", f != NULL, NULL);
    if (f) {
        check("header read", fread(header, 1, 16, f) == 16, NULL);
        fclose(f);
    }
    check("header is SQLite's", memcmp(header, "SQLite format 3", 16) == 0, NULL);

    db = NULL;
    check("reopen", sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK, db);
    check("schema lookup", count(db, "SELECT COUNT(*) FROM sqlite_schema WHERE type='table' AND name='urls'") == 1, db);
    check("count after reopen", count(db, "SELECT COUNT(*) FROM urls") == 2000, db);
    check("sum after reopen", count(db, "SELECT SUM(visits) FROM urls") == 1999L * 2000 / 2, db);
    check("integrity", count(db, "SELECT COUNT(*) FROM pragma_integrity_check WHERE integrity_check='ok'") == 1, db);
    check("close again", sqlite3_close(db) == SQLITE_OK, NULL);

    printf("[SQLITE] result=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 20 : 0;
}
