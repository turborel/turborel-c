/**
 * @file reset.c
 * @brief A pooled connection's check and reset [CLIENT Pools]. Granted
 * `ping`, the check sends Ping, which the server answers with no worker
 * and no statement; granted `reset`, the reset sends Reset, which ends
 * all the session holds in one request and keeps its user, then the
 * program's own statements run.
 *
 * A server that grants neither is checked and reset by statements: the
 * check is ROLLBACK, which a session outside a transaction completes
 * doing nothing; the reset gives the session back as it logged in, in
 * one pipeline: its transaction rolled back, the statements it prepared
 * closed and destroyed, its user, schema and zone those of its login,
 * then the program's own statements. A setting a borrower SET stays, as
 * no statement sets every setting back. Either way autocommit is off
 * after.
 */
#include "codec/buffer.h"
#include "pool/pool.h"
#include "stmt/pipeline.h"
#include "stmt/stmt.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long a pooled connection's check and reset wait on its server at
   most, in milliseconds: as long as a step of connecting. */
#define TRL_RESET_WITHIN_MS 30000U
/* What a pooled connection is checked by, of a server without Ping. */
#define TRL_CHECK "ROLLBACK"
/* The requests that release a statement: Close, then Deallocate. */
#define TRL_RELEASE_REQUESTS 2U

/* Runs `run` of `conn`, each of its waits at most TRL_RESET_WITHIN_MS, or
   the connection's own limit if shorter; the limit put back after. A
   connection over a stream its maker bounds is run as it is bounded. */
static bool bounded(trl_conn *conn, bool (*run)(trl_conn *conn, const trl_pool_config *config),
                    const trl_pool_config *config) {
    uint32_t before = trl_conn_read_timeout(conn);
    uint32_t within = before != 0U && before < TRL_RESET_WITHIN_MS ? before : TRL_RESET_WITHIN_MS;
    bool limited = trl_conn_set_read_timeout(conn, within) == TRL_OK;
    bool ran = run(conn, config);
    if (limited && !trl_conn_is_lost(conn)) {
        ran = trl_conn_set_read_timeout(conn, before) == TRL_OK && ran;
    }
    return ran;
}

/* Whether `conn` answers: Ping, or ROLLBACK. */
static bool answering(trl_conn *conn, const trl_pool_config *config) {
    (void)config;
    if (trl_conn_granted(conn, TRL_CAPABILITY_PING)) {
        return trl_conn_ping(conn) == TRL_OK;
    }
    trl_result *result = NULL;
    bool answered = trl_query(conn, TRL_CHECK, &result) == TRL_OK;
    trl_result_free(result);
    return answered;
}

bool trl_pool_answers(trl_conn *conn) {
    return bounded(conn, answering, NULL);
}

/* Runs the program's statements of a reset. */
static bool program_statements(trl_conn *conn, const trl_pool_config *config) {
    for (size_t i = 0U; i < config->reset_count; ++i) {
        trl_result *result = NULL;
        bool ran = trl_query(conn, config->reset[i], &result) == TRL_OK;
        trl_result_free(result);
        if (!ran) {
            return false;
        }
    }
    return true;
}

/* `name` as a character string literal of a delimited identifier, as SET
   SESSION AUTHORIZATION and SET SCHEMA take one, so a name of any case or
   character is the same name, after `verb`, into `out`. */
static void delimited(trl_buffer *out, const char *verb, const char *name) {
    trl_put_text(out, verb);
    trl_put_text(out, " '\"");
    for (const char *at = name; *at != '\0'; ++at) {
        if (*at == '"') {
            trl_put_text(out, "\"\"");
        } else if (*at == '\'') {
            trl_put_text(out, "''");
        } else {
            trl_put_u8(out, (uint8_t)*at);
        }
    }
    trl_put_text(out, "\"'");
    trl_put_u8(out, 0U);
}

/* Queues the reset by statements: ROLLBACK, each statement prepared
   released, the login's user, schema and zone, the program's statements. */
static bool queued(trl_pipeline *pipeline, const trl_conn *conn, const trl_pool_config *config) {
    trl_buffer user;
    trl_buffer schema;
    trl_buffer_init(&user);
    trl_buffer_init(&schema);
    delimited(&user, "SET SESSION AUTHORIZATION", trl_conn_authorization(conn));
    delimited(&schema, "SET SCHEMA", trl_conn_authorization(conn));
    bool made = user.state == TRL_BUFFER_OK && schema.state == TRL_BUFFER_OK &&
                trl_pipeline_query(pipeline, TRL_CHECK) == TRL_OK;
    for (uint32_t number = 0U; made && number < trl_statements_prepared(conn); ++number) {
        made = trl_pipeline_release(pipeline, number) == TRL_OK;
    }
    made = made && trl_pipeline_query(pipeline, (const char *)user.data) == TRL_OK &&
           trl_pipeline_query(pipeline, (const char *)schema.data) == TRL_OK &&
           trl_pipeline_query(pipeline, "SET TIME ZONE LOCAL") == TRL_OK;
    for (size_t i = 0U; made && i < config->reset_count; ++i) {
        made = trl_pipeline_query(pipeline, config->reset[i]) == TRL_OK;
    }
    trl_buffer_free(&user);
    trl_buffer_free(&schema);
    return made;
}

/* Whether a reset by statements ran: each statement but the releases of
   statements already gone. */
static bool reset_ran(const trl_pipeline *pipeline, uint32_t prepared) {
    size_t releases = (size_t)prepared * TRL_RELEASE_REQUESTS;
    for (size_t i = 0U; i < trl_pipeline_count(pipeline); ++i) {
        bool release = i >= 1U && i < 1U + releases;
        if (!release && trl_pipeline_outcome(pipeline, i) == TRL_PIPED_FAILED) {
            return false;
        }
    }
    return true;
}

/* Gives `conn`'s session back as it logged in, and runs the program's
   statements, unbounded. */
static bool resetting(trl_conn *conn, const trl_pool_config *config) {
    if (trl_conn_is_lost(conn)) {
        return false;
    }
    (void)trl_conn_set_autocommit(conn, false);
    if (trl_conn_granted(conn, TRL_CAPABILITY_RESET)) {
        bool renewed = trl_conn_reset(conn) == TRL_OK;
        /* Every statement it prepared went with the session it had. */
        if (renewed) {
            trl_statements_gone(conn);
        }
        return renewed && program_statements(conn, config);
    }
    uint32_t prepared = trl_statements_prepared(conn);
    if (prepared > TRL_POOL_RESET_MOST_STATEMENTS) {
        return false;
    }
    trl_pipeline *pipeline = NULL;
    bool ran = trl_pipeline_new(conn, &pipeline) == TRL_OK && queued(pipeline, conn, config) &&
               trl_pipeline_run(pipeline) == TRL_OK && reset_ran(pipeline, prepared);
    trl_pipeline_free(pipeline);
    if (ran) {
        trl_statements_gone(conn);
    }
    return ran;
}

bool trl_pool_reset(trl_conn *conn, const trl_pool_config *config) {
    return !trl_conn_is_lost(conn) && bounded(conn, resetting, config);
}
