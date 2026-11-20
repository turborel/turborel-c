/**
 * @file bulk_test.c
 * @brief Copies and exports against a server of the test's own: a copy's
 * rows, its end and its abandonment, what may be sent while it is under
 * way, and its misuse; an export's rows as CSV and as native frames, read
 * back, and its failures; native input that is not native rows.
 */
#include "bulk_serving.h"
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "format.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Room for an export's bytes. */
#define EXPORT_BYTES 4096U

/* An export's bytes as written, and whether a write fails. */
typedef struct written_out {
    uint8_t bytes[EXPORT_BYTES];
    size_t length;
    bool fails;
    size_t read_at;
} written_out;

static bool write_out(void *context, const uint8_t *bytes, size_t length) {
    written_out *w = context;
    if (w->fails || w->length + length > sizeof w->bytes) {
        return false;
    }
    memcpy(w->bytes + w->length, bytes, length);
    w->length += length;
    return true;
}

/* Reads what was written back, two bytes at a time. */
static size_t read_back(void *context, uint8_t *into, size_t capacity) {
    written_out *w = context;
    size_t n = w->length - w->read_at;
    n = n < 2U ? n : 2U;
    n = n < capacity ? n : capacity;
    memcpy(into, w->bytes + w->read_at, n);
    w->read_at += n;
    return n;
}

/* A row of two texts. */
static trl_params *two_texts(void) {
    trl_params *row = NULL;
    (void)trl_params_new(&row);
    (void)trl_params_add_text(row, "a", 1U);
    (void)trl_params_add_text(row, "b", 1U);
    return row;
}

/* A source of rows: three rows, then the end, or a failure at the second. */
static trl_source_status three_rows(void *context, trl_params *row) {
    int *given = context;
    if (*given == 3 || *given < 0) {
        return *given < 0 ? TRL_SOURCE_FAILED : TRL_SOURCE_END;
    }
    ++*given;
    (void)trl_params_add_text(row, "a", 1U);
    (void)trl_params_add_text(row, "b", 1U);
    return TRL_SOURCE_ROW;
}

/**
 * Verifies: [PROTOCOL CopyIn 'I'] [PROTOCOL CopyData 'J'] [PROTOCOL
 * CopyEnd 'N'] a copy's rows sent a chunk at a time with no reply, its end
 * answered by the rows inserted; abandoned, none; while it is under way
 * only its rows, its end and Ping are sent, anything else refused
 * (SQLSTATE TB910) [CLIENT Copying and exporting].
 */
static void a_copy_sends_its_rows(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    script s;
    trl_conn *conn = bulk_connect(&s, &plan);
    static const char *const columns[] = {"A", "B"};
    trl_copy_target target = {"T", columns, 2U};
    trl_copy *copy = NULL;
    trl_params *row = two_texts();
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    for (int i = 0; i < 3; ++i) {
        TRL_CHECK(test, trl_copy_send(copy, row) == TRL_OK);
    }
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT 1", &result) == TRL_FAILED && result == NULL);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "TB910") == 0);
    uint64_t rows = 0U;
    TRL_CHECK(test, trl_copy_finish(copy, &rows) == TRL_OK && rows == 3U);
    TRL_CHECK(test, trl_copy_finish(copy, &rows) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_copy_send(copy, row) == TRL_INVALID_ARGUMENT && trl_copy_abandon(copy) == TRL_INVALID_ARGUMENT);
    trl_copy_free(copy);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK && trl_copy_send(copy, row) == TRL_OK);
    TRL_CHECK(test, trl_copy_abandon(copy) == TRL_OK);
    trl_copy_free(copy);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    trl_copy_free(copy);
    TRL_CHECK(test, plan.copies == 3U && trl_conn_ping(conn) == TRL_FAILED);
    trl_copy_target whole = {"T", NULL, 0U};
    int given = 0;
    TRL_CHECK(test, trl_copy_rows(conn, &whole, three_rows, &given, &rows) == TRL_OK && rows == 3U);
    given = -1;
    TRL_CHECK(test, trl_copy_rows(conn, &whole, three_rows, &given, &rows) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "TB910") == 0);
    plan.copy_fail = true;
    TRL_CHECK(test, trl_copy_in(conn, &whole, &copy) == TRL_OK && trl_copy_finish(copy, NULL) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "42000") == 0);
    trl_copy_free(copy);
    trl_params_free(row);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Copying and exporting] a copy needs `copy` granted,
 * refused before anything is sent, the connection kept; null arguments, a
 * column not named and a value the protocol cannot carry are refused.
 */
static void a_copy_is_refused(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    script s;
    trl_conn *conn = bulk_connect(&s, &plan);
    static const char *const columns[] = {"A", NULL};
    trl_copy_target target = {"T", columns, 1U};
    trl_copy *copy = NULL;
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_FAILED && copy == NULL);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0 && !trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
    plan.copy = true;
    conn = bulk_connect(&s, &plan);
    trl_copy_target unnamed = {"T", columns, 2U};
    TRL_CHECK(test, trl_copy_in(conn, &unnamed, &copy) == TRL_INVALID_ARGUMENT);
    trl_copy_target no_list = {"T", NULL, 1U};
    TRL_CHECK(test, trl_copy_in(conn, &no_list, &copy) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_copy_in(NULL, &target, &copy) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_copy_in(conn, &target, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test,
              trl_copy_send(NULL, NULL) == TRL_INVALID_ARGUMENT && trl_copy_finish(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_copy_abandon(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_copy_rows(conn, &target, NULL, NULL, NULL) == TRL_INVALID_ARGUMENT);
    trl_copy_target no_table = {NULL, NULL, 0U};
    TRL_CHECK(test, trl_copy_in(conn, &no_table, &copy) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_copy_in(conn, NULL, &copy) == TRL_INVALID_ARGUMENT);
    written_out out;
    memset(&out, 0, sizeof out);
    trl_export_sink sink = {TRL_EXPORT_CSV, write_out, &out};
    trl_export_sink no_writer = {TRL_EXPORT_CSV, NULL, &out};
    TRL_CHECK(test, trl_export(conn, NULL, &sink, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_export(conn, "Q", NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_export(conn, "Q", &no_writer, NULL) == TRL_INVALID_ARGUMENT);
    trl_copy_free(NULL);
    TRL_CHECK(test, trl_copy_in(conn, &target, &copy) == TRL_OK);
    trl_params *bad = NULL;
    (void)trl_params_new(&bad);
    trl_value text;
    memset(&text, 0, sizeof text);
    text.tag = TRL_TAG_VARYING;
    text.as.bytes.bytes = (const uint8_t *)"\xff";
    text.as.bytes.length = 1U;
    (void)trl_params_add_value(bad, &text);
    TRL_CHECK(test, trl_copy_send(copy, bad) == TRL_FAILED && !trl_conn_is_lost(conn));
    trl_params_free(bad);
    trl_copy_free(copy);
    trl_conn_free(conn);
    script_free(&s);
}

/* Exports "Q" over a server planned as `plan` to `out` as `format`. */
static trl_status exported(bulk_plan *plan, trl_export_format format, written_out *out, uint64_t *rows,
                           char *sqlstate) {
    script s;
    trl_conn *conn = bulk_connect(&s, plan);
    trl_export_sink sink = {format, write_out, out};
    trl_status status = trl_export(conn, "Q", &sink, rows);
    trl_format(sqlstate, 8U, "%s", trl_failure_sqlstate(trl_conn_failure(conn)));
    trl_conn_free(conn);
    script_free(&s);
    return status;
}

/**
 * Verifies: [CLIENT Copying and exporting] an export writes a query's
 * columns and rows as CSV, a header then a record a row, as they come;
 * as native frames, read back with every value as it was.
 */
static void an_export_writes_its_rows(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    written_out out;
    memset(&out, 0, sizeof out);
    uint64_t rows = 0U;
    char sqlstate[8];
    TRL_CHECK(test, exported(&plan, TRL_EXPORT_CSV, &out, &rows, sqlstate) == TRL_OK && rows == 3U);
    static const char csv[] = "C0,C1\r\nv,v\r\nv,v\r\nv,v\r\n";
    TRL_CHECK(test, out.length == sizeof csv - 1U && memcmp(out.bytes, csv, out.length) == 0);
    plan.query = EXPORT_QUOTED;
    memset(&out, 0, sizeof out);
    TRL_CHECK(test, exported(&plan, TRL_EXPORT_CSV, &out, &rows, sqlstate) == TRL_OK && rows == 5U);
    static const char quoted[] = "C0,C1\r\n\"a,b\",\"say \"\"hi\"\"\"\r\n\" x\",\"y\t\"\r\n\"l\nb\",\"c\rd\"\r\n"
                                 "\"\vz\",\"z\f\"\r\n\"\",\r\n";
    TRL_CHECK(test, out.length == sizeof quoted - 1U && memcmp(out.bytes, quoted, out.length) == 0);
    plan.query = EXPORT_ROWS;
    memset(&out, 0, sizeof out);
    TRL_CHECK(test, exported(&plan, TRL_EXPORT_NATIVE, &out, &rows, sqlstate) == TRL_OK && rows == 3U);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_read_native(read_back, &out, &result) == TRL_OK);
    TRL_CHECK(test, trl_result_column_count(result) == 2U && trl_result_row_count(result) == 3U);
    TRL_CHECK(test, trl_result_failure(result) == NULL);
    trl_result_free(result);
    TRL_CHECK(test, trl_export(NULL, "Q", NULL, &rows) == TRL_INVALID_ARGUMENT && rows == 0U);
    TRL_CHECK(test, trl_read_native(NULL, NULL, &result) == TRL_INVALID_ARGUMENT);
}

/**
 * Verifies: [CLIENT Copying and exporting] an export fails with the
 * query's Error, of a statement that is no query, of a write that fails
 * (SQLSTATE 08006, the connection kept), and of a reply out of turn.
 */
static void an_export_fails(trl_test *test) {
    static const struct {
        bulk_export query;
        bool fails;
        const char *sqlstate;
    } cases[] = {
        {EXPORT_ERROR, false, "42000"},       {EXPORT_ROWS_THEN_ERROR, false, "22003"},
        {EXPORT_OUT_OF_TURN, false, "08006"}, {EXPORT_ROWS_THEN_OUT_OF_TURN, false, "08006"},
        {EXPORT_NO_QUERY, false, "08006"},    {EXPORT_ROWS, true, "08006"},
    };
    for (size_t i = 0U; i < sizeof cases / sizeof cases[0]; ++i) {
        bulk_plan plan;
        memset(&plan, 0, sizeof plan);
        plan.query = cases[i].query;
        written_out out;
        memset(&out, 0, sizeof out);
        out.fails = cases[i].fails;
        uint64_t rows = 0U;
        char sqlstate[8];
        TRL_CHECK(test, exported(&plan, TRL_EXPORT_NATIVE, &out, &rows, sqlstate) == TRL_FAILED);
        TRL_CHECK(test, strcmp(sqlstate, cases[i].sqlstate) == 0);
    }
}

/* Whether `bytes` read as native rows fail, saying `why`. */
static bool not_native(const uint8_t *bytes, size_t length, const char *why) {
    written_out in;
    memset(&in, 0, sizeof in);
    memcpy(in.bytes, bytes, length);
    in.length = length;
    trl_result *result = NULL;
    bool failed = trl_read_native(read_back, &in, &result) == TRL_FAILED && trl_result_failure(result) != NULL &&
                  strstr(trl_failure_message(trl_result_failure(result)), why) != NULL;
    trl_result_free(result);
    return failed;
}

/**
 * Verifies: [CLIENT Copying and exporting] input that is not native rows
 * is refused, saying why: empty, a frame cut short or longer than the
 * protocol's longest, no Description first, a frame other than Rows before
 * the Complete, a frame that does not decode.
 */
static void native_input_is_checked(trl_test *test) {
    static const uint8_t pong[] = {TRL_KIND_PONG, 0U, 0U, 0U, 0U};
    static const uint8_t cut[] = {TRL_KIND_DESCRIPTION, 9U, 0U, 0U, 0U, 1U};
    static const uint8_t longest[] = {TRL_KIND_DESCRIPTION, 0xFFU, 0xFFU, 0xFFU, 0x7FU};
    static const uint8_t bad[] = {TRL_KIND_DESCRIPTION, 1U, 0U, 0U, 0U, 9U};
    static const uint8_t then_pong[] = {
        TRL_KIND_DESCRIPTION, 4U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, TRL_KIND_PONG, 0U, 0U, 0U, 0U,
    };
    static const uint8_t no_end[] = {TRL_KIND_DESCRIPTION, 4U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    TRL_CHECK(test, not_native(pong, 0U, "the input ends before a frame"));
    TRL_CHECK(test, not_native(pong, sizeof pong, "no Description first"));
    TRL_CHECK(test, not_native(cut, sizeof cut, "the input ends within a frame"));
    TRL_CHECK(test, not_native(longest, sizeof longest, "longer than the protocol's longest"));
    TRL_CHECK(test, not_native(bad, sizeof bad, ""));
    TRL_CHECK(test, not_native(then_pong, sizeof then_pong, "a frame other than Rows before the Complete"));
    TRL_CHECK(test, not_native(no_end, sizeof no_end, "the input ends before a frame"));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_copy_sends_its_rows),     TRL_TEST_CASE(a_copy_is_refused),
        TRL_TEST_CASE(an_export_writes_its_rows), TRL_TEST_CASE(an_export_fails),
        TRL_TEST_CASE(native_input_is_checked),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
