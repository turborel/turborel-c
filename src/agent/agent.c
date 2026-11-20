/**
 * @file agent.c
 * @brief The SQL-agent [CLIENT The SQL-agent], as turborel-client's
 * `agent::Agent` is: a program's connections, each named, one current;
 * CONNECT, SET CONNECTION and DISCONNECT run by the agent itself
 * (`connections.c`), every other statement in the current connection's
 * session.
 */
#include "turborel/agent.h"

#include "agent/agent.h"
#include "agent/statement.h"
#include "codec/error.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The SQLCODE of a privilege's failure. */
#define TRL_SQLCODE_PRIVILEGE (-104)
/* The SQLSTATE of no connection. */
#define TRL_SQLSTATE_MISSING "08003"
/* The first connections an agent holds room for, and how its room grows. */
#define TRL_AGENT_FIRST 4U
#define TRL_AGENT_GROWTH 2U
/* The characters of a SQLSTATE's class. */
#define TRL_CLASS_BYTES 2U

/* Fails the statement with the connection exception of `sqlstate`, as
   turborel-client's are: of class 28 a privilege's SQLCODE, of 0A one not
   supported, else a connection's. */
trl_status trl_agent_exception(trl_agent *agent, const char *sqlstate, const char *rule, const char *message) {
    int32_t sqlcode = TRL_SQLCODE_CONNECTION;
    if (strncmp(sqlstate, "28", TRL_CLASS_BYTES) == 0) {
        sqlcode = TRL_SQLCODE_PRIVILEGE;
    } else if (strncmp(sqlstate, "0A", TRL_CLASS_BYTES) == 0) {
        sqlcode = TRL_SQLCODE_UNSUPPORTED;
    }
    trl_failure_cite(&agent->failure, sqlcode, sqlstate, message, rule);
    return TRL_FAILED;
}

/* Fails the statement of memory not had. */
trl_status trl_agent_no_memory(trl_agent *agent) {
    trl_failure_no_memory(&agent->failure);
    return TRL_NO_MEMORY;
}

/* The status of the failure set: of memory not had, or another. */
trl_status trl_agent_failed(const trl_agent *agent) {
    return agent->failure.sqlcode == TRL_SQLCODE_RESOURCES ? TRL_NO_MEMORY : TRL_FAILED;
}

trl_status trl_agent_new(trl_agent_connector connect, void *context, trl_agent **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (connect == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_agent *agent = trl_calloc(1U, sizeof *agent);
    if (agent == NULL) {
        return TRL_NO_MEMORY;
    }
    agent->connect = connect;
    agent->context = context;
    trl_failure_init(&agent->failure);
    *out = agent;
    return TRL_OK;
}

/* Adds `conn`, named `name`, the agent's own, current after. */
bool trl_agent_hold(trl_agent *agent, char *name, trl_conn *conn) {
    if (agent->count == agent->capacity) {
        size_t capacity = agent->capacity == 0U ? TRL_AGENT_FIRST : agent->capacity * TRL_AGENT_GROWTH;
        trl_held *grown = trl_realloc(agent->held, capacity * sizeof *grown);
        if (grown == NULL) {
            return false;
        }
        agent->held = grown;
        agent->capacity = capacity;
    }
    trl_held h = {name, conn};
    agent->held[agent->count] = h;
    agent->current = agent->count++;
    return true;
}

trl_status trl_agent_connected(trl_agent_connector connect, void *context, trl_conn *conn, trl_agent **out) {
    if (conn == NULL) {
        if (out != NULL) {
            *out = NULL;
        }
        return TRL_INVALID_ARGUMENT;
    }
    trl_status made = trl_agent_new(connect, context, out);
    if (made != TRL_OK) {
        trl_conn_free(conn);
        return made;
    }
    if (!trl_agent_hold(*out, NULL, conn)) {
        trl_conn_free(conn);
        trl_agent_free(*out);
        *out = NULL;
        return TRL_NO_MEMORY;
    }
    return TRL_OK;
}

trl_conn *trl_agent_current(const trl_agent *agent) {
    return agent != NULL && agent->current < agent->count ? agent->held[agent->current].conn : NULL;
}

const char *trl_agent_current_name(const trl_agent *agent) {
    return agent != NULL && agent->current < agent->count ? agent->held[agent->current].name : NULL;
}

size_t trl_agent_count(const trl_agent *agent) {
    return agent != NULL ? agent->count : 0U;
}

const trl_failure *trl_agent_failure(const trl_agent *agent) {
    return agent != NULL ? &agent->failure : NULL;
}

/* The place of the connection named `name`, null for the default one;
   `count` if none. */
size_t trl_agent_find(const trl_agent *agent, const char *name) {
    for (size_t i = 0U; i < agent->count; ++i) {
        const char *its = agent->held[i].name;
        if ((name == NULL && its == NULL) || (name != NULL && its != NULL && strcmp(name, its) == 0)) {
            return i;
        }
    }
    return agent->count;
}

/* How a statement for the current connection runs: whole, or a batch at
   a time. */
typedef struct how {
    trl_batch_handler handler;
    void *context;
} how;

/* Runs `sql` in the current connection's session, its failure the
   agent's. */
static trl_status on_current(trl_agent *agent, const char *sql, how by, trl_result **out) {
    trl_conn *conn = trl_agent_current(agent);
    if (conn == NULL) {
        return trl_agent_exception(agent, TRL_SQLSTATE_MISSING, "ISO/IEC 9075:1992 §15.3 GR9",
                                   "there is no current connection");
    }
    trl_status ran =
        by.handler == NULL ? trl_query(conn, sql, out) : trl_query_batches(conn, sql, by.handler, by.context, out);
    if (ran == TRL_FAILED) {
        trl_failure_copy(&agent->failure, trl_conn_failure(conn));
    }
    return ran;
}

/* Runs `sql`: a connection statement itself, any other in the current
   connection's session. */
static trl_status agent_run(trl_agent *agent, const char *sql, how by, trl_result **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (agent == NULL || sql == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_failure_clear(&agent->failure);
    trl_agent_statement statement;
    trl_agent_statement_init(&statement);
    trl_status ran = TRL_FAILED;
    if (!trl_agent_statement_read(sql, &statement, &agent->failure)) {
        ran = trl_agent_failed(agent);
    } else if (statement.verb != TRL_AGENT_OTHER) {
        ran = trl_agent_connection_statement(agent, &statement, out);
    } else {
        ran = on_current(agent, sql, by, out);
    }
    trl_agent_statement_free(&statement);
    return ran;
}

trl_status trl_agent_query(trl_agent *agent, const char *sql, trl_result **out) {
    how whole = {NULL, NULL};
    return agent_run(agent, sql, whole, out);
}

trl_status trl_agent_query_batches(trl_agent *agent, const char *sql, trl_batch_handler handler, void *context,
                                   trl_result **out) {
    if (handler == NULL) {
        if (out != NULL) {
            *out = NULL;
        }
        return TRL_INVALID_ARGUMENT;
    }
    how batches = {handler, context};
    return agent_run(agent, sql, batches, out);
}

trl_status trl_agent_terminate(trl_agent *agent) {
    if (agent == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_failure_clear(&agent->failure);
    trl_status ended = TRL_OK;
    for (size_t i = 0U; i < agent->count; ++i) {
        if (trl_conn_terminate(agent->held[i].conn) != TRL_OK && ended == TRL_OK) {
            trl_failure_copy(&agent->failure, trl_conn_failure(agent->held[i].conn));
            ended = TRL_FAILED;
        }
        trl_conn_free(agent->held[i].conn);
        free(agent->held[i].name);
    }
    agent->count = 0U;
    agent->current = 0U;
    return ended;
}

void trl_agent_free(trl_agent *agent) {
    if (agent == NULL) {
        return;
    }
    for (size_t i = 0U; i < agent->count; ++i) {
        trl_conn_free(agent->held[i].conn);
        free(agent->held[i].name);
    }
    free(agent->held);
    trl_failure_clear(&agent->failure);
    free(agent);
}
