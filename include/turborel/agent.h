/**
 * @file agent.h
 * @brief The SQL-agent [CLIENT The SQL-agent]: a program's connections,
 * each named, one current. It runs CONNECT, SET CONNECTION and
 * DISCONNECT itself, as SQL-92's SQL-agent does, and sends every other
 * statement to the current connection. A connector the program gives
 * makes the connections CONNECT asks for, to the server it names, as the
 * user it names.
 *
 * An agent is one thread's at a time, as a connection is.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_AGENT_H
#define TRL_AGENT_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/statements.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Makes the connection CONNECT asks for.
 *
 * @param context The program's.
 * @param server The server CONNECT TO names, its spaces trimmed; null for
 * CONNECT TO DEFAULT, the default server.
 * @param user The user USER names, trimmed; null for none, the user the
 * connector takes.
 * @param out Where the connection goes, made on TRL_FAILED too, holding
 * why; or left null on TRL_FAILED, `why` then saying why.
 * @param why A failure the agent lends, empty, which a connector that
 * makes no connection fills by trl_failure_tell(), so its own failure,
 * as a wrapper's, is told as a connection's is: of SQLSTATE 08006
 * SQLSTATE 08001, of any other 08004, its message kept.
 * @return TRL_OK, or why none was made.
 */
typedef trl_status (*trl_agent_connector)(void *context, const char *server, const char *user, trl_conn **out,
                                          trl_failure *why);

/** @brief An SQL-agent. */
typedef struct trl_agent trl_agent;

/**
 * @brief Makes an agent with no connection.
 *
 * @param connect What makes its connections.
 * @param context Its context.
 * @param out Where the agent goes, the caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_agent_new(trl_agent_connector connect, void *context, trl_agent **out);

/**
 * @brief Makes an agent whose default connection, current, is `conn`: as
 * a client begins, connected.
 *
 * @param connect What makes its other connections.
 * @param context Its context.
 * @param conn The connection, the agent's after, freed with it, even on
 * failure.
 * @param out Where the agent goes, the caller's to free.
 * @return As trl_agent_new() returns.
 */
TRL_API trl_status trl_agent_connected(trl_agent_connector connect, void *context, trl_conn *conn, trl_agent **out);

/**
 * @brief Runs one statement: CONNECT, SET CONNECTION or DISCONNECT
 * itself, its result the completion, SQLSTATE 00000, or 01002 of a
 * DISCONNECT that met an error ending a connection; any other in the
 * current connection's session, as trl_query() runs it.
 *
 * CONNECT fails with SQLSTATE 0A001 in an active transaction, 28000 of a
 * user that is no authorization identifier, 2E000 of a name that is no
 * identifier, 08002 of a name in use, 08001 if the server cannot be
 * reached and 08004 if it refuses; SET CONNECTION with 0A001, or 08003 of
 * no such connection; DISCONNECT with 08003 if it names none, or 25000 if
 * one it ends is in an active transaction; another statement with 08003
 * if there is no current connection.
 *
 * @param agent The agent.
 * @param sql The statement, UTF-8.
 * @param out Where the result goes, the caller's to free; null unless
 * TRL_OK.
 * @return TRL_OK; TRL_FAILED with the agent's failure; TRL_INVALID_ARGUMENT
 * of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_agent_query(trl_agent *agent, const char *sql, trl_result **out);

/**
 * @brief Runs one statement as trl_agent_query() does, a query's rows
 * given to `handler` a batch at a time, as trl_query_batches() gives them.
 *
 * @param agent The agent.
 * @param sql The statement.
 * @param handler Given each batch, on this thread.
 * @param context Passed to `handler`.
 * @param out Where the result goes, the caller's to free.
 * @return As trl_agent_query() returns.
 */
TRL_API trl_status trl_agent_query_batches(trl_agent *agent, const char *sql, trl_batch_handler handler, void *context,
                                           trl_result **out);

/**
 * @brief The current connection, still the agent's, to read what its
 * server last told it, or to cancel its statement.
 *
 * @param agent The agent.
 * @return The connection; null if there is none.
 */
TRL_API trl_conn *trl_agent_current(const trl_agent *agent);

/**
 * @brief The current connection's name.
 *
 * @param agent The agent.
 * @return Its name, an identifier as SQL reads it, valid until the next
 * statement; null for the default connection, or for none.
 */
TRL_API const char *trl_agent_current_name(const trl_agent *agent);

/**
 * @brief How many connections the agent holds, current and dormant.
 *
 * @param agent The agent.
 * @return How many; 0 of a null agent.
 */
TRL_API size_t trl_agent_count(const trl_agent *agent);

/**
 * @brief Why the agent's last statement failed.
 *
 * @param agent The agent.
 * @return Its failure, SQLCODE 0 if it did not; valid until the next.
 */
TRL_API const trl_failure *trl_agent_failure(const trl_agent *agent);

/**
 * @brief Ends every connection by trl_conn_terminate(), as the client
 * ends: every one ended, though one fails.
 *
 * @param agent The agent, with no connection after.
 * @return TRL_OK; TRL_FAILED with the agent's failure, the first;
 * TRL_INVALID_ARGUMENT of a null agent.
 */
TRL_API trl_status trl_agent_terminate(trl_agent *agent);

/**
 * @brief Frees the agent, each connection it holds closed by
 * trl_conn_free().
 *
 * @param agent The agent; null does nothing.
 */
TRL_API void trl_agent_free(trl_agent *agent);

#ifdef __cplusplus
}
#endif

#endif
