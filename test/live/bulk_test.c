/**
 * @file bulk_test.c
 * @brief Bulk copy and export against a real server (RULES.md R-TEST-8):
 * rows copied all or nothing, abandoned, refused mid-copy, by column; a query's rows
 * exported as CSV and as native rows read back. Opt-in: TRL_TEST_SERVER
 * names the server, else skipped.
 */
#include "format.h"
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
#define OUT_BYTES 8192U

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
    trl_format(sql, sizeof sql, "SELECT COUNT(*) FROM %s", table);
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

/* A row of an id and a name. */
static void row_of(trl_params *row, uint64_t id, const char *name) {
    trl_params_clear(row);
    trl_decimal key = {{id, 0}, 0U};
    (void)trl_params_add_decimal(row, key);
    (void)trl_params_add_text(row, name, strlen(name));
}

/**
 * Verifies: [CLIENT Copying and exporting] a copy into a table's columns
 * named, its rows sent with no reply each, finished with the rows
 * inserted; a row refused undoes every row, its failure's detail naming
 * the row, the transaction under way kept, as a savepoint of the copy's
 * own were rolled back [PROTOCOL CopyEnd 'N'] [PROTOCOL Conversations].
 */
static void a_copy_is_all_or_nothing(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE PEOPLE (ID INTEGER PRIMARY KEY, NAME VARCHAR(5))") && run(conn, "COMMIT"));
    static const char *const columns[] = {"ID", "NAME"};
    trl_copy_target target = {"PEOPLE", columns, 2U};
    trl_copy *copy = NULL;
    trl_params *row = NULL;
    (void)trl_params_new(&row);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    static const char *const names[] = {"Ada", "Brian", "Chen"};
    for (uint64_t i = 0U; i < 3U; ++i) {
        row_of(row, i + 1U, names[i]);
        TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    }
    uint64_t rows = 0U;
    TRL_CHECK(test, trl_copy_finish(copy, &rows) == TRL_OK && rows == 3U);
    TRL_CHECK(test, trl_copy_finish(copy, &rows) == TRL_INVALID_ARGUMENT);
    trl_copy_free(copy);
    TRL_CHECK(test, run(conn, "COMMIT") && count_of(conn, "PEOPLE") == 3);
    TRL_CHECK(test, run(conn, "INSERT INTO PEOPLE VALUES (9, 'Ivo')"));
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    row_of(row, 4U, "Dora");
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    row_of(row, 1U, "Eve");
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    TRL_CHECK(test, trl_copy_finish(copy, &rows) == TRL_FAILED);
    const trl_failure *failure = trl_conn_failure(conn);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(failure), "23000") == 0 && !trl_conn_is_lost(conn));
    const char *detail = trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_DETAIL);
    TRL_CHECK(test, detail != NULL && strstr(detail, "row 2 of the copy") != NULL);
    trl_copy_free(copy);
    TRL_CHECK(test, trl_conn_in_transaction(conn) && count_of(conn, "PEOPLE") == 4 && run(conn, "ROLLBACK"));
    TRL_CHECK(test, count_of(conn, "PEOPLE") == 3);
    trl_params_free(row);
    trl_conn_free(conn);
}

/* Gives three rows, then the end, or fails at the second if `context` says. */
static trl_source_status three(void *context, trl_params *row) {
    int *given = context;
    if (*given < 0) {
        return TRL_SOURCE_FAILED;
    }
    if (*given == 3) {
        return TRL_SOURCE_END;
    }
    ++*given;
    row_of(row, 10U + (uint64_t)*given, "n");
    return TRL_SOURCE_ROW;
}

/**
 * Verifies: [CLIENT Copying and exporting] a copy abandoned, or freed
 * before it finishes, undoes its rows; until it ends only its rows, its
 * end and Ping are sent; the rows an iterator gives copied, committed as
 * the copy succeeds under autocommit [PROTOCOL CopyIn 'I'].
 */
static void copies_abandoned_and_iterated(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE NUMBERS (ID INTEGER, NAME VARCHAR(5))") && run(conn, "COMMIT"));
    trl_copy_target target = {"NUMBERS", NULL, 0U};
    trl_copy *copy = NULL;
    trl_params *row = NULL;
    (void)trl_params_new(&row);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    row_of(row, 1U, "one");
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "VALUES (1)", &result) == TRL_FAILED && result == NULL);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "TB910") == 0);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK);
    TRL_CHECK(test, trl_copy_abandon(copy) == TRL_OK);
    trl_copy_free(copy);
    TRL_CHECK(test, count_of(conn, "NUMBERS") == 0);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK && trl_copy_send(copy, row) == TRL_OK);
    trl_copy_free(copy);
    TRL_CHECK(test, count_of(conn, "NUMBERS") == 0 && run(conn, "COMMIT"));
    TRL_CHECK(test, trl_conn_set_autocommit(conn, true) == TRL_OK);
    int given = 0;
    uint64_t rows = 0U;
    TRL_CHECK(test, trl_copy_rows(conn, &target, three, &given, &rows) == TRL_OK && rows == 3U);
    TRL_CHECK(test, !trl_conn_in_transaction(conn) && count_of(conn, "NUMBERS") == 3);
    given = -1;
    TRL_CHECK(test, trl_copy_rows(conn, &target, three, &given, &rows) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "TB910") == 0);
    TRL_CHECK(test, count_of(conn, "NUMBERS") == 3);
    trl_params_free(row);
    trl_conn_free(conn);
}

/* A row by column: an id's text and a word's, null if `word` is. */
static void cells_of(trl_params *row, const char *id, const char *word) {
    trl_params_clear(row);
    (void)trl_params_add_text(row, id, strlen(id));
    if (word == NULL) {
        (void)trl_params_add_null(row);
    } else {
        (void)trl_params_add_text(row, word, strlen(word));
    }
}

/* Copies the rows of `ids` and `words` into WORDS, by column; the copy's end, and the rows inserted. */
static trl_status copied(trl_conn *conn, const char *const *ids, const char *const *words, size_t count,
                         uint64_t *rows) {
    static const char *const columns[] = {"ID", "WORD"};
    trl_copy_target target = {"WORDS", columns, 2U};
    trl_copy *copy = NULL;
    trl_params *row = NULL;
    (void)trl_params_new(&row);
    trl_status status = trl_copy_in(conn, &target, &copy);
    for (size_t i = 0U; status == TRL_OK && i < count; ++i) {
        cells_of(row, ids[i], words[i]);
        status = trl_copy_send(copy, row);
    }
    status = status == TRL_OK ? trl_copy_finish(copy, rows) : status;
    trl_copy_free(copy);
    trl_params_free(row);
    return status;
}

/**
 * Verifies: [PROTOCOL CopyColumns 'O'] granted `columns`, a copy's rows of
 * character strings go by column: each cell cast as its string would be
 * by row, a null kept apart from the empty string; a cell too long for
 * its column fails the copy at its end, its detail naming the row, every
 * row undone; rows by column and by row mixed in one copy; a copy by
 * column abandoned, none inserted [CLIENT Copying and exporting].
 */
static void a_copy_goes_by_column(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, trl_conn_granted(conn, TRL_CAPABILITY_COLUMNS));
    TRL_CHECK(test, run(conn, "CREATE TABLE WORDS (ID INTEGER, WORD VARCHAR(3))") && run(conn, "COMMIT"));
    static const char *const ids[] = {"1", "2", "3"};
    static const char *const words[] = {NULL, "", "abc"};
    uint64_t rows = 0U;
    TRL_CHECK(test, copied(conn, ids, words, 3U, &rows) == TRL_OK && rows == 3U && run(conn, "COMMIT"));
    TRL_CHECK(test, count_of(conn, "WORDS WHERE WORD IS NULL AND ID = 1") == 1 &&
                        count_of(conn, "WORDS WHERE WORD = '' AND ID = 2") == 1 &&
                        count_of(conn, "WORDS WHERE WORD = 'abc' AND ID = 3") == 1);
    static const char *const long_words[] = {"de", "defg", "d"};
    TRL_CHECK(test, copied(conn, ids, long_words, 3U, &rows) == TRL_FAILED && !trl_conn_is_lost(conn));
    const trl_failure *failure = trl_conn_failure(conn);
    const char *detail = trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_DETAIL);
    TRL_CHECK(test, strncmp(trl_failure_sqlstate(failure), "22", 2U) == 0 && detail != NULL &&
                        strstr(detail, "row 2 of the copy") != NULL);
    TRL_CHECK(test, run(conn, "ROLLBACK") && count_of(conn, "WORDS") == 3);
    static const char *const columns[] = {"ID", "WORD"};
    trl_copy_target target = {"WORDS", columns, 2U};
    trl_copy *copy = NULL;
    trl_params *row = NULL;
    (void)trl_params_new(&row);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    cells_of(row, "4", "gh");
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    row_of(row, 5U, "ij");
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    cells_of(row, "6", NULL);
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK && trl_copy_finish(copy, &rows) == TRL_OK && rows == 3U);
    trl_copy_free(copy);
    TRL_CHECK(test, run(conn, "COMMIT") && count_of(conn, "WORDS WHERE ID BETWEEN 4 AND 6") == 3 &&
                        count_of(conn, "WORDS WHERE ID = 5 AND WORD = 'ij'") == 1);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    cells_of(row, "7", "kl");
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK && trl_copy_abandon(copy) == TRL_OK);
    trl_copy_free(copy);
    TRL_CHECK(test, count_of(conn, "WORDS") == 6 && run(conn, "COMMIT"));
    trl_params_free(row);
    trl_conn_free(conn);
}

/* An export's bytes, gathered. */
typedef struct gathered {
    uint8_t bytes[OUT_BYTES];
    size_t length;
    size_t read;
    bool refuse;
} gathered;

/* Gathers an export's bytes, or refuses them if told. */
static bool gather(void *context, const uint8_t *bytes, size_t length) {
    gathered *g = context;
    if (g->refuse || g->length + length > sizeof g->bytes) {
        return false;
    }
    memcpy(g->bytes + g->length, bytes, length);
    g->length += length;
    return true;
}

/* Reads gathered bytes back, a few at a time. */
static size_t read_back(void *context, uint8_t *into, size_t capacity) {
    gathered *g = context;
    size_t left = g->length - g->read;
    size_t n = left < capacity ? left : capacity;
    n = n < 7U ? n : 7U;
    memcpy(into, g->bytes + g->read, n);
    g->read += n;
    return n;
}

/**
 * Verifies: [CLIENT Copying and exporting] a query exported as CSV: a
 * header of the columns' names, then a record for each row, each value as
 * turborel displays it, a field quoted where it must be, a null empty, an
 * empty string quoted; a statement that is no query, or a write that
 * fails, fails the export, the connection kept.
 */
static void a_query_is_exported_as_csv(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    static gathered g;
    memset(&g, 0, sizeof g);
    trl_export_sink sink = {TRL_EXPORT_CSV, gather, &g};
    uint64_t rows = 0U;
    const char *sql = "VALUES (1.50, 'a,b', 'say \"hi\"', ' pad', '', CAST(NULL AS INTEGER), 7.5E0, TRUE, "
                      "DATE '2024-02-29', TIMESTAMP '2024-02-29 13:45:06.789', INTERVAL '3 04:05' DAY TO MINUTE, "
                      "X'01FF', CAST(1.5 AS DECFLOAT), ARRAY[1, 2])";
    TRL_CHECK(test, trl_export(conn, sql, &sink, &rows) == TRL_OK && rows == 1U);
    g.bytes[g.length] = '\0';
    const char *expected_record =
        "1.50,\"a,b\",\"say \"\"hi\"\"\",\" pad\",\"\",,7.5E0,TRUE,2024-02-29,2024-02-29 13:45:06.789,3 04:05,01FF,"
        "1.5,\"ARRAY[1, 2]\"\r\n";
    const char *newline = strstr((const char *)g.bytes, "\r\n");
    TRL_CHECK(test, newline != NULL && strcmp(newline + 2, expected_record) == 0);
    if (newline != NULL && strcmp(newline + 2, expected_record) != 0) {
        (void)fprintf(stderr, "exported: %s\n", (const char *)g.bytes);
    }
    TRL_CHECK(test, trl_export(conn, "CREATE TABLE NONE (N INTEGER)", &sink, &rows) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "an export runs a query") == 0);
    g.refuse = true;
    TRL_CHECK(test, trl_export(conn, "VALUES (1), (2)", &sink, &rows) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "cannot write the export") == 0);
    TRL_CHECK(test, !trl_conn_is_lost(conn) && run(conn, "VALUES (1)"));
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Copying and exporting] a query exported as native
 * rows, the protocol's own frames, read back with every value as it was;
 * input that is not native rows refused (SQLSTATE 08006).
 */
static void native_rows_read_back(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    static gathered g;
    memset(&g, 0, sizeof g);
    trl_export_sink sink = {TRL_EXPORT_NATIVE, gather, &g};
    uint64_t rows = 0U;
    TRL_CHECK(test, trl_export(conn, "SELECT X, 'v' || CAST(X AS VARCHAR(3)) AS NAME FROM (VALUES (1), (2)) AS V(X)",
                               &sink, &rows) == TRL_OK &&
                        rows == 2U);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_read_native(read_back, &g, &result) == TRL_OK && trl_result_failure(result) == NULL);
    TRL_CHECK(test, trl_result_row_count(result) == 2U && trl_result_column_count(result) == 2U);
    TRL_CHECK(test, strcmp(trl_result_column_name(result, 1U), "NAME") == 0);
    size_t length = 0U;
    const char *name = trl_value_text(trl_result_value(result, 1U, 1U), &length);
    TRL_CHECK(test, name != NULL && length == 2U && memcmp(name, "v2", 2U) == 0);
    trl_result_free(result);
    g.read = 0U;
    g.length = 3U;
    TRL_CHECK(test, trl_read_native(read_back, &g, &result) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_result_failure(result)), "08006") == 0);
    trl_result_free(result);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-bulk"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-bulk", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_copy_is_all_or_nothing), TRL_TEST_CASE(copies_abandoned_and_iterated),
        TRL_TEST_CASE(a_copy_goes_by_column),    TRL_TEST_CASE(a_query_is_exported_as_csv),
        TRL_TEST_CASE(native_rows_read_back),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
