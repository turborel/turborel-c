/**
 * @file pool_test.c
 * @brief Pools over scripted servers: a connection checked and reset by
 * the protocol or by statements, closed as its check or reset fails, and
 * each refusal of the pool's own.
 */
#include "codec/kinds.h"
#include "conn/conn.h"
#include "failure.h"
#include "format.h"
#include "harness.h"
#include "script.h"
#include "serving.h"
#include "sync.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The most connections a test's connector makes. */
#define MADE_MOST 8U

/* A connector of scripted connections, each served by the plans given. */
typedef struct maker {
    script scripts[MADE_MOST];
    serving_sequence plans[MADE_MOST];
    size_t made;
    bool everything;
    const reply_plan *first;
    size_t first_count;
    trl_status fails;
} maker;

static trl_status make(void *context, trl_conn **out) {
    maker *m = context;
    if (m->fails != TRL_OK) {
        *out = NULL;
        if (m->fails == TRL_FAILED) {
            trl_failure failure;
            trl_failure_init(&failure);
            trl_failure_set(&failure, -919, "08004", "refused");
            *out = trl_conn_failed_with(&failure);
            trl_failure_clear(&failure);
        }
        return m->fails;
    }
    size_t at = m->made++;
    serving_sequence plan = {{PLAN_DONE, m->everything}, m->first, m->first_count, 0U};
    m->plans[at] = plan;
    *out = serving_connect_sequence(&m->scripts[at], &m->plans[at]);
    return TRL_OK;
}

static void maker_free(maker *m) {
    for (size_t i = 0U; i < m->made; ++i) {
        script_free(&m->scripts[i]);
    }
}

/* A pool of `m`'s connections, at most `max`, checked if `check`. */
static trl_pool *pool_of(maker *m, size_t max, bool check) {
    trl_pool_config config = trl_pool_config_default();
    config.max = max;
    config.max_idle = max;
    config.check = check;
    trl_pool *pool = NULL;
    (void)trl_pool_new(make, m, &config, &pool);
    return pool;
}

/* Whether the pool has made `made`, and holds `idle` idle. */
static bool counts_are(trl_pool *pool, size_t made, size_t idle) {
    size_t m = 99U;
    size_t i = 99U;
    trl_pool_counts(pool, &m, &i);
    return m == made && i == idle;
}

/**
 * Verifies: [CLIENT Pools] a pool's defaults: ten made and idle, ten
 * minutes idle, an hour's life, thirty seconds' wait, checked, no reset
 * statements; a pool refuses a null connector or out, no room for one
 * connection, and reset statements not given.
 */
static void a_pool_has_its_defaults_and_refusals(trl_test *test) {
    trl_pool_config config = trl_pool_config_default();
    TRL_CHECK(test, config.max == 10U && config.max_idle == 10U && config.idle_timeout_ms == 600000U);
    TRL_CHECK(test, config.max_lifetime_ms == 3600000U && config.wait_ms == 30000U && config.check);
    TRL_CHECK(test, config.reset == NULL && config.reset_count == 0U);
    maker m = {0};
    trl_pool *pool = NULL;
    TRL_CHECK(test, trl_pool_new(NULL, &m, NULL, &pool) == TRL_INVALID_ARGUMENT && pool == NULL);
    TRL_CHECK(test, trl_pool_new(make, &m, NULL, NULL) == TRL_INVALID_ARGUMENT);
    config.max = 0U;
    TRL_CHECK(test, trl_pool_new(make, &m, &config, &pool) == TRL_INVALID_ARGUMENT);
    config.max = 1U;
    config.reset_count = 1U;
    TRL_CHECK(test, trl_pool_new(make, &m, &config, &pool) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pool_of_settings(NULL, NULL, &pool) == TRL_INVALID_ARGUMENT && pool == NULL);
    TRL_CHECK(test, trl_pool_of_settings(NULL, NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pool_new(make, &m, NULL, &pool) == TRL_OK && counts_are(pool, 0U, 0U));
    trl_conn *conn = NULL;
    TRL_CHECK(test,
              trl_pool_get(NULL, &conn) == TRL_INVALID_ARGUMENT && trl_pool_get(pool, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_pool_hold(NULL) == NULL);
    trl_pool_counts(NULL, NULL, NULL);
    trl_pool_counts(pool, NULL, NULL);
    trl_pool_release(NULL, NULL);
    trl_pool_release(pool, NULL);
    trl_pool_free(pool);
    trl_pool_free(NULL);
    char text[32];
    trl_format_duration(text, sizeof text, 0U);
    TRL_CHECK(test, strcmp(text, "0ns") == 0);
    trl_format_duration(text, sizeof text, 250U);
    TRL_CHECK(test, strcmp(text, "250ms") == 0);
    trl_format_duration(text, sizeof text, 30000U);
    TRL_CHECK(test, strcmp(text, "30s") == 0);
    trl_format_duration(text, sizeof text, 1500U);
    TRL_CHECK(test, strcmp(text, "1.5s") == 0);
    trl_format_duration(text, sizeof text, 2005U);
    TRL_CHECK(test, strcmp(text, "2.005s") == 0);
}

/**
 * Verifies: [CLIENT Pools] of a server granting Reset, a connection given
 * back is reset by it, autocommit off, then the program's statements run,
 * and kept; one whose reset or statements fail is closed; one lost is
 * closed; one not of the pool is freed.
 */
static void a_connection_is_reset_by_the_protocol(trl_test *test) {
    maker m = {0};
    m.everything = true;
    static const char *const statements[] = {"SET SCHEMA 'S'"};
    trl_pool_config config = trl_pool_config_default();
    config.max = 2U;
    config.check = false;
    config.reset = statements;
    config.reset_count = 1U;
    trl_pool *pool = NULL;
    TRL_CHECK(test, trl_pool_new(make, &m, &config, &pool) == TRL_OK);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK && counts_are(pool, 1U, 0U));
    (void)trl_conn_set_autocommit(conn, true);
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 1U, 1U) && m.plans[0].answered == 2U);
    TRL_CHECK(test, m.scripts[0].kinds[1] == TRL_KIND_RESET && !trl_conn_autocommit(conn));
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK && counts_are(pool, 1U, 0U));
    m.plans[0].then.plan = PLAN_ERROR;
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    static const reply_plan reset_then_fail[] = {PLAN_DONE, PLAN_ERROR};
    m.plans[1].first = reset_then_fail;
    m.plans[1].first_count = 2U;
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    m.scripts[2].fail_writes = true;
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "VALUES 1", &result) == TRL_FAILED && trl_conn_is_lost(conn));
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    trl_conn *stranger = NULL;
    (void)make(&m, &stranger);
    trl_pool_release(pool, stranger);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    trl_pool_free(pool);
    maker_free(&m);
}

/**
 * Verifies: [CLIENT Pools] of a server granting neither Ping nor Reset, a
 * connection is checked by ROLLBACK, and one failing its check is closed
 * and another made; it is reset by statements in one pipeline, the
 * statements it prepared released, a statement gone already no failure;
 * one whose reset fails is closed, as is one that prepared too many.
 */
static void a_connection_is_checked_and_reset_by_statements(trl_test *test) {
    maker m = {0};
    trl_pool *pool = pool_of(&m, 2U, true);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK && trl_conn_granted_count(conn) == 0U);
    conn->next_statement = 2U;
    static const reply_plan released[] = {PLAN_DONE, PLAN_DONE, PLAN_ERROR, PLAN_DONE, PLAN_DONE};
    m.plans[0].first = released;
    m.plans[0].first_count = 5U;
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 1U, 1U) && m.plans[0].answered == 8U && conn->next_statement == 0U);
    m.plans[0].then.plan = PLAN_ERROR;
    trl_conn *again = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &again) == TRL_OK && m.made == 2U && counts_are(pool, 1U, 0U));
    trl_pool_release(pool, again);
    TRL_CHECK(test, counts_are(pool, 1U, 1U));
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    m.plans[1].then.plan = PLAN_ERROR;
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    conn->next_statement = TRL_POOL_RESET_MOST_STATEMENTS + 1U;
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    trl_pool_free(pool);
    maker_free(&m);
}

/**
 * Verifies: [CLIENT Pools] get fails with the failure of making a
 * connection, or of memory not had, the pool's room given back; a pool
 * whose every connection is handed out waits its limit and fails
 * (SQLSTATE 08001); a connection older than its life is closed as it
 * comes back, and none beyond the most idle kept.
 */
static void a_get_fails_as_it_cannot_make_one(trl_test *test) {
    maker m = {0};
    m.fails = TRL_FAILED;
    trl_pool_config config = trl_pool_config_default();
    config.max = 1U;
    config.max_idle = 0U;
    config.wait_ms = 0U;
    trl_pool *pool = NULL;
    (void)trl_pool_new(make, &m, &config, &pool);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08004") == 0 && counts_are(pool, 0U, 0U));
    trl_conn_free(conn);
    m.fails = TRL_NO_MEMORY;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_NO_MEMORY && conn == NULL && counts_are(pool, 0U, 0U));
    m.fails = TRL_OK;
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    trl_conn *waited = NULL;
    TRL_CHECK(test, trl_pool_get(pool, &waited) == TRL_FAILED);
    TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(waited)), "came back within 0ns") != NULL);
    trl_conn_free(waited);
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    trl_pool_free(pool);
    config.max_idle = 1U;
    config.max_lifetime_ms = 1U;
    (void)trl_pool_new(make, &m, &config, &pool);
    TRL_CHECK(test, trl_pool_get(pool, &conn) == TRL_OK);
    uint64_t until = trl_now_ms() + 5U;
    while (trl_now_ms() < until) {
        /* Its life passes. */
    }
    trl_pool_release(pool, conn);
    TRL_CHECK(test, counts_are(pool, 0U, 0U));
    trl_pool_free(pool);
    maker_free(&m);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_pool_has_its_defaults_and_refusals),
        TRL_TEST_CASE(a_connection_is_reset_by_the_protocol),
        TRL_TEST_CASE(a_connection_is_checked_and_reset_by_statements),
        TRL_TEST_CASE(a_get_fails_as_it_cannot_make_one),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
