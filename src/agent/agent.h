/**
 * @file agent.h
 * @brief An SQL-agent's state, its connections and its failure, and how
 * its connection statements are run (`connections.c`).
 */
#ifndef TRL_AGENT_AGENT_H
#define TRL_AGENT_AGENT_H

#include "agent/statement.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief A connection of the agent: its name, null for the default connection. */
typedef struct trl_held {
    /** @brief Its name, an identifier as SQL reads it. */
    char *name;
    /** @brief The connection. */
    trl_conn *conn;
} trl_held;

/** @brief An agent: its connector, its connections, the current one's place, and its failure. */
struct trl_agent {
    /** @brief What makes its connections. */
    trl_agent_connector connect;
    /** @brief Its context. */
    void *context;
    /** @brief Its connections. */
    trl_held *held;
    /** @brief How many. */
    size_t count;
    /** @brief How many its list holds. */
    size_t capacity;
    /** @brief The current connection's place, or `count` for none. */
    size_t current;
    /** @brief Why its last statement failed. */
    trl_failure failure;
};

/**
 * @brief Fails the statement with the connection exception of `sqlstate`,
 * citing `rule`: of class 28 a privilege's SQLCODE, of 0A one not
 * supported, else a connection's; TRL_FAILED.
 */
trl_status trl_agent_exception(trl_agent *agent, const char *sqlstate, const char *rule, const char *message);

/** @brief Fails the statement of memory not had; TRL_NO_MEMORY. */
trl_status trl_agent_no_memory(trl_agent *agent);

/** @brief The status of the failure set: TRL_NO_MEMORY of memory not had, else TRL_FAILED. */
trl_status trl_agent_failed(const trl_agent *agent);

/** @brief Adds `conn`, named `name`, the agent's own, current after; false if memory cannot be had. */
bool trl_agent_hold(trl_agent *agent, char *name, trl_conn *conn);

/** @brief The place of the connection named `name`, null for the default one; `count` if none. */
size_t trl_agent_find(const trl_agent *agent, const char *name);

/** @brief Runs CONNECT, SET CONNECTION or DISCONNECT, read; its completion into `out`. */
trl_status trl_agent_connection_statement(trl_agent *agent, trl_agent_statement *statement, trl_result **out);

#endif
