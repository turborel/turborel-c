/**
 * @file pipeline_test.c
 * @brief Pipelines over scripted servers: each outcome in order, a
 * skipping batch settled, and a run that fails as its connection is lost
 * or its batch cannot be undone.
 */
#include "harness.h"
#include "script.h"
#include "serving.h"
#include "stmt/pipeline.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Requests enough to fill more than one window. */
#define MANY 1500U

/**
 * Verifies: [CLIENT Pipelines] every request queued has one outcome, in
 * order: a query's rows, an execution's, a failure its request's own;
 * nothing may be queued or run again once run; an outcome past the last
 * is none.
 */
static void each_request_has_its_outcome(trl_test *test) {
    static const reply_plan plans[] = {PLAN_PREPARED, PLAN_ROWS, PLAN_ERROR, PLAN_DONE};
    serving_sequence sequence = {{PLAN_DONE, true}, plans, 4U, 0U};
    script s;
    trl_conn *conn = serving_connect_sequence(&s, &sequence);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "VALUES ?", &statement) == TRL_OK);
    trl_pipeline *pipeline = NULL;
    TRL_CHECK(test, trl_pipeline_new(conn, &pipeline) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_query(pipeline, "VALUES 1") == TRL_OK);
    TRL_CHECK(test, trl_pipeline_query(pipeline, "VALUES 2") == TRL_OK);
    TRL_CHECK(test, trl_pipeline_execute(pipeline, statement, NULL) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_OK && trl_pipeline_count(pipeline) == 3U);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 0U) == TRL_PIPED_DONE);
    TRL_CHECK(test, trl_result_row_count(trl_pipeline_result(pipeline, 0U)) == 3U);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 1U) == TRL_PIPED_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_pipeline_failure(pipeline, 1U)), "a statement refused") == 0);
    TRL_CHECK(test, trl_pipeline_failure(pipeline, 0U) == NULL && trl_pipeline_result(pipeline, 1U) == NULL);
    TRL_CHECK(test,
              trl_pipeline_outcome(pipeline, 3U) == TRL_PIPED_SKIPPED && trl_pipeline_result(pipeline, 3U) == NULL);
    TRL_CHECK(test, trl_pipeline_failure(pipeline, 3U) == NULL);
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_query(pipeline, "VALUES 3") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_execute(pipeline, statement, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_release(pipeline, 0U) == TRL_INVALID_ARGUMENT);
    trl_pipeline_free(pipeline);
    trl_statement_free(statement);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Pipelines] a pipeline refuses what it cannot queue or
 * run: a null connection, pipeline, statement or text.
 */
static void a_pipeline_refuses_null_arguments(trl_test *test) {
    serving plan = {PLAN_DONE, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_pipeline *pipeline = NULL;
    TRL_CHECK(test, trl_pipeline_new(NULL, &pipeline) == TRL_INVALID_ARGUMENT && pipeline == NULL);
    TRL_CHECK(test, trl_pipeline_new(conn, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_new(conn, &pipeline) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_query(pipeline, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_query(NULL, "VALUES 1") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_execute(pipeline, NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_on_error(NULL, TRL_ON_ERROR_SKIP) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pipeline_run(NULL) == TRL_INVALID_ARGUMENT && trl_pipeline_count(NULL) == 0U);
    TRL_CHECK(test, trl_pipeline_outcome(NULL, 0U) == TRL_PIPED_SKIPPED && trl_pipeline_result(NULL, 0U) == NULL);
    TRL_CHECK(test, trl_pipeline_failure(NULL, 0U) == NULL);
    trl_pipeline_free(pipeline);
    trl_pipeline_free(NULL);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Pipelines] a skipping batch whose first request fails
 * skips the rest, no savepoint to undo to; one whose later request fails
 * is undone to the savepoint after it, the savepoints released; a window
 * after a failure is never sent.
 */
static void a_skipping_batch_is_settled(trl_test *test) {
    static const reply_plan first_fails[] = {PLAN_ERROR};
    serving_sequence sequence = {{PLAN_DONE, true}, first_fails, 1U, 0U};
    script s;
    trl_conn *conn = serving_connect_sequence(&s, &sequence);
    trl_pipeline *pipeline = NULL;
    (void)trl_pipeline_new(conn, &pipeline);
    (void)trl_pipeline_on_error(pipeline, TRL_ON_ERROR_SKIP);
    char sql[128];
    memset(sql, 0, sizeof sql);
    (void)memcpy(sql, "VALUES '", 8U);
    memset(sql + 8U, 'x', 100U);
    sql[108] = '\'';
    for (size_t i = 0U; i < MANY; ++i) {
        (void)trl_pipeline_query(pipeline, sql);
    }
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_OK && trl_pipeline_outcome(pipeline, 0U) == TRL_PIPED_FAILED);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 1U) == TRL_PIPED_SKIPPED);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, MANY - 1U) == TRL_PIPED_SKIPPED);
    size_t sent = sequence.answered;
    TRL_CHECK(test, sent > 2U && sent < (MANY * 2U) - 1U);
    trl_pipeline_free(pipeline);
    static const reply_plan second_fails[] = {PLAN_DONE, PLAN_DONE, PLAN_ERROR};
    sequence.first = second_fails;
    sequence.first_count = 3U;
    sequence.answered = 0U;
    (void)trl_pipeline_new(conn, &pipeline);
    (void)trl_pipeline_on_error(pipeline, TRL_ON_ERROR_SKIP);
    (void)trl_pipeline_query(pipeline, "VALUES 1");
    (void)trl_pipeline_query(pipeline, "VALUES 2");
    (void)trl_pipeline_query(pipeline, "VALUES 3");
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_OK && trl_pipeline_outcome(pipeline, 1U) == TRL_PIPED_FAILED);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 0U) == TRL_PIPED_DONE);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 2U) == TRL_PIPED_SKIPPED);
    TRL_CHECK(test, sequence.answered == 7U);
    trl_pipeline_free(pipeline);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Pipelines] run fails as the connection is lost: a
 * reply out of turn, a write that fails; and as a skipping batch cannot
 * be undone to its savepoint, or released, or committed under autocommit.
 */
static void a_run_fails_as_it_cannot_go_on(trl_test *test) {
    static const reply_plan broken[] = {PLAN_DONE, PLAN_BROKEN_ROWS};
    serving_sequence sequence = {{PLAN_DONE, true}, broken, 2U, 0U};
    script s;
    trl_conn *conn = serving_connect_sequence(&s, &sequence);
    trl_pipeline *pipeline = NULL;
    (void)trl_pipeline_new(conn, &pipeline);
    (void)trl_pipeline_query(pipeline, "VALUES 1");
    (void)trl_pipeline_query(pipeline, "VALUES 2");
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    TRL_CHECK(test, trl_pipeline_count(pipeline) == 0U);
    trl_pipeline_free(pipeline);
    trl_conn_free(conn);
    script_free(&s);
    sequence.answered = 0U;
    sequence.first_count = 0U;
    conn = serving_connect_sequence(&s, &sequence);
    s.fail_writes = true;
    (void)trl_pipeline_new(conn, &pipeline);
    (void)trl_pipeline_query(pipeline, "VALUES 1");
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_FAILED);
    trl_pipeline_free(pipeline);
    trl_conn_free(conn);
    script_free(&s);
    static const reply_plan undo_fails[] = {PLAN_DONE, PLAN_DONE, PLAN_ERROR, PLAN_DONE, PLAN_DONE, PLAN_ERROR};
    static const reply_plan release_fails[] = {
        PLAN_DONE, PLAN_DONE, PLAN_ERROR, PLAN_DONE, PLAN_DONE, PLAN_DONE, PLAN_ERROR,
    };
    static const reply_plan commit_fails[] = {
        PLAN_DONE, PLAN_DONE, PLAN_DONE, PLAN_DONE, PLAN_DONE, PLAN_DONE, PLAN_ERROR,
    };
    const reply_plan *failing[] = {undo_fails, release_fails, commit_fails};
    const size_t counts[] = {6U, 7U, 7U};
    bool every = true;
    for (size_t k = 0U; k < 3U; ++k) {
        serving_sequence each = {{PLAN_DONE, true}, failing[k], counts[k], 0U};
        conn = serving_connect_sequence(&s, &each);
        (void)trl_conn_set_autocommit(conn, k == 2U);
        (void)trl_pipeline_new(conn, &pipeline);
        (void)trl_pipeline_on_error(pipeline, TRL_ON_ERROR_SKIP);
        (void)trl_pipeline_query(pipeline, "VALUES 1");
        (void)trl_pipeline_query(pipeline, "VALUES 2");
        (void)trl_pipeline_query(pipeline, "VALUES 3");
        every = every && trl_pipeline_run(pipeline) == TRL_FAILED;
        trl_pipeline_free(pipeline);
        trl_conn_free(conn);
        script_free(&s);
    }
    TRL_CHECK(test, every);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(each_request_has_its_outcome),
        TRL_TEST_CASE(a_pipeline_refuses_null_arguments),
        TRL_TEST_CASE(a_skipping_batch_is_settled),
        TRL_TEST_CASE(a_run_fails_as_it_cannot_go_on),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
