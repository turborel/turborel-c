/**
 * @file called_test.c
 * @brief A CALL's parameter modes against a real server (RULES.md
 * R-TEST-8): with `parameter-modes` granted, its Prepared's tail 20 told
 * through the C API, each `?` with its mode, its procedure parameter's
 * position and type, the input or column it is, and the procedure's
 * name; without it, every `?` IN [PROTOCOL Parameter modes].
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

/* Whether `?` `marker` has `mode` and `position`, and an exact type. */
static bool marker_is(const trl_statement *call, size_t marker, trl_parameter_mode mode, uint32_t position) {
    return trl_statement_marker_mode(call, marker) == mode && trl_statement_marker_position(call, marker) == position &&
           trl_type_kind(trl_statement_marker_type(call, marker)) == TRL_KIND_EXACT;
}

/**
 * Verifies: [PROTOCOL Parameter modes] granted `parameter-modes`, which a
 * connection asks for, a CALL of a procedure of IN, OUT and INOUT
 * parameters, written with its `?`s apart from its parameters' order,
 * tells every `?`, the OUT one too: its mode, its parameter's position
 * and type; an input's place among the values executed, an OUT or INOUT
 * one's column of the CALL's row, that column giving its value back; the
 * procedure's schema and name.
 */
static void a_calls_markers_tell_their_modes(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, trl_conn_granted(conn, TRL_CAPABILITY_PARAMETER_MODES));
    TRL_CHECK(test, run(conn, "CREATE PROCEDURE MOVED (IN A INTEGER, OUT B INTEGER, INOUT C INTEGER) "
                              "MODIFIES SQL DATA BEGIN SET B = A * 2; SET C = C + A; END") &&
                        run(conn, "COMMIT"));
    trl_statement *call = NULL;
    TRL_CHECK(test, trl_prepare(conn, "CALL MOVED(?, ?, ?)", &call) == TRL_OK);
    TRL_CHECK(test, trl_statement_marker_count(call) == 3U && trl_statement_parameter_count(call) == 2U);
    TRL_CHECK(test, marker_is(call, 0U, TRL_PARAMETER_MODE_IN, 1U) && marker_is(call, 1U, TRL_PARAMETER_MODE_OUT, 2U) &&
                        marker_is(call, 2U, TRL_PARAMETER_MODE_INOUT, 3U));
    TRL_CHECK(test, trl_statement_marker_mode(call, 3U) == TRL_PARAMETER_MODE_NONE &&
                        trl_statement_marker_type(call, 3U) == NULL);
    size_t place = 9U;
    TRL_CHECK(test, trl_statement_marker_parameter(call, 0U, &place) && place == 0U &&
                        !trl_statement_marker_parameter(call, 1U, &place) &&
                        trl_statement_marker_parameter(call, 2U, &place) && place == 1U);
    size_t column = 9U;
    TRL_CHECK(test, !trl_statement_marker_column(call, 0U, &column) && trl_statement_marker_column(call, 1U, &column) &&
                        column == 0U && trl_statement_marker_column(call, 2U, &column) && column == 1U);
    TRL_CHECK(test,
              strcmp(trl_statement_procedure_schema(call), "ANN") == 0 && trl_statement_procedure_name(call) != NULL);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    trl_decimal five = {{5U, 0}, 0U};
    trl_decimal seven = {{7U, 0}, 0U};
    (void)trl_params_add_decimal(params, five);
    (void)trl_params_add_decimal(params, seven);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, call, params, &result) == TRL_OK && trl_result_row_count(result) == 1U);
    (void)trl_statement_marker_column(call, 1U, &column);
    TRL_CHECK(test, exact_is(trl_result_value(result, 0U, column), "10"));
    (void)trl_statement_marker_column(call, 2U, &column);
    TRL_CHECK(test, exact_is(trl_result_value(result, 0U, column), "12"));
    trl_result_free(result);
    trl_params_free(params);
    trl_statement_free(call);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL Parameter modes] a CALL whose `?`s are written out
 * of its parameters' order, an OUT one first: its columns still in the
 * procedure's order, so each `?`'s column is its parameter's place among
 * the OUT and INOUT ones; a statement other than a CALL, and a CALL of a
 * connection that asked for no capability, tell no tail 20: every
 * parameter a `?` of IN, of no position, of no procedure.
 */
static void markers_out_of_order_and_without_tail_20(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE PROCEDURE NAMED_OUT (OUT X INTEGER, IN Y INTEGER, OUT Z INTEGER) "
                              "MODIFIES SQL DATA BEGIN SET X = Y; SET Z = Y + 1; END") &&
                        run(conn, "COMMIT"));
    trl_statement *call = NULL;
    TRL_CHECK(test, trl_prepare(conn, "CALL NAMED_OUT(Z => ?, Y => ?, X => ?)", &call) == TRL_OK);
    size_t column = 9U;
    TRL_CHECK(test, marker_is(call, 0U, TRL_PARAMETER_MODE_OUT, 3U) && marker_is(call, 2U, TRL_PARAMETER_MODE_OUT, 1U));
    TRL_CHECK(test, trl_statement_marker_column(call, 0U, &column) && column == 1U &&
                        trl_statement_marker_column(call, 2U, &column) && column == 0U);
    trl_statement_free(call);
    trl_statement *plain = NULL;
    TRL_CHECK(test, trl_prepare(conn, "VALUES CAST(? AS INTEGER)", &plain) == TRL_OK);
    TRL_CHECK(test, trl_statement_marker_count(plain) == 1U && marker_is(plain, 0U, TRL_PARAMETER_MODE_IN, 0U) &&
                        trl_statement_procedure_name(plain) == NULL);
    trl_statement_free(plain);
    trl_conn_free(conn);
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_ask_none(options);
    conn = live_trusted(options);
    TRL_CHECK(test, !trl_conn_granted(conn, TRL_CAPABILITY_PARAMETER_MODES));
    TRL_CHECK(test, trl_prepare(conn, "CALL NAMED_OUT(?, ?, ?)", &call) == TRL_OK);
    TRL_CHECK(test, trl_statement_marker_count(call) == 1U && marker_is(call, 0U, TRL_PARAMETER_MODE_IN, 0U) &&
                        trl_statement_procedure_schema(call) == NULL);
    trl_statement_free(call);
    trl_conn_free(conn);
    trl_conn_options_free(options);
    TRL_CHECK(test, trl_statement_marker_count(NULL) == 0U && trl_statement_procedure_name(NULL) == NULL &&
                        !trl_statement_marker_column(NULL, 0U, NULL));
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-called"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-called", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_calls_markers_tell_their_modes),
        TRL_TEST_CASE(markers_out_of_order_and_without_tail_20),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
