/**
 * @file statements_test.c
 * @brief Statements against a real server (RULES.md R-TEST-8): queries,
 * prepared statements and cursors, completions, failures and notices,
 * autocommit, and the session's control. Opt-in: TRL_TEST_SERVER names the
 * server, else the test is skipped.
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
#define TEXT_BYTES 128U
/* A query of more rows than one batch of the server's holds. */
#define MANY_ROWS 20000U
#define MANY_QUERY                                                                                                     \
    "WITH RECURSIVE R(N) AS (SELECT CAST(1 AS BIGINT) FROM (VALUES (1)) AS V(X) UNION ALL SELECT N + 1 FROM R "        \
    "WHERE N < 20000) SELECT N, 'row ' || CAST(N AS VARCHAR(10)) FROM R"

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

/* Whether a value is the exact number of `text`. */
static bool exact_is(const trl_value *value, const char *text) {
    trl_decimal number;
    char written[TRL_DECIMAL_TEXT_BYTES];
    return trl_value_decimal(value, &number) == TRL_OK &&
           trl_decimal_text(&number, written, sizeof written) == TRL_OK && strcmp(written, text) == 0;
}

/* Whether a value is the text `text`. */
static bool text_is(const trl_value *value, const char *text) {
    size_t length = 0U;
    const char *bytes = trl_value_text(value, &length);
    return bytes != NULL && length == strlen(text) && memcmp(bytes, text, length) == 0;
}

/**
 * Verifies: [CLIENT Running statements] a query gives its columns and
 * rows, of every kind of value; one of no row completes with SQLCODE 100,
 * SQLSTATE 02000; another statement gives its completion
 * [PROTOCOL Query 'Q'].
 */
static void a_query_gives_columns_and_rows(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_result *result = NULL;
    TRL_CHECK(test,
              conn != NULL && trl_query(conn,
                                        "SELECT CAST(1.50 AS DECIMAL(5,2)) AS PRICE, CAST(2.5 AS DOUBLE PRECISION), "
                                        "'Ada', X'0102', DATE '2026-11-20', TRUE, CAST(NULL AS INTEGER), "
                                        "INTERVAL '2' DAY, CAST(1E1 AS DECFLOAT) FROM (VALUES (1)) AS V(X)",
                                        &result) == TRL_OK);
    TRL_CHECK(test, trl_result_has_rows(result) && trl_result_column_count(result) == 9U);
    TRL_CHECK(test, strcmp(trl_result_column_name(result, 0U), "PRICE") == 0);
    TRL_CHECK(test, trl_type_kind(trl_result_column_type(result, 0U)) == TRL_KIND_EXACT);
    TRL_CHECK(test, trl_type_precision(trl_result_column_type(result, 0U)) == 5U);
    TRL_CHECK(test, trl_type_scale(trl_result_column_type(result, 0U)) == 2U);
    TRL_CHECK(test, trl_result_row_count(result) == 1U && exact_is(trl_result_value(result, 0U, 0U), "1.50"));
    double approximate = 0.0;
    TRL_CHECK(test, trl_value_double(trl_result_value(result, 0U, 1U), &approximate) == TRL_OK);
    TRL_CHECK(test, approximate > 2.4 && approximate < 2.6);
    TRL_CHECK(test, text_is(trl_result_value(result, 0U, 2U), "Ada"));
    size_t length = 0U;
    const uint8_t *bytes = trl_value_bytes(trl_result_value(result, 0U, 3U), &length);
    TRL_CHECK(test, bytes != NULL && length == 2U && bytes[1] == 2U);
    trl_datetime date;
    TRL_CHECK(test, trl_value_datetime(trl_result_value(result, 0U, 4U), &date) == TRL_OK);
    TRL_CHECK(test, trl_value_kind(trl_result_value(result, 0U, 4U)) == TRL_KIND_DATE);
    bool truth = false;
    TRL_CHECK(test, trl_value_boolean(trl_result_value(result, 0U, 5U), &truth) == TRL_OK && truth);
    TRL_CHECK(test, trl_value_is_null(trl_result_value(result, 0U, 6U)));
    trl_interval interval;
    TRL_CHECK(test, trl_value_interval(trl_result_value(result, 0U, 7U), &interval) == TRL_OK);
    trl_decfloat decfloat;
    TRL_CHECK(test, trl_value_decfloat(trl_result_value(result, 0U, 8U), &decfloat) == TRL_OK);
    TRL_CHECK(test, trl_result_sqlcode(result) == 0 && strcmp(trl_result_sqlstate(result), "00000") == 0);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT X FROM (VALUES (1)) AS V(X) WHERE X > 1", &result) == TRL_OK);
    TRL_CHECK(test, trl_result_row_count(result) == 0U && trl_result_sqlcode(result) == 100);
    TRL_CHECK(test, strcmp(trl_result_sqlstate(result), "02000") == 0);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "CREATE TABLE QS (ID INTEGER)", &result) == TRL_OK);
    TRL_CHECK(test, !trl_result_has_rows(result) && trl_result_column_count(result) == 0U);
    trl_result_free(result);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Running statements] a failure gives its SQLCODE,
 * SQLSTATE, message, the rule it cites apart, and its position; with
 * `diagnostics` [CLIENT What the server tells], what it is about.
 */
static void a_failure_says_what_and_where(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT * FROM NOPE", &result) == TRL_FAILED && result == NULL);
    const trl_failure *failure = trl_conn_failure(conn);
    TRL_CHECK(test, trl_failure_sqlcode(failure) == -103 && strcmp(trl_failure_sqlstate(failure), "2A000") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(failure), "table ANN.NOPE is not defined") == 0);
    TRL_CHECK(test, trl_failure_rule(failure) != NULL);
    uint64_t position = 0U;
    TRL_CHECK(test, trl_failure_position(failure, &position) && position == 14U);
    TRL_CHECK(test, !trl_conn_is_lost(conn));
    TRL_CHECK(test, run(conn, "CREATE TABLE DK (ID INTEGER, CONSTRAINT DK_KEY PRIMARY KEY (ID))") &&
                        run(conn, "INSERT INTO DK VALUES (1)") && run(conn, "COMMIT"));
    TRL_CHECK(test, trl_query(conn, "INSERT INTO DK VALUES (1)", &result) == TRL_FAILED);
    failure = trl_conn_failure(conn);
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_SEVERITY), "ERROR") == 0);
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_CONSTRAINT), "DK_KEY") == 0);
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_TABLE), "DK") == 0);
    TRL_CHECK(test, trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_DETAIL) != NULL);
    trl_conn_free(conn);
}

/* Counts the rows of each batch it is given. */
static void count_batch(void *context, const trl_result *batch) {
    size_t *counted = context;
    counted[0] += trl_result_row_count(batch);
    counted[1] += 1U;
}

/**
 * Verifies: [CLIENT Running statements] a query's rows given a batch at a
 * time, as they come.
 */
static void rows_come_a_batch_at_a_time(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    size_t counted[2] = {0U, 0U};
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query_batches(conn, MANY_QUERY, count_batch, counted, &result) == TRL_OK);
    TRL_CHECK(test, counted[0] == MANY_ROWS && counted[1] > 1U);
    TRL_CHECK(test, trl_result_column_count(result) == 2U && trl_result_row_count(result) == 0U);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, MANY_QUERY, &result) == TRL_OK && trl_result_row_count(result) == MANY_ROWS);
    TRL_CHECK(test, exact_is(trl_result_value(result, MANY_ROWS - 1U, 0U), "20000"));
    trl_result_free(result);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Running statements] a transaction begins with the
 * first statement and lasts until COMMIT or ROLLBACK; under autocommit
 * each statement is its own, by `ends`, no COMMIT sent, a failing commit's
 * Error in place of its Complete [PROTOCOL Query 'Q']
 * [PROTOCOL Conversations].
 */
static void a_transaction_ends_as_asked(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, !trl_conn_in_transaction(conn) && !trl_conn_autocommit(conn));
    TRL_CHECK(test, run(conn, "CREATE TABLE TX (ID INTEGER)") && run(conn, "COMMIT"));
    TRL_CHECK(test, run(conn, "INSERT INTO TX VALUES (1)") && trl_conn_in_transaction(conn));
    TRL_CHECK(test, run(conn, "ROLLBACK") && !trl_conn_in_transaction(conn));
    TRL_CHECK(test, trl_conn_set_autocommit(conn, true) == TRL_OK && trl_conn_autocommit(conn));
    TRL_CHECK(test, run(conn, "INSERT INTO TX VALUES (2)") && !trl_conn_in_transaction(conn));
    TRL_CHECK(test, run(conn, "CREATE TABLE TD (ID INTEGER, CONSTRAINT TD_POS CHECK (ID > 0) INITIALLY DEFERRED)"));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "INSERT INTO TD VALUES (-1)", &result) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "40002") == 0);
    TRL_CHECK(test, !trl_conn_in_transaction(conn));
    TRL_CHECK(test, trl_conn_set_autocommit(conn, false) == TRL_OK);
    TRL_CHECK(test, trl_query(conn, "SELECT COUNT(*) FROM TX", &result) == TRL_OK);
    TRL_CHECK(test, exact_is(trl_result_value(result, 0U, 0U), "1"));
    trl_result_free(result);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Prepared statements] a statement prepared with `?`,
 * the type each parameter takes and its columns, executed with values as
 * many times as wanted, across transactions, then deallocated
 * [PROTOCOL Prepare 'P'] [PROTOCOL Deallocate 'D'].
 */
static void a_prepared_statement_runs_again(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE PS (ID INTEGER, NAME VARCHAR(20))") && run(conn, "COMMIT"));
    trl_statement *insert = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO PS VALUES (?, ?)", &insert) == TRL_OK);
    TRL_CHECK(test, trl_statement_parameter_count(insert) == 2U && trl_statement_column_count(insert) == 0U);
    TRL_CHECK(test, trl_type_kind(trl_statement_parameter_type(insert, 1U)) == TRL_KIND_CHARACTER_VARYING);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    static const char *const names[] = {"Ada", "Grace", "Edsger"};
    for (int i = 0; i < 3; ++i) {
        trl_decimal id = {{(uint64_t)i + 1U, 0}, 0U};
        trl_params_clear(params);
        (void)trl_params_add_decimal(params, id);
        (void)trl_params_add_text(params, names[i], strlen(names[i]));
        trl_result *result = NULL;
        TRL_CHECK(test, trl_execute(conn, insert, params, &result) == TRL_OK && !trl_result_has_rows(result));
        trl_result_free(result);
        TRL_CHECK(test, run(conn, "COMMIT"));
    }
    trl_statement *select = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT NAME FROM PS WHERE ID > ? ORDER BY ID", &select) == TRL_OK);
    TRL_CHECK(test, trl_statement_column_count(select) == 1U);
    TRL_CHECK(test, strcmp(trl_statement_column_name(select, 0U), "NAME") == 0);
    TRL_CHECK(test, trl_type_kind(trl_statement_column_type(select, 0U)) == TRL_KIND_CHARACTER_VARYING);
    trl_params_clear(params);
    trl_decimal one = {{1U, 0}, 0U};
    (void)trl_params_add_decimal(params, one);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, select, params, &result) == TRL_OK && trl_result_row_count(result) == 2U);
    TRL_CHECK(test, text_is(trl_result_value(result, 1U, 0U), "Edsger"));
    trl_result_free(result);
    TRL_CHECK(test, trl_deallocate(conn, insert) == TRL_OK);
    TRL_CHECK(test, trl_deallocate(conn, insert) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "26000") == 0);
    trl_statement_free(insert);
    trl_statement_free(select);
    trl_params_free(params);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Prepared statements] a cursor opened takes its first
 * rows, fetches the next batch, Suspended while it has more, and is closed;
 * one not open fails (SQLSTATE 24000) [PROTOCOL Execute 'E']
 * [PROTOCOL Fetch 'F'] [PROTOCOL Close 'C'].
 */
static void a_cursor_is_read_in_parts(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, MANY_QUERY, &statement) == TRL_OK);
    trl_cursor cursor = {statement, 0U};
    trl_result *batch = NULL;
    TRL_CHECK(test, trl_cursor_open(conn, cursor, NULL, 10U, &batch) == TRL_OK);
    TRL_CHECK(test, trl_result_row_count(batch) == 10U && trl_result_more(batch) && trl_conn_in_transaction(conn));
    TRL_CHECK(test, trl_result_column_count(batch) == 2U);
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_fetch(conn, cursor, 5U, &batch) == TRL_OK && trl_result_row_count(batch) == 5U);
    TRL_CHECK(test, exact_is(trl_result_value(batch, 0U, 0U), "11"));
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_close(conn, cursor) == TRL_OK);
    TRL_CHECK(test, trl_cursor_fetch(conn, cursor, 5U, &batch) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "24000") == 0);
    TRL_CHECK(test, trl_cursor_close(conn, cursor) == TRL_FAILED);
    trl_statement_free(statement);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Controlling the session] several numbered cursors of
 * one statement open at once, one already open refused (SQLSTATE 24000);
 * a prepared statement and an open cursor described again
 * [PROTOCOL Describe 'Y'].
 */
static void numbered_cursors_and_descriptions(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, MANY_QUERY, &statement) == TRL_OK);
    trl_cursor first = {statement, 1U};
    trl_cursor second = {statement, 2U};
    trl_result *batch = NULL;
    TRL_CHECK(test, trl_cursor_open(conn, first, NULL, 3U, &batch) == TRL_OK);
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_open(conn, second, NULL, 4U, &batch) == TRL_OK);
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_fetch(conn, first, 1U, &batch) == TRL_OK);
    TRL_CHECK(test, exact_is(trl_result_value(batch, 0U, 0U), "4"));
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_open(conn, first, NULL, 3U, &batch) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "24000") == 0);
    trl_result *columns = NULL;
    TRL_CHECK(test, trl_describe_cursor(conn, second, &columns) == TRL_OK);
    TRL_CHECK(test, trl_result_column_count(columns) == 2U && trl_result_row_count(columns) == 0U);
    trl_result_free(columns);
    trl_statement *again = NULL;
    TRL_CHECK(test, trl_describe(conn, statement, &again) == TRL_OK && trl_statement_column_count(again) == 2U);
    trl_statement_free(again);
    TRL_CHECK(test, trl_cursor_close(conn, first) == TRL_OK && trl_cursor_close(conn, second) == TRL_OK);
    trl_statement_free(statement);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL Prepare 'P'] each statement prepared takes a number
 * of its own, so none whose cursor is open is replaced; a statement whose
 * cursor is open is not deallocated (SQLSTATE 24000), and is once it is
 * closed [PROTOCOL Deallocate 'D'].
 */
static void an_open_cursors_statement_stays(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, MANY_QUERY, &statement) == TRL_OK);
    trl_cursor cursor = {statement, 0U};
    trl_result *batch = NULL;
    TRL_CHECK(test, trl_cursor_open(conn, cursor, NULL, 1U, &batch) == TRL_OK);
    trl_result_free(batch);
    trl_statement *other = NULL;
    TRL_CHECK(test, trl_prepare(conn, "VALUES (1)", &other) == TRL_OK);
    TRL_CHECK(test, trl_cursor_fetch(conn, cursor, 1U, &batch) == TRL_OK && trl_result_column_count(batch) == 2U);
    TRL_CHECK(test, exact_is(trl_result_value(batch, 0U, 0U), "2"));
    trl_result_free(batch);
    TRL_CHECK(test, trl_deallocate(conn, statement) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "24000") == 0 && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_cursor_close(conn, cursor) == TRL_OK && trl_deallocate(conn, statement) == TRL_OK);
    trl_statement_free(other);
    trl_statement_free(statement);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL Execute 'E'] a CALL's columns are its OUT and INOUT
 * parameters', and its Execute sends one row of their values before its
 * Complete [PROTOCOL Prepared 'P'].
 */
static void a_call_gives_its_output_parameters(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE PROCEDURE DOUBLED (IN A INTEGER, OUT B INTEGER, INOUT C INTEGER) "
                              "MODIFIES SQL DATA BEGIN SET B = A * 2; SET C = C + A; END") &&
                        run(conn, "COMMIT"));
    trl_statement *call = NULL;
    TRL_CHECK(test, trl_prepare(conn, "CALL DOUBLED(?, ?, ?)", &call) == TRL_OK);
    TRL_CHECK(test, trl_statement_parameter_count(call) == 2U && trl_statement_column_count(call) == 2U);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    trl_decimal five = {{5U, 0}, 0U};
    trl_decimal seven = {{7U, 0}, 0U};
    (void)trl_params_add_decimal(params, five);
    (void)trl_params_add_decimal(params, seven);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, call, params, &result) == TRL_OK && trl_result_row_count(result) == 1U);
    TRL_CHECK(test, exact_is(trl_result_value(result, 0U, 0U), "10"));
    TRL_CHECK(test, exact_is(trl_result_value(result, 0U, 1U), "12"));
    trl_result_free(result);
    trl_params_free(params);
    trl_statement_free(call);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL Execute 'E'] a string `?` longer than its column is
 * cut to the column's length, its completion warning 01004.
 */
static void a_cut_parameter_warns(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE CUT (V VARCHAR(5))"));
    trl_statement *insert = NULL;
    trl_params *params = NULL;
    trl_result *result = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO CUT VALUES (?)", &insert) == TRL_OK);
    TRL_CHECK(test, trl_params_new(&params) == TRL_OK && trl_params_add_text(params, "abcdefgh", 8U) == TRL_OK);
    TRL_CHECK(test, trl_execute(conn, insert, params, &result) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_result_sqlstate(result), "01004") == 0);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT V FROM CUT", &result) == TRL_OK);
    TRL_CHECK(test, text_is(trl_result_value(result, 0U, 0U), "abcde"));
    trl_result_free(result);
    trl_params_free(params);
    trl_statement_free(insert);
    trl_conn_free(conn);
}

/* Whether column `column` of `result` was declared as `name`, written as `written`. */
static bool declared_as(const trl_result *result, size_t column, const char *name, const char *written) {
    const char *told = trl_result_column_type_name(result, column);
    const char *as = trl_result_column_type_written(result, column);
    bool same = told != NULL && as != NULL && strcmp(told, name) == 0 && strcmp(as, written) == 0;
    if (!same) {
        (void)fprintf(stderr, "column %zu: %s, %s\n", column, told == NULL ? "(none)" : told, as == NULL ? "" : as);
    }
    return same;
}

/**
 * Verifies: [PROTOCOL Declared types] granted `declared-types`, which a
 * connection asks for, each column's declared type as the page's table
 * gives it, its values read by their tags; not asked, none, the type
 * written as its tag writes it [CLIENT Prepared statements].
 */
static void declared_types_are_told(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, trl_conn_granted(conn, TRL_CAPABILITY_DECLARED_TYPES));
    TRL_CHECK(test, run(conn, "CREATE TYPE MONEY AS DECIMAL(9,2) FINAL"));
    TRL_CHECK(test, run(conn, "CREATE TABLE KINDS (A SMALLINT, B INTEGER, C BIGINT, D DECIMAL(9,2), E NUMERIC(5), "
                              "F REAL, G DOUBLE PRECISION, H FLOAT(20), I MONEY, J INTEGER ARRAY[3])"));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT * FROM KINDS", &result) == TRL_OK);
    TRL_CHECK(test, declared_as(result, 0U, "SMALLINT", "SMALLINT") && declared_as(result, 1U, "INTEGER", "INTEGER"));
    TRL_CHECK(test, declared_as(result, 2U, "BIGINT", "BIGINT") && declared_as(result, 3U, "DECIMAL", "DECIMAL(9,2)"));
    TRL_CHECK(test, declared_as(result, 4U, "NUMERIC", "NUMERIC(5,0)") && declared_as(result, 5U, "REAL", "REAL"));
    TRL_CHECK(test, declared_as(result, 6U, "DOUBLE PRECISION", "DOUBLE PRECISION"));
    TRL_CHECK(test,
              declared_as(result, 7U, "FLOAT", "FLOAT(24)") && declared_as(result, 8U, "USER-DEFINED", "ANN.MONEY"));
    TRL_CHECK(test, declared_as(result, 9U, "ARRAY", "INTEGER ARRAY[3]"));
    TRL_CHECK(test, trl_type_kind(trl_result_column_type(result, 1U)) == TRL_KIND_EXACT && run(conn, "COMMIT"));
    trl_result_free(result);
    trl_conn_free(conn);
    trl_conn_options *options = NULL;
    TRL_CHECK(test, trl_conn_options_new(&options) == TRL_OK && trl_conn_options_ask_none(options) == TRL_OK);
    conn = live_trusted(options);
    TRL_CHECK(test, trl_query(conn, "SELECT * FROM KINDS", &result) == TRL_OK);
    TRL_CHECK(test, trl_result_column_type_name(result, 1U) == NULL);
    const char *written = trl_result_column_type_written(result, 1U);
    TRL_CHECK(test, written != NULL && strcmp(written, "NUMERIC(10,0)") == 0);
    trl_result_free(result);
    trl_conn_free(conn);
    trl_conn_options_free(options);
}

/**
 * Verifies: [PROTOCOL Composite fields] a prepared statement's columns
 * told NOT NULL as Prepared describes them, as a result's are: a key's
 * column and one declared NOT NULL, not a column that takes the null
 * value; none past the last [PROTOCOL Prepared 'P'].
 */
static void a_prepared_statement_tells_not_null(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE NN (K INTEGER PRIMARY KEY, M VARCHAR(5) NOT NULL, N VARCHAR(5))"));
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT K, M, N FROM NN WHERE K = ?", &statement) == TRL_OK);
    TRL_CHECK(test, trl_statement_column_not_null(statement, 0U) && trl_statement_column_not_null(statement, 1U));
    TRL_CHECK(test, !trl_statement_column_not_null(statement, 2U) && !trl_statement_column_not_null(statement, 3U));
    TRL_CHECK(test, !trl_statement_column_not_null(NULL, 0U));
    TRL_CHECK(test, trl_deallocate(conn, statement) == TRL_OK && run(conn, "ROLLBACK"));
    trl_statement_free(statement);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-statements"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-statements", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_query_gives_columns_and_rows),    TRL_TEST_CASE(a_failure_says_what_and_where),
        TRL_TEST_CASE(rows_come_a_batch_at_a_time),       TRL_TEST_CASE(a_transaction_ends_as_asked),
        TRL_TEST_CASE(a_prepared_statement_runs_again),   TRL_TEST_CASE(a_cursor_is_read_in_parts),
        TRL_TEST_CASE(numbered_cursors_and_descriptions), TRL_TEST_CASE(a_call_gives_its_output_parameters),
        TRL_TEST_CASE(an_open_cursors_statement_stays),   TRL_TEST_CASE(a_cut_parameter_warns),
        TRL_TEST_CASE(declared_types_are_told),           TRL_TEST_CASE(a_prepared_statement_tells_not_null),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
