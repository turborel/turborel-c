/**
 * @file pool_test.c
 * @brief Pools of connections against a real server (RULES.md R-TEST-8),
 * as turborel-client's own tests run them: bounded under many threads,
 * waiting at most their limit, each connection given back reset to its
 * login, one that no longer answers or has lived its time closed.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "format.h"
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

/* The platform declares its threads in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
#define THREADS 8U
#define ROUNDS 5U
#define SQL_BYTES 128U

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    trl_result_free(result);
    return ran == TRL_OK;
}

/* The settings of the pools, their connections named `pooled`. */
static trl_settings *settings;

/* A pool of at most `max` connections, waiting `wait_ms`. */
static trl_pool *pool_of(size_t max, uint32_t wait_ms) {
    trl_pool_config config = trl_pool_config_default();
    config.max = max;
    config.max_idle = max;
    config.wait_ms = wait_ms;
    trl_pool *pool = NULL;
    (void)trl_pool_of_settings(settings, &config, &pool);
    return pool;
}

/* How many rows a query of one count gives as its count; -1 if it fails. */
static long count_of(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    long count = -1L;
    if (trl_query(conn, sql, &result) == TRL_OK && trl_result_row_count(result) == 1U) {
        char text[32];
        (void)trl_value_to_text(trl_result_value(result, 0U, 0U), text, sizeof text, NULL);
        count = strtol(text, NULL, 10);
    }
    trl_result_free(result);
    return count;
}

/* A borrower: its pool, and the most connections it saw made. */
typedef struct borrower {
    trl_pool *pool;
    size_t peak;
    bool fine;
} borrower;

static void *borrow(void *context) {
    borrower *b = context;
    for (size_t i = 0U; i < ROUNDS; ++i) {
        trl_conn *conn = NULL;
        if (trl_pool_get(b->pool, &conn) != TRL_OK) {
            b->fine = false;
            trl_conn_free(conn);
            continue;
        }
        size_t made = 0U;
        trl_pool_counts(b->pool, &made, NULL);
        b->peak = made > b->peak ? made : b->peak;
        b->fine = b->fine && count_of(conn, "SELECT COUNT(*) FROM T") == 0L;
        trl_pool_release(b->pool, conn);
    }
    return NULL;
}

/**
 * Verifies: [CLIENT Pools] eight threads borrowing from a pool of three,
 * again and again, each wait for a connection to come back; never more
 * than three are made, each borrower's statements run, and as the threads
 * end every connection made is idle; a hold of the pool is the same pool.
 */
static void a_pool_bounds_its_connections_under_threads(trl_test *test) {
    trl_pool *pool = pool_of(3U, 30000U);
    borrower borrowers[THREADS];
    pthread_t threads[THREADS];
    for (size_t i = 0U; i < THREADS; ++i) {
        borrowers[i].pool = trl_pool_hold(pool);
        borrowers[i].peak = 0U;
        borrowers[i].fine = true;
        TRL_CHECK(test, pthread_create(&threads[i], NULL, borrow, &borrowers[i]) == 0);
    }
    size_t peak = 0U;
    bool fine = true;
    for (size_t i = 0U; i < THREADS; ++i) {
        (void)pthread_join(threads[i], NULL);
        peak = borrowers[i].peak > peak ? borrowers[i].peak : peak;
        fine = fine && borrowers[i].fine;
        trl_pool_free(borrowers[i].pool);
    }
    size_t made = 0U;
    size_t idle = 0U;
    trl_pool_counts(pool, &made, &idle);
    TRL_CHECK(test, fine && peak <= 3U && made <= 3U && made == idle);
    trl_pool_free(pool);
}

/**
 * Verifies: [CLIENT Pools] a pool whose every connection is handed out
 * waits its limit, then fails (SQLSTATE 08001); the connection given back
 * is handed out again, none more made.
 */
static void an_exhausted_pool_waits_its_limit(trl_test *test) {
    trl_pool *pool = pool_of(1U, 50U);
    trl_conn *held = NULL;
    trl_conn *waited = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &held) == TRL_OK);
    TRL_CHECK(test, trl_pool_get(pool, &waited) == TRL_FAILED);
    const trl_failure *failure = trl_conn_failure(waited);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(failure), "08001") == 0);
    TRL_CHECK(test,
              strstr(trl_failure_message(failure), "no connection of the pool's 1 came back within 50ms") != NULL);
    trl_conn_free(waited);
    trl_pool_release(pool, held);
    trl_conn *again = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &again) == TRL_OK);
    size_t made = 9U;
    size_t idle = 9U;
    trl_pool_counts(pool, &made, &idle);
    TRL_CHECK(test, made == 1U && idle == 0U);
    trl_pool_release(pool, again);
    trl_pool_free(pool);
}

/* Asserts `conn`, of `pool`, given back is reset: its transaction rolled
   back, its statements gone, its schema its user's, autocommit off. */
static void given_back_reset(trl_test *test, trl_pool *pool, trl_conn *conn) {
    TRL_CHECK(test, run(conn, "INSERT INTO T VALUES (1)"));
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT K FROM T", &statement) == TRL_OK);
    trl_statement_free(statement);
    TRL_CHECK(test, run(conn, "SET SCHEMA 'ELSEWHERE'"));
    (void)trl_conn_set_autocommit(conn, true);
    TRL_CHECK(test, trl_conn_in_transaction(conn));
    trl_pool_release(pool, conn);
    trl_conn *again = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &again) == TRL_OK);
    TRL_CHECK(test, !trl_conn_in_transaction(again) && !trl_conn_autocommit(again));
    TRL_CHECK(test, count_of(again, "SELECT COUNT(*) FROM T") == 0L);
    trl_statement *fresh = NULL;
    trl_result *result = NULL;
    TRL_CHECK(test, trl_prepare(again, "SELECT K FROM T", &fresh) == TRL_OK);
    TRL_CHECK(test, trl_execute(again, fresh, NULL, &result) == TRL_OK && trl_result_row_count(result) == 0U);
    trl_result_free(result);
    trl_statement_free(fresh);
    TRL_CHECK(test, run(again, "ROLLBACK"));
    size_t made = 0U;
    size_t idle = 9U;
    trl_pool_counts(pool, &made, &idle);
    TRL_CHECK(test, made == 1U && idle == 0U);
    trl_pool_release(pool, again);
}

/**
 * Verifies: [CLIENT Pools] a connection given back is reset to its login,
 * by Reset of a server that grants it: its transaction rolled back, the
 * rows it wrote gone; its prepared statements destroyed, so the next
 * borrower prepares afresh, more of them than statements would destroy
 * kept the connection; its schema its user's again, autocommit off.
 */
static void a_connection_given_back_is_reset(trl_test *test) {
    trl_pool *pool = pool_of(1U, 30000U);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    for (unsigned int i = 0U; i <= TRL_POOL_RESET_MOST_STATEMENTS; ++i) {
        trl_statement *statement = NULL;
        (void)trl_prepare(conn, "VALUES 1", &statement);
        trl_statement_free(statement);
    }
    given_back_reset(test, pool, conn);
    trl_pool_free(pool);
}

/* A connection granted nothing: trusted, over TCP, asking no capability. */
static trl_status granted_nothing(void *context, trl_conn **out) {
    (void)context;
    trl_conn_options *options = NULL;
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_ask_none(options);
    (void)trl_target_tcp("127.0.0.1", SERVER_PORT, &target);
    (void)trl_mechanism_trust(&trust);
    trl_status made = trl_connect(target, "ann", trust, options, out);
    trl_mechanism_free(trust);
    trl_target_free(target);
    trl_conn_options_free(options);
    return made;
}

/**
 * Verifies: [CLIENT Pools] of a server that grants neither Ping nor
 * Reset, a connection is checked by ROLLBACK and reset by statements, to
 * the same login; with too many statements left prepared, it is closed
 * rather than kept; a pool made of a function connects by it.
 */
static void a_connection_granted_nothing_is_reset_by_statements(trl_test *test) {
    trl_pool_config config = trl_pool_config_default();
    config.max = 1U;
    config.max_idle = 1U;
    trl_pool *pool = NULL;
    TRL_CHECK(test, trl_pool_new(granted_nothing, NULL, &config, &pool) == TRL_OK);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK && trl_conn_granted_count(conn) == 0U);
    given_back_reset(test, pool, conn);
    trl_conn *many = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &many) == TRL_OK);
    for (unsigned int i = 0U; i <= TRL_POOL_RESET_MOST_STATEMENTS; ++i) {
        trl_statement *statement = NULL;
        (void)trl_prepare(many, "VALUES 1", &statement);
        trl_statement_free(statement);
    }
    trl_pool_release(pool, many);
    size_t made = 9U;
    size_t idle = 9U;
    trl_pool_counts(pool, &made, &idle);
    TRL_CHECK(test, made == 0U && idle == 0U);
    trl_pool_free(pool);
}

/**
 * Verifies: [CLIENT Pools] an idle connection its server ended fails its
 * check as it is handed out, and is closed; a new one is made in its
 * place.
 */
static void a_connection_that_no_longer_answers_is_replaced(trl_test *test) {
    trl_pool *pool = pool_of(2U, 30000U);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    trl_pool_release(pool, conn);
    trl_conn *owner = live_trusted(NULL);
    long id = count_of(owner, "SELECT ID FROM INFORMATION_SCHEMA.CONNECTIONS WHERE APPLICATION = 'pooled'");
    char sql[SQL_BYTES];
    trl_format(sql, sizeof sql, "END CONNECTION %ld", id);
    TRL_CHECK(test, id > 0L && run(owner, sql) && run(owner, "ROLLBACK"));
    trl_conn_free(owner);
    struct timespec pause = {0, 100000000L};
    (void)nanosleep(&pause, NULL);
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK && run(conn, "ROLLBACK"));
    size_t made = 0U;
    size_t idle = 9U;
    trl_pool_counts(pool, &made, &idle);
    TRL_CHECK(test, made == 1U && idle == 0U);
    trl_pool_release(pool, conn);
    trl_pool_free(pool);
}

/**
 * Verifies: [CLIENT Pools] a connection that has lived its time is closed
 * as it comes back; one idle past its limit is closed rather than handed
 * out, another made.
 */
static void connections_live_and_idle_within_their_limits(trl_test *test) {
    struct timespec pause = {0, 5000000L};
    trl_pool_config config = trl_pool_config_default();
    config.max_lifetime_ms = 1U;
    trl_pool *lived = NULL;
    TRL_CHECK(test, trl_pool_of_settings(settings, &config, &lived) == TRL_OK);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(lived, &conn) == TRL_OK);
    (void)nanosleep(&pause, NULL);
    trl_pool_release(lived, conn);
    size_t made = 9U;
    size_t idle = 9U;
    trl_pool_counts(lived, &made, &idle);
    TRL_CHECK(test, made == 0U && idle == 0U);
    trl_pool_free(lived);
    config = trl_pool_config_default();
    config.idle_timeout_ms = 1U;
    config.check = false;
    trl_pool *resting = NULL;
    TRL_CHECK(test, trl_pool_of_settings(settings, &config, &resting) == TRL_OK);
    TRL_CHECK(test, trl_pool_get(resting, &conn) == TRL_OK);
    trl_conn *first = conn;
    trl_pool_release(resting, conn);
    trl_pool_counts(resting, &made, &idle);
    TRL_CHECK(test, made == 1U && idle == 1U);
    (void)nanosleep(&pause, NULL);
    TRL_CHECK(test, trl_pool_get(resting, &conn) == TRL_OK);
    trl_pool_release(resting, conn);
    trl_pool_counts(resting, &made, &idle);
    TRL_CHECK(test, made == 1U && idle == 1U);
    (void)first;
    trl_pool_free(resting);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-pool"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-pool", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") &&
                 run(owner, "CREATE TABLE T (K INTEGER)") && run(owner, "COMMIT");
    trl_conn_free(owner);
    ready = ready && trl_settings_from_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable&application_name=pooled",
                                           &settings) == TRL_OK;
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_pool_bounds_its_connections_under_threads),
        TRL_TEST_CASE(an_exhausted_pool_waits_its_limit),
        TRL_TEST_CASE(a_connection_given_back_is_reset),
        TRL_TEST_CASE(a_connection_granted_nothing_is_reset_by_statements),
        TRL_TEST_CASE(a_connection_that_no_longer_answers_is_replaced),
        TRL_TEST_CASE(connections_live_and_idle_within_their_limits),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    trl_settings_free(settings);
    server_stop(&live_server);
    return result;
}
// NOLINTEND(misc-include-cleaner)
