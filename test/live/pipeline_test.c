/**
 * @file pipeline_test.c
 * @brief Pipelines against a real server (RULES.md R-TEST-8), as
 * turborel-client's own tests run them: requests written at once,
 * answered in order, a failure its request's own; or, skipping after an
 * error, the requests after it undone and those before kept; batches
 * longer than a window written a window at a time.
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
#define SQL_BYTES 256U

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    trl_result_free(result);
    return ran == TRL_OK;
}

/* How many rows T holds, as a new connection reads them; -1 if it cannot. */
static long keys(void) {
    trl_conn *conn = live_trusted(NULL);
    trl_result *result = NULL;
    long count = conn != NULL && trl_query(conn, "SELECT K FROM T", &result) == TRL_OK
                     ? (long)trl_result_row_count(result)
                     : -1L;
    trl_result_free(result);
    trl_conn_free(conn);
    return count;
}

/* A connection to a T emptied. */
static trl_conn *emptied(void) {
    trl_conn *conn = live_trusted(NULL);
    if (conn != NULL && (!run(conn, "DELETE FROM T") || !run(conn, "COMMIT"))) {
        trl_conn_free(conn);
        return NULL;
    }
    return conn;
}

/* Whether request `i` of `pipeline` failed with a SQLSTATE of class `class`. */
static bool failed_of(const trl_pipeline *pipeline, size_t i, const char *class) {
    const trl_failure *failure = trl_pipeline_failure(pipeline, i);
    return trl_pipeline_outcome(pipeline, i) == TRL_PIPED_FAILED && failure != NULL &&
           strncmp(trl_failure_sqlstate(failure), class, 2U) == 0;
}

/**
 * Verifies: [CLIENT Pipelines] a pipeline's requests, queries and
 * executions of a prepared statement, are answered one outcome each, in
 * the order queued; a failure, a key met twice (class 23), is its
 * request's alone, and the requests after it run; the connection goes on
 * after, its transaction active as the last request left it
 * [PROTOCOL Requests].
 */
static void a_pipeline_answers_each_request_in_order(trl_test *test) {
    trl_conn *conn = emptied();
    trl_statement *insert = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO T VALUES (?, 'v')", &insert) == TRL_OK);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    trl_decimal two = {{2U, 0U}, 0U};
    (void)trl_params_add_decimal(params, two);
    trl_pipeline *pipeline = NULL;
    TRL_CHECK(test, trl_pipeline_new(conn, &pipeline) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_query(pipeline, "INSERT INTO T VALUES (1, 'a')") == TRL_OK);
    TRL_CHECK(test, trl_pipeline_query(pipeline, "INSERT INTO T VALUES (1, 'again')") == TRL_OK);
    TRL_CHECK(test, trl_pipeline_execute(pipeline, insert, params) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_query(pipeline, "SELECT K, V FROM T ORDER BY K") == TRL_OK);
    TRL_CHECK(test, trl_pipeline_count(pipeline) == 4U && trl_pipeline_run(pipeline) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 0U) == TRL_PIPED_DONE && failed_of(pipeline, 1U, "23"));
    TRL_CHECK(test, trl_pipeline_outcome(pipeline, 2U) == TRL_PIPED_DONE);
    TRL_CHECK(test, trl_result_row_count(trl_pipeline_result(pipeline, 3U)) == 2U);
    TRL_CHECK(test, trl_conn_in_transaction(conn) && run(conn, "COMMIT") && keys() == 2L);
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_INVALID_ARGUMENT);
    trl_pipeline_free(pipeline);
    TRL_CHECK(test, trl_pipeline_new(conn, &pipeline) == TRL_OK && trl_pipeline_run(pipeline) == TRL_OK);
    TRL_CHECK(test, trl_pipeline_count(pipeline) == 0U);
    trl_pipeline_free(pipeline);
    trl_params_free(params);
    trl_statement_free(insert);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Pipelines] under autocommit each request of a
 * pipeline is its own transaction, so a failed one undoes none other;
 * skipping on error, the requests after the first failure are undone and
 * skipped, those before it kept, and under autocommit the batch's work
 * committed as it ends.
 */
static void a_pipeline_skips_after_an_error_when_asked(trl_test *test) {
    trl_conn *conn = emptied();
    TRL_CHECK(test, trl_conn_set_autocommit(conn, true) == TRL_OK);
    trl_pipeline *each = NULL;
    (void)trl_pipeline_new(conn, &each);
    (void)trl_pipeline_query(each, "INSERT INTO T VALUES (1, 'a')");
    (void)trl_pipeline_query(each, "INSERT INTO T VALUES (1, 'b')");
    (void)trl_pipeline_query(each, "INSERT INTO T VALUES (2, 'c')");
    TRL_CHECK(test, trl_pipeline_run(each) == TRL_OK && failed_of(each, 1U, "23"));
    TRL_CHECK(test, trl_pipeline_outcome(each, 2U) == TRL_PIPED_DONE);
    TRL_CHECK(test, !trl_conn_in_transaction(conn) && keys() == 2L);
    trl_pipeline_free(each);
    trl_pipeline *skipping = NULL;
    (void)trl_pipeline_new(conn, &skipping);
    TRL_CHECK(test, trl_pipeline_on_error(skipping, TRL_ON_ERROR_SKIP) == TRL_OK);
    (void)trl_pipeline_query(skipping, "INSERT INTO T VALUES (3, 'kept')");
    (void)trl_pipeline_query(skipping, "INSERT INTO T VALUES (1, 'twice')");
    (void)trl_pipeline_query(skipping, "INSERT INTO T VALUES (4, 'undone')");
    (void)trl_pipeline_query(skipping, "INSERT INTO T VALUES (5, 'undone')");
    TRL_CHECK(test, trl_pipeline_run(skipping) == TRL_OK && trl_pipeline_count(skipping) == 4U);
    TRL_CHECK(test, trl_pipeline_outcome(skipping, 0U) == TRL_PIPED_DONE && failed_of(skipping, 1U, "23"));
    TRL_CHECK(test, trl_pipeline_outcome(skipping, 2U) == TRL_PIPED_SKIPPED);
    TRL_CHECK(test, trl_pipeline_outcome(skipping, 3U) == TRL_PIPED_SKIPPED);
    TRL_CHECK(test, trl_pipeline_result(skipping, 2U) == NULL && trl_pipeline_failure(skipping, 2U) == NULL);
    TRL_CHECK(test, !trl_conn_in_transaction(conn) && keys() == 3L);
    trl_pipeline_free(skipping);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Pipelines] within a transaction a skipping pipeline
 * undoes only what came after its failure, the transaction's earlier work
 * kept and still active; its first request failing skips all the rest.
 */
static void a_skipping_pipeline_keeps_the_transaction_before_it(trl_test *test) {
    trl_conn *conn = emptied();
    TRL_CHECK(test, run(conn, "INSERT INTO T VALUES (1, 'before')"));
    trl_pipeline *skipping = NULL;
    (void)trl_pipeline_new(conn, &skipping);
    (void)trl_pipeline_on_error(skipping, TRL_ON_ERROR_SKIP);
    (void)trl_pipeline_query(skipping, "INSERT INTO T VALUES (1, 'twice')");
    (void)trl_pipeline_query(skipping, "INSERT INTO T VALUES (2, 'undone')");
    TRL_CHECK(test, trl_pipeline_run(skipping) == TRL_OK && failed_of(skipping, 0U, "23"));
    TRL_CHECK(test, trl_pipeline_outcome(skipping, 1U) == TRL_PIPED_SKIPPED);
    TRL_CHECK(test, trl_conn_in_transaction(conn));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT K FROM T", &result) == TRL_OK && trl_result_row_count(result) == 1U);
    trl_result_free(result);
    TRL_CHECK(test, run(conn, "COMMIT") && keys() == 1L);
    trl_pipeline_free(skipping);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Pipelines] a batch of many more bytes than a window is
 * written a window at a time, each reply read in order, and every request
 * answered: none waits on a server waiting on it.
 */
static void a_batch_longer_than_a_window_is_answered_whole(trl_test *test) {
    trl_conn *conn = emptied();
    char value[91];
    memset(value, 'w', 90U);
    value[90] = '\0';
    size_t count = TRL_PIPELINE_WINDOW / 50U;
    trl_pipeline *pipeline = NULL;
    (void)trl_pipeline_new(conn, &pipeline);
    for (size_t k = 0U; k < count; ++k) {
        char sql[SQL_BYTES];
        trl_format(sql, sizeof sql, "INSERT INTO T VALUES (%zu, '%s')", k, value);
        (void)trl_pipeline_query(pipeline, sql);
        (void)trl_pipeline_query(pipeline, "SELECT K, V FROM T WHERE K = 0");
    }
    TRL_CHECK(test, trl_pipeline_run(pipeline) == TRL_OK && trl_pipeline_count(pipeline) == count * 2U);
    bool every = true;
    for (size_t i = 0U; i < count * 2U; ++i) {
        every = every && trl_pipeline_outcome(pipeline, i) == TRL_PIPED_DONE;
    }
    TRL_CHECK(test, every && run(conn, "COMMIT") && keys() == (long)count);
    trl_pipeline_free(pipeline);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-pipeline"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-pipeline", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") &&
                 run(owner, "CREATE TABLE T (K INTEGER PRIMARY KEY, V VARCHAR(100))") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_pipeline_answers_each_request_in_order),
        TRL_TEST_CASE(a_pipeline_skips_after_an_error_when_asked),
        TRL_TEST_CASE(a_skipping_pipeline_keeps_the_transaction_before_it),
        TRL_TEST_CASE(a_batch_longer_than_a_window_is_answered_whole),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
