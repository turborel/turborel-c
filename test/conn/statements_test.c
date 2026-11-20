/**
 * @file statements_test.c
 * @brief Statements against a server of the test's own, at their edges
 * [PROTOCOL Conversations]: replies out of turn, rows broken off, methods
 * of capabilities not granted, completions' tails and notices kept.
 */
#include "codec/buffer.h"
#include "harness.h"
#include "script.h"
#include "serving.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Verifies: [PROTOCOL Conversations] a query's Description, its Rows in
 * batches, and its Complete; a reply out of turn, or rows broken off, lose
 * the connection; an Error is the statement's failure
 * [PROTOCOL Query 'Q'].
 */
static void a_query_is_read_or_refused(trl_test *test) {
    serving plan = {PLAN_ROWS, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_OK && trl_result_row_count(result) == 3U);
    TRL_CHECK(test,
              strcmp(trl_result_column_name(result, 1U), "C1") == 0 && trl_result_column_name(result, 2U) == NULL);
    TRL_CHECK(test, !trl_result_column_not_null(result, 0U) && trl_result_column_not_null(result, 1U) &&
                        !trl_result_column_not_null(result, 2U) && !trl_result_column_not_null(NULL, 0U));
    TRL_CHECK(test, trl_result_value(result, 3U, 0U) == NULL && trl_result_value(result, 0U, 2U) == NULL);
    TRL_CHECK(test, trl_result_column_type(result, 2U) == NULL && trl_conn_in_transaction(conn));
    trl_result_free(result);
    plan.plan = PLAN_ERROR;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_FAILED && serving_failed_with(conn, "a statement refused"));
    TRL_CHECK(test, !trl_conn_is_lost(conn));
    plan.plan = PLAN_BROKEN_ROWS;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_FAILED &&
                        serving_failed_with(conn, "the server broke off its rows"));
    trl_conn_free(conn);
    script_free(&s);
    plan.plan = PLAN_PONG;
    conn = serving_connect(&s, &plan);
    TRL_CHECK(test,
              trl_query(conn, "Q", &result) == TRL_FAILED && serving_failed_with(conn, "the server sent no result"));
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT What the server tells] a completion's tails: the
 * commit's position, the rows, the identity values, the conditions, which
 * a program may drop once it took them, and the server's time; a
 * warning's SQLSTATE [PROTOCOL Complete 'K'].
 */
static void a_completion_tells_its_tails(trl_test *test) {
    serving plan = {PLAN_TAILS, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_OK && strcmp(trl_result_sqlstate(result), "01003") == 0);
    trl_result_free(result);
    uint64_t number = 0U;
    TRL_CHECK(test, trl_conn_commit_position(conn, &number) && number == 42U);
    TRL_CHECK(test, trl_conn_rows_affected(conn, &number) && number == 2U);
    TRL_CHECK(test, trl_conn_generated_count(conn) == 2U && strcmp(trl_conn_generated_at(conn, 1U), "8") == 0);
    TRL_CHECK(test, trl_conn_condition_count(conn) == 1U);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_condition_at(conn, 0U)), "null eliminated") == 0);
    trl_conn_clear_conditions(conn);
    TRL_CHECK(test, trl_conn_condition_count(conn) == 0U && trl_conn_condition_at(conn, 0U) == NULL);
    TRL_CHECK(test, trl_conn_generated_count(conn) == 2U);
    trl_conn_clear_conditions(NULL);
    trl_timing timing;
    TRL_CHECK(test, trl_conn_server_time(conn, &timing) && timing.total == 30U && timing.locks == 1U);
    TRL_CHECK(test, strcmp(trl_conn_warning(conn), "01003") == 0);
    plan.plan = PLAN_DONE;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_OK && trl_conn_warning(conn) == NULL);
    trl_result_free(result);
    TRL_CHECK(test, !trl_conn_rows_affected(conn, NULL) && !trl_conn_server_time(conn, NULL));
    TRL_CHECK(test, trl_conn_commit_position(conn, NULL));
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, !trl_conn_rows_affected(NULL, NULL) && trl_conn_generated_count(NULL) == 0U);
    TRL_CHECK(test, trl_conn_generated_at(NULL, 0U) == NULL && !trl_conn_server_time(NULL, NULL));
    TRL_CHECK(test, !trl_conn_commit_position(NULL, NULL) && trl_conn_condition_count(NULL) == 0U);
    TRL_CHECK(test, trl_conn_condition_at(NULL, 0U) == NULL && trl_conn_warning(NULL) == NULL);
}

/**
 * Verifies: [CLIENT What the server tells] with no handler the last 64
 * notices are kept, the oldest dropped first, and cleared once taken.
 */
static void the_last_notices_are_kept(trl_test *test) {
    serving plan = {PLAN_NOTICES, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_OK && trl_conn_notice_count(conn) == 64U);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_notice_at(conn, 63U)), "01999") == 0);
    TRL_CHECK(test, trl_conn_notice_at(conn, 64U) == NULL);
    trl_result_free(result);
    trl_conn_clear_notices(conn);
    TRL_CHECK(test, trl_conn_notice_count(conn) == 0U);
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, trl_conn_notice_count(NULL) == 0U && trl_conn_notice_at(NULL, 0U) == NULL);
    TRL_CHECK(test, trl_conn_set_notice_handler(NULL, NULL, NULL) == TRL_INVALID_ARGUMENT);
    trl_conn_clear_notices(NULL);
}

/**
 * Verifies: [CLIENT Controlling the session] several statements as one
 * request: each outcome to the first failure, which stops the rest; an
 * Error of class 08 loses the connection and gives no outcome.
 */
static void several_statements_stop_at_a_failure(trl_test *test) {
    serving plan = {PLAN_MANY, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_ran *ran = NULL;
    TRL_CHECK(test, trl_query_many(conn, "A; B; C", &ran) == TRL_OK && trl_ran_count(ran) == 2U);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_ran_failure(ran)), "the third refused") == 0);
    TRL_CHECK(test, trl_result_row_count(trl_ran_result(ran, 1U)) == 1U);
    trl_ran_free(ran);
    plan.plan = PLAN_ERROR_08;
    TRL_CHECK(test, trl_query_many(conn, "A", &ran) == TRL_FAILED && ran == NULL && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, trl_ran_count(NULL) == 0U && trl_ran_result(NULL, 0U) == NULL && trl_ran_failure(NULL) == NULL);
    trl_ran_free(NULL);
}

/**
 * Verifies: [CLIENT What a connection asks for] a method needing a
 * capability not granted fails before it sends anything: describe,
 * numbered cursors, reset, change-user, multi-statement, query attributes.
 */
static void methods_need_their_capabilities(trl_test *test) {
    serving plan = {PLAN_DONE, false};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    size_t written = s.from_client.length;
    trl_statement *statement = NULL;
    trl_result *result = NULL;
    trl_ran *ran = NULL;
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_FAILED && serving_failed_with(conn, "the server did not grant reset"));
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", trust) == TRL_FAILED);
    TRL_CHECK(test, trl_query_many(conn, "A", &ran) == TRL_FAILED);
    static const char *const names[] = {"k"};
    static const char *const values[] = {"v"};
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, names, values, 1U) == TRL_FAILED);
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, NULL, NULL, 0U) == TRL_OK);
    TRL_CHECK(test, s.from_client.length == written && !trl_conn_is_lost(conn));
    plan.plan = PLAN_PREPARED;
    TRL_CHECK(test, trl_prepare(conn, "S", &statement) == TRL_OK);
    written = s.from_client.length;
    trl_cursor numbered = {statement, 1U};
    TRL_CHECK(test, trl_cursor_open(conn, numbered, NULL, 1U, &result) == TRL_FAILED);
    TRL_CHECK(test, trl_cursor_fetch(conn, numbered, 1U, &result) == TRL_FAILED);
    TRL_CHECK(test, trl_cursor_close(conn, numbered) == TRL_FAILED);
    TRL_CHECK(test, trl_describe_cursor(conn, numbered, &result) == TRL_FAILED);
    trl_statement *again = NULL;
    TRL_CHECK(test, trl_describe(conn, statement, &again) == TRL_FAILED && again == NULL);
    TRL_CHECK(test, s.from_client.length == written);
    trl_mechanism_free(trust);
    trl_statement_free(statement);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL Execute 'E'] a statement's Execute read to Rows and
 * Suspended or to its completion; a reply out of turn loses the connection
 * [PROTOCOL Prepare 'P'] [PROTOCOL Describe 'Y'].
 */
static void prepared_statements_at_their_edges(trl_test *test) {
    serving plan = {PLAN_PREPARED, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "S", &statement) == TRL_OK);
    TRL_CHECK(test, trl_statement_parameter_count(statement) == 1U && trl_statement_column_count(statement) == 1U);
    TRL_CHECK(test, trl_statement_parameter_type(statement, 1U) == NULL);
    TRL_CHECK(test, trl_statement_column_name(statement, 1U) == NULL);
    trl_cursor cursor = {statement, 0U};
    trl_result *result = NULL;
    plan.plan = PLAN_SUSPEND;
    TRL_CHECK(test, trl_cursor_open(conn, cursor, NULL, 1U, &result) == TRL_OK && trl_result_more(result));
    TRL_CHECK(test, trl_result_sqlcode(result) == 0 && strcmp(trl_result_sqlstate(result), "") == 0);
    trl_result_free(result);
    plan.plan = PLAN_DESCRIBED;
    TRL_CHECK(test, trl_describe_cursor(conn, cursor, &result) == TRL_OK && trl_result_column_count(result) == 1U);
    trl_result_free(result);
    plan.plan = PLAN_ERROR;
    TRL_CHECK(test, trl_describe_cursor(conn, cursor, &result) == TRL_FAILED);
    trl_statement *again = NULL;
    TRL_CHECK(test, trl_describe(conn, statement, &again) == TRL_FAILED);
    TRL_CHECK(test, trl_prepare(conn, "S", &again) == TRL_FAILED);
    TRL_CHECK(test, trl_deallocate(conn, statement) == TRL_FAILED && trl_cursor_close(conn, cursor) == TRL_FAILED);
    plan.plan = PLAN_PONG;
    TRL_CHECK(test, trl_describe_cursor(conn, cursor, &result) == TRL_FAILED);
    TRL_CHECK(test, serving_failed_with(conn, "the server sent no Description") && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
    plan.plan = PLAN_PREPARED;
    conn = serving_connect(&s, &plan);
    trl_statement *bare = NULL;
    plan.plan = PLAN_PONG;
    TRL_CHECK(test,
              trl_prepare(conn, "S", &bare) == TRL_FAILED && serving_failed_with(conn, "the server sent no Prepared"));
    trl_conn_free(conn);
    script_free(&s);
    trl_statement_free(statement);
}

/* Counts the batches given it. */
static void count_batch(void *context, const trl_result *batch) {
    size_t *count = context;
    *count += trl_result_row_count(batch);
}

/**
 * Verifies: [PROTOCOL Execute 'E'] a statement of no columns read to its
 * completion, an Error its failure, a reply out of turn losing the
 * connection; rows given batch by batch stop at an Error or at a reply
 * out of turn [PROTOCOL Rows 'B'].
 */
static void completions_and_batches_at_their_edges(trl_test *test) {
    serving plan = {PLAN_PREPARED_BARE, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "S", &statement) == TRL_OK && trl_statement_column_count(statement) == 0U);
    trl_result *result = NULL;
    plan.plan = PLAN_DONE;
    TRL_CHECK(test, trl_execute(conn, statement, NULL, &result) == TRL_OK && !trl_result_has_rows(result));
    trl_result_free(result);
    plan.plan = PLAN_ERROR;
    TRL_CHECK(test, trl_execute(conn, statement, NULL, &result) == TRL_FAILED && !trl_conn_is_lost(conn));
    size_t rows = 0U;
    plan.plan = PLAN_ROWS_THEN_ERROR;
    TRL_CHECK(test, trl_query_batches(conn, "Q", count_batch, &rows, &result) == TRL_FAILED && rows == 1U);
    TRL_CHECK(test, serving_failed_with(conn, "out of range"));
    plan.plan = PLAN_ROWS;
    rows = 0U;
    TRL_CHECK(test, trl_query_batches(conn, "Q", count_batch, &rows, &result) == TRL_OK && rows == 3U);
    TRL_CHECK(test, trl_result_row_count(result) == 0U);
    trl_result_free(result);
    TRL_CHECK(test, trl_query_batches(conn, "Q", NULL, NULL, &result) == TRL_INVALID_ARGUMENT && result == NULL);
    plan.plan = PLAN_BROKEN_ROWS;
    TRL_CHECK(test, trl_query_batches(conn, "Q", count_batch, &rows, &result) == TRL_FAILED);
    TRL_CHECK(test, serving_failed_with(conn, "the server broke off its rows") && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
    plan.plan = PLAN_PREPARED_BARE;
    conn = serving_connect(&s, &plan);
    trl_statement *bare = NULL;
    TRL_CHECK(test, trl_prepare(conn, "S", &bare) == TRL_OK);
    plan.plan = PLAN_PONG;
    TRL_CHECK(test, trl_execute(conn, bare, NULL, &result) == TRL_FAILED);
    TRL_CHECK(test, serving_failed_with(conn, "the server sent no completion"));
    trl_statement_free(bare);
    trl_statement_free(statement);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [DESIGN] D9 a null pointer passed in is refused with a status,
 * and an accessor takes null.
 */
static void null_arguments_are_refused(trl_test *test) {
    trl_result *result = NULL;
    trl_statement *statement = NULL;
    trl_ran *ran = NULL;
    trl_cursor none = {NULL, 0U};
    TRL_CHECK(test, trl_query(NULL, "Q", &result) == TRL_INVALID_ARGUMENT &&
                        trl_query(NULL, "Q", NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_query_batches(NULL, "Q", NULL, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_prepare(NULL, "S", &statement) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_prepare(NULL, "S", NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_execute(NULL, NULL, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_cursor_open(NULL, none, NULL, 1U, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_cursor_fetch(NULL, none, 1U, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_cursor_close(NULL, none) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_describe_cursor(NULL, none, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_describe(NULL, NULL, &statement) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_deallocate(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_query_many(NULL, "Q", &ran) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_query_many(NULL, "Q", NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_reset(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_change_user(NULL, "u", NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_query_attributes(NULL, NULL, NULL, 0U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_autocommit(NULL, true) == TRL_INVALID_ARGUMENT && !trl_conn_autocommit(NULL));
    TRL_CHECK(test, !trl_conn_in_transaction(NULL));
    TRL_CHECK(test, !trl_result_has_rows(NULL) && trl_result_column_count(NULL) == 0U);
    TRL_CHECK(test, trl_result_column_name(NULL, 0U) == NULL && trl_result_column_type(NULL, 0U) == NULL);
    TRL_CHECK(test, trl_result_row_count(NULL) == 0U && trl_result_value(NULL, 0U, 0U) == NULL);
    TRL_CHECK(test,
              !trl_result_more(NULL) && trl_result_sqlcode(NULL) == 0 && strcmp(trl_result_sqlstate(NULL), "") == 0);
    TRL_CHECK(test, trl_statement_parameter_count(NULL) == 0U && trl_statement_parameter_type(NULL, 0U) == NULL);
    TRL_CHECK(test, trl_statement_column_count(NULL) == 0U && trl_statement_column_name(NULL, 0U) == NULL);
    TRL_CHECK(test, trl_statement_column_type(NULL, 0U) == NULL);
    trl_result_free(NULL);
    trl_statement_free(NULL);
}

/**
 * Verifies: [PROTOCOL Declared types] each column's declared type, of a
 * Prepared and of a Description, its name and as SQL writes it; a column
 * not told it has no name, and is written as its tag writes it, so an
 * exact numeric type is NUMERIC(p,s) [CLIENT Running statements].
 */
static void declared_types_are_given(trl_test *test) {
    serving plan = {PLAN_DECLARED, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "S", &statement) == TRL_OK && trl_statement_column_count(statement) == 2U);
    TRL_CHECK(test, strcmp(trl_statement_column_type_name(statement, 0U), "INTEGER") == 0);
    TRL_CHECK(test, strcmp(trl_statement_column_type_written(statement, 1U), "S.MONEY") == 0);
    TRL_CHECK(test, trl_statement_column_type_name(statement, 2U) == NULL);
    TRL_CHECK(test, trl_statement_column_type_written(NULL, 0U) == NULL);
    trl_statement_free(statement);
    plan.plan = PLAN_DECLARED_ROWS;
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT", &result) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_result_column_type_name(result, 1U), "USER-DEFINED") == 0);
    TRL_CHECK(test, strcmp(trl_result_column_type_written(result, 0U), "INTEGER") == 0);
    TRL_CHECK(test,
              trl_result_column_type_written(result, 2U) == NULL && trl_result_column_type_name(NULL, 0U) == NULL);
    trl_result_free(result);
    plan.plan = PLAN_PREPARED;
    TRL_CHECK(test, trl_prepare(conn, "S", &statement) == TRL_OK);
    TRL_CHECK(test, trl_statement_column_type_name(statement, 0U) == NULL);
    TRL_CHECK(test, strcmp(trl_statement_column_type_written(statement, 0U), "CHARACTER VARYING(20)") == 0);
    trl_statement_free(statement);
    trl_conn_free(conn);
    script_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_query_is_read_or_refused),      TRL_TEST_CASE(a_completion_tells_its_tails),
        TRL_TEST_CASE(the_last_notices_are_kept),       TRL_TEST_CASE(several_statements_stop_at_a_failure),
        TRL_TEST_CASE(methods_need_their_capabilities), TRL_TEST_CASE(prepared_statements_at_their_edges),
        TRL_TEST_CASE(null_arguments_are_refused),      TRL_TEST_CASE(completions_and_batches_at_their_edges),
        TRL_TEST_CASE(declared_types_are_given),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
