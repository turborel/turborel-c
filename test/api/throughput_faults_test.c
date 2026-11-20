/**
 * @file throughput_faults_test.c
 * @brief Memory not had in pipelines, pools and the SQL-agent (RULES.md
 * R-MEM-2), over scripted servers: a batch queued and run, skipping;
 * connections got, reset by statements and checked; connections made,
 * chosen and ended by connection statements; each allocation failing in
 * turn, never crashing or leaking. Built with TRL_FAULTS alone.
 */
#include "faults.h"
#include "harness.h"
#include "script.h"
#include "serving.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most connections a scenario makes. */
#define MADE_MOST 4U

/* Scripted connections a scenario makes, each its own server. */
typedef struct maker {
    script scripts[MADE_MOST];
    serving_sequence plans[MADE_MOST];
    size_t made;
} maker;

static trl_status make(maker *m, trl_conn **out) {
    *out = NULL;
    if (m->made == MADE_MOST) {
        return TRL_NO_MEMORY;
    }
    size_t at = m->made++;
    serving_sequence plan = {{PLAN_DONE, false}, NULL, 0U, 0U};
    m->plans[at] = plan;
    *out = serving_connect_sequence(&m->scripts[at], &m->plans[at]);
    if (*out == NULL) {
        return TRL_NO_MEMORY;
    }
    return trl_conn_is_lost(*out) ? TRL_FAILED : TRL_OK;
}

static void maker_free(maker *m) {
    for (size_t i = 0U; i < m->made; ++i) {
        script_free(&m->scripts[i]);
    }
}

/* Queues a query, an execution of `statement` and another query, and
   runs them skipping; whether what came of it is allowed. */
static bool batch_run(trl_conn *conn, const trl_statement *statement) {
    trl_params *params = NULL;
    trl_pipeline *pipeline = NULL;
    bool fine = true;
    bool made = trl_params_new(&params) == TRL_OK && trl_params_add_text(params, "x", 1U) == TRL_OK &&
                trl_pipeline_new(conn, &pipeline) == TRL_OK;
    if (made) {
        (void)trl_pipeline_on_error(pipeline, TRL_ON_ERROR_SKIP);
        trl_status queued = trl_pipeline_query(pipeline, "VALUES 1");
        queued = queued == TRL_OK ? trl_pipeline_execute(pipeline, statement, params) : queued;
        queued = queued == TRL_OK ? trl_pipeline_query(pipeline, "VALUES 2") : queued;
        fine = queued == TRL_OK || queued == TRL_NO_MEMORY;
        trl_status ran = queued == TRL_OK ? trl_pipeline_run(pipeline) : TRL_OK;
        fine = fine && allowed(ran, trl_conn_failure(conn));
    }
    trl_pipeline_free(pipeline);
    trl_params_free(params);
    return fine;
}

/* A batch queued, a query, an execution and a release, run skipping. */
static bool pipeline_session(void) {
    static const reply_plan prepared[] = {PLAN_PREPARED};
    serving_sequence plan = {{PLAN_DONE, true}, prepared, 1U, 0U};
    script s;
    trl_conn *conn = serving_connect_sequence(&s, &plan);
    bool fine = true;
    trl_statement *statement = NULL;
    if (conn != NULL && trl_prepare(conn, "VALUES ?", &statement) == TRL_OK) {
        fine = batch_run(conn, statement);
    }
    trl_statement_free(statement);
    trl_conn_free(conn);
    script_free(&s);
    return fine;
}

static trl_status pooled(void *context, trl_conn **out) {
    return make(context, out);
}

/* Connections got, given back reset by statements, checked and got again. */
static bool pool_session(void) {
    maker m = {0};
    static const char *const statements[] = {"SET SCHEMA 'S'"};
    trl_pool_config config = trl_pool_config_default();
    config.max = 2U;
    config.reset = statements;
    config.reset_count = 1U;
    trl_pool *pool = NULL;
    trl_status made = trl_pool_new(pooled, &m, &config, &pool);
    bool fine = made == TRL_OK || made == TRL_NO_MEMORY;
    for (int round = 0; pool != NULL && round < 2; ++round) {
        trl_conn *conn = NULL;
        trl_status got = trl_pool_get(pool, &conn);
        fine = fine && allowed(got, trl_conn_failure(conn));
        if (got == TRL_OK) {
            trl_pool_release(pool, conn);
        } else {
            trl_conn_free(conn);
        }
    }
    trl_pool_free(trl_pool_hold(pool));
    trl_pool_free(pool);
    maker_free(&m);
    return fine;
}

static trl_status dialed(void *context, const char *server, const char *user, trl_conn **out) {
    (void)server;
    (void)user;
    return make(context, out);
}

/* Connections made, chosen and ended, and a statement for the current one. */
static bool agent_session(void) {
    maker m = {0};
    trl_agent *agent = NULL;
    trl_status made = trl_agent_new(dialed, &m, &agent);
    bool fine = made == TRL_OK || made == TRL_NO_MEMORY;
    static const char *const statements[] = {
        "CONNECT TO 'a' AS 'b' USER 'ann'",
        "CONNECT TO DEFAULT",
        "SET CONNECTION ' b '",
        "VALUES 1",
        "DISCONNECT '\"B\"'",
        "SET CONNECTION DEFAULT",
        "DISCONNECT ALL",
    };
    for (size_t i = 0U; agent != NULL && i < sizeof statements / sizeof statements[0]; ++i) {
        trl_result *result = NULL;
        trl_status ran = trl_agent_query(agent, statements[i], &result);
        fine = fine && allowed(ran, trl_agent_failure(agent));
        trl_result_free(result);
    }
    trl_conn *first = NULL;
    trl_agent *connected = NULL;
    if (make(&m, &first) == TRL_OK) {
        made = trl_agent_connected(dialed, &m, first, &connected);
        fine = fine && (made == TRL_OK || made == TRL_NO_MEMORY);
    } else {
        trl_conn_free(first);
    }
    trl_status ended = connected != NULL ? trl_agent_terminate(connected) : TRL_OK;
    fine = fine && allowed(ended, trl_agent_failure(connected));
    trl_agent_free(connected);
    trl_agent_free(agent);
    maker_free(&m);
    return fine;
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a pipeline's batch queued
 * and run skipping, of a pool's connections got, reset and checked, and
 * of an agent's connection statements failing in turn fails the call as
 * memory not had, or as it fails without a fault, and leaks nothing.
 */
static void throughput_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(pipeline_session));
    TRL_CHECK(test, every_fault(pool_session));
    TRL_CHECK(test, every_fault(agent_session));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(throughput_survives_each_fault),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
