/**
 * @file import_test.c
 * @brief Import against a real server (RULES.md R-TEST-8): delimited text
 * read a record at a time and inserted into a table, by copy or by INSERT,
 * a batch to each transaction; bad rows refused or skipped; what stopped
 * an import, where, and what of it was kept. Opt-in: TRL_TEST_SERVER
 * names the server, else skipped.
 */
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
#define TEXT_BYTES 512U
/* A field longer than a large object a server sends whole. */
#define LONG_FIELD 70000U

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    if (ran != TRL_OK) {
        char line[PATH_BYTES];
        (void)trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        (void)fprintf(stderr, "%s: %s\n", sql, line);
    }
    trl_result_free(result);
    return ran == TRL_OK;
}

/* The count of rows of `table`, or -1. */
static long long count_of(trl_conn *conn, const char *table) {
    char sql[PATH_BYTES];
    (void)snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM %s", table);
    trl_result *result = NULL;
    trl_decimal count;
    long long n = -1;
    if (trl_query(conn, sql, &result) == TRL_OK &&
        trl_value_decimal(trl_result_value(result, 0U, 0U), &count) == TRL_OK) {
        n = (long long)count.units.low;
    }
    trl_result_free(result);
    return n;
}

/* Text read from memory, a few bytes at a time. */
typedef struct text_input {
    const char *text;
    size_t length;
    size_t at;
} text_input;

static bool read_text(void *context, uint8_t *into, size_t capacity, size_t *length) {
    text_input *in = context;
    size_t left = in->length - in->at;
    size_t n = left < capacity ? left : capacity;
    n = n < 5U ? n : 5U;
    memcpy(into, in->text + in->at, n);
    in->at += n;
    *length = n;
    return true;
}

/* Imports `text`, written as `dialect` says, as `how` says. */
static trl_status import_text(trl_conn *conn, const char *text, const trl_dialect *dialect, const trl_import *how,
                              trl_imported **out) {
    text_input in = {text, strlen(text), 0U};
    trl_delimited *delimited = NULL;
    if (trl_delimited_new(read_text, &in, dialect, &delimited) != TRL_OK) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_source source = trl_delimited_source(delimited);
    trl_status status = trl_import_rows(conn, &source, how, out);
    trl_delimited_free(delimited);
    return status;
}

/* Whether what stopped an import writes as `expected`. */
static bool stopped_as(const trl_imported *imported, const char *expected) {
    char text[TEXT_BYTES];
    (void)trl_imported_text(imported, text, sizeof text);
    if (strcmp(text, expected) != 0) {
        (void)fprintf(stderr, "stopped as \"%s\"\n", text);
        return false;
    }
    return true;
}

/**
 * Verifies: [CLIENT Importing] CSV with a header naming the columns
 * imported by copy, the server granting it; a string too long for its
 * column refused, not cut, its record and line said, the batch before it
 * kept; spaces beyond the length cut [CLIENT Copying and exporting].
 */
static void csv_is_imported_by_copy(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE PEOPLE (ID INTEGER, NAME VARCHAR(5))") && run(conn, "COMMIT"));
    trl_dialect csv = trl_dialect_csv();
    csv.header = true;
    trl_import how = {"people", TRL_IMPORT_BATCH, false, NULL, NULL};
    trl_imported *imported = NULL;
    TRL_CHECK(test,
              import_text(conn, "id,name\r\n1,Ada\r\n2,\"Bri,n\"\r\n3,Dan   \n", &csv, &how, &imported) == TRL_OK);
    TRL_CHECK(test, trl_imported_rows(imported) == 3U && count_of(conn, "PEOPLE") == 3 && run(conn, "COMMIT"));
    trl_imported_free(imported);
    how.batch = 2U;
    TRL_CHECK(test, import_text(conn, "id,name\n4,Eve\n5,Fay\n6,Charlotte\n", &csv, &how, &imported) == TRL_FAILED);
    TRL_CHECK(test, trl_imported_rows(imported) == 2U && trl_imported_rolled_back(imported) == 0U);
    trl_cause cause;
    TRL_CHECK(test, trl_imported_cause(imported, &cause) && cause.sql);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(cause.failure), "22001") == 0);
    TRL_CHECK(test,
              stopped_as(imported, "record 3, line 4: SQLCODE -301 (SQLSTATE 22001): a value of 9 characters, not "
                                   "spaces beyond 5, does not fit CHARACTER VARYING(5) [ISO/IEC 9075:1992 "
                                   "\xc2\xa7"
                                   "13.8 GR4b iii, \xc2\xa7"
                                   "9.2 GR3e ii]; 2 rows committed, 0 rolled back"));
    trl_position at;
    TRL_CHECK(test, trl_imported_position(imported, &at) && at.record == 3U && at.line == 4U);
    trl_imported_free(imported);
    TRL_CHECK(test, count_of(conn, "PEOPLE") == 5);
    trl_conn_free(conn);
}

/* Counts the rows skipped, keeping the last one's record. */
static void skipped(void *context, const trl_rejected *row) {
    uint64_t *last = context;
    *last = row->position.record;
}

/**
 * Verifies: [CLIENT Importing] a row that fails skipped and given to the
 * program, if asked, by INSERT; without skipping, a failing row stops the
 * import, the batch under way rolled back, the batches before kept; a
 * record of too many fields refused.
 */
static void bad_rows_are_skipped_or_stop(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE KEYED (ID INTEGER PRIMARY KEY, NAME VARCHAR(5))") && run(conn, "COMMIT"));
    trl_dialect csv = trl_dialect_csv();
    uint64_t last = 0U;
    trl_import how = {"KEYED", TRL_IMPORT_BATCH, true, skipped, &last};
    trl_imported *imported = NULL;
    TRL_CHECK(test, import_text(conn, "1,a\n1,b\n2,c,d\n3,e\n", &csv, &how, &imported) == TRL_OK);
    TRL_CHECK(test, trl_imported_rows(imported) == 2U && trl_imported_rejected(imported) == 2U && last == 3U);
    trl_imported_free(imported);
    how.skip = false;
    how.batch = 2U;
    TRL_CHECK(test, import_text(conn, "10,a\n11,b\n12,c\n10,d\n", &csv, &how, &imported) == TRL_FAILED);
    TRL_CHECK(test, trl_imported_rows(imported) == 2U && trl_imported_rolled_back(imported) == 1U);
    trl_position at;
    TRL_CHECK(test, trl_imported_position(imported, &at) && at.record == 4U);
    trl_imported_free(imported);
    TRL_CHECK(test, !trl_conn_in_transaction(conn) && count_of(conn, "KEYED") == 4 && run(conn, "COMMIT"));
    TRL_CHECK(test, import_text(conn, "20,a,b\n", &csv, &how, &imported) == TRL_FAILED);
    TRL_CHECK(test, stopped_as(imported, "record 1, line 1: [DESIGN] 3 fields, for 2 columns; 0 rows committed, 0 "
                                         "rolled back"));
    trl_imported_free(imported);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Importing] tab-separated text with a text for null,
 * its fields the table's columns in order; a field longer than 64 KiB for
 * a large object column sent by creating a large object
 * [CLIENT Large objects].
 */
static void tsv_and_long_fields(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE NOTES (ID INTEGER, BODY CLOB(1M))") && run(conn, "COMMIT"));
    trl_dialect tsv = trl_dialect_tsv();
    tsv.null = "\\N";
    static char text[LONG_FIELD + 64U];
    size_t used = (size_t)snprintf(text, sizeof text, "1\t\\N\n2\t");
    memset(text + used, 'x', LONG_FIELD);
    memcpy(text + used + LONG_FIELD, "\n", 2U);
    trl_import how = {"NOTES", TRL_IMPORT_BATCH, false, NULL, NULL};
    trl_imported *imported = NULL;
    TRL_CHECK(test, import_text(conn, text, &tsv, &how, &imported) == TRL_OK && trl_imported_rows(imported) == 2U);
    trl_imported_free(imported);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM NOTES ORDER BY ID", &result) == TRL_OK);
    trl_lob_info info;
    TRL_CHECK(test, trl_value_is_null(trl_result_value(result, 0U, 0U)));
    TRL_CHECK(test, trl_value_lob(trl_result_value(result, 1U, 0U), &info) == TRL_OK && info.octets == LONG_FIELD);
    trl_result_free(result);
    TRL_CHECK(test, run(conn, "COMMIT"));
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Importing] an import begins with no transaction
 * active; a table not named right, input not well formed, said with where
 * it began.
 */
static void imports_refused_before_rows(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_dialect csv = trl_dialect_csv();
    trl_import how = {"1x", TRL_IMPORT_BATCH, false, NULL, NULL};
    trl_imported *imported = NULL;
    TRL_CHECK(test, import_text(conn, "1\n", &csv, &how, &imported) == TRL_FAILED);
    TRL_CHECK(test, stopped_as(imported, "[ISO/IEC 9075:1992 \xc2\xa7"
                                         "5.4] 1x is not a <table name>; 0 rows "
                                         "committed, 0 rolled back"));
    trl_imported_free(imported);
    TRL_CHECK(test, run(conn, "CREATE TABLE QUOTED (A VARCHAR(5))"));
    how.table = "QUOTED";
    TRL_CHECK(test, import_text(conn, "a\n", &csv, &how, &imported) == TRL_FAILED);
    trl_cause cause;
    TRL_CHECK(test, trl_imported_cause(imported, &cause) && !cause.sql &&
                        strstr(cause.message, "no transaction active") != NULL);
    trl_imported_free(imported);
    TRL_CHECK(test, run(conn, "COMMIT"));
    TRL_CHECK(test, import_text(conn, "a\nb\"c\n", &csv, &how, &imported) == TRL_FAILED);
    TRL_CHECK(test, stopped_as(imported, "record 2, line 2: [RFC 4180 \xc2\xa7"
                                         "2 rule 5] field 1: a quote in a field "
                                         "not enclosed in quotes; 0 rows committed, 1 rolled back"));
    trl_imported_free(imported);
    TRL_CHECK(test, import_text(conn, "\"open\n", &csv, &how, &imported) == TRL_FAILED);
    TRL_CHECK(test, trl_imported_cause(imported, &cause) && strstr(cause.message, "not closed") != NULL);
    trl_imported_free(imported);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-import"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    (void)snprintf(dir, sizeof dir, "%s/live-import", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(csv_is_imported_by_copy),
        TRL_TEST_CASE(bad_rows_are_skipped_or_stop),
        TRL_TEST_CASE(tsv_and_long_fields),
        TRL_TEST_CASE(imports_refused_before_rows),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
