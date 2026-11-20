/**
 * @file session.h
 * @brief A connection's session [CLIENT Controlling the session]
 * [CLIENT What the server tells]: autocommit and the transaction, what
 * the last statement's completion told, notices, Reset, ChangeUser and
 * query attributes.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_SESSION_H
#define TRL_SESSION_H

#include "turborel/base.h"
#include "turborel/connect.h"
#include "turborel/connection.h"
#include "turborel/failure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Makes each statement trl_query(), trl_query_batches() or
 * trl_execute() runs its own transaction, committed as it succeeds or
 * rolled back as it fails, with no COMMIT sent; off by default, a
 * transaction lasting from its first statement until COMMIT or ROLLBACK
 * [CLIENT Running statements].
 *
 * @param conn The connection.
 * @param on Whether.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null connection.
 */
TRL_API trl_status trl_conn_set_autocommit(trl_conn *conn, bool on);

/**
 * @brief Whether autocommit is on.
 *
 * @param conn The connection; null gives false.
 * @return Whether it is.
 */
TRL_API bool trl_conn_autocommit(const trl_conn *conn);

/**
 * @brief Whether the session's transaction is active, as the server said
 * after the last statement.
 *
 * @param conn The connection; null gives false.
 * @return Whether it is.
 */
TRL_API bool trl_conn_in_transaction(const trl_conn *conn);

/**
 * @brief The SQLSTATE of the last statement's completion if it was a
 * warning, of class 01; else null.
 *
 * @param conn The connection.
 * @return The SQLSTATE, borrowed, or null.
 */
TRL_API const char *trl_conn_warning(const trl_conn *conn);

/**
 * @brief The rows the last statement changed, as GET DIAGNOSTICS ROW_COUNT
 * gives them, or a query's rows, with `row-counts`.
 *
 * @param conn The connection.
 * @param rows Where they go, if told; may be null.
 * @return Whether the last completion told them.
 */
TRL_API bool trl_conn_rows_affected(const trl_conn *conn, uint64_t *rows);

/**
 * @brief How many identity values the last INSERT generated, with
 * `generated-values`.
 *
 * @param conn The connection; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_conn_generated_count(const trl_conn *conn);

/**
 * @brief A generated identity value, as SQL writes it, in the order of the
 * INSERT's rows.
 *
 * @param conn The connection.
 * @param index Which, from 0.
 * @return It, borrowed; null past the last.
 */
TRL_API const char *trl_conn_generated_at(const trl_conn *conn, size_t index);

/** @brief The server's time for a statement, in microseconds. */
typedef struct trl_timing {
    /** @brief From when a worker began it until its completion was ready. */
    uint64_t total;
    /** @brief Its request's wait for a worker. */
    uint64_t queued;
    /** @brief Its wait for locks. */
    uint64_t locks;
} trl_timing;

/**
 * @brief The server's time for the last statement, with `row-counts`.
 *
 * @param conn The connection.
 * @param timing Where it goes, if told; may be null.
 * @return Whether the last completion told it.
 */
TRL_API bool trl_conn_server_time(const trl_conn *conn, trl_timing *timing);

/**
 * @brief The log position of the last commit a statement told, with
 * `commit-position`.
 *
 * @param conn The connection.
 * @param position Where it goes, if told; may be null.
 * @return Whether one was told.
 */
TRL_API bool trl_conn_commit_position(const trl_conn *conn, uint64_t *position);

/**
 * @brief How many conditions the last statement raised, with `conditions`.
 *
 * @param conn The connection; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_conn_condition_count(const trl_conn *conn);

/**
 * @brief A condition of the last statement: its SQLSTATE, SQLCODE and
 * message, as a failure.
 *
 * @param conn The connection.
 * @param index Which, from 0.
 * @return It, borrowed until the next statement; null past the last.
 */
TRL_API const trl_failure *trl_conn_condition_at(const trl_conn *conn, size_t index);

/**
 * @brief Given each Notice as it comes: its SQLSTATE, message and
 * diagnostics, as a failure of SQLCODE 0, borrowed for the call alone.
 */
typedef void (*trl_notice_handler)(void *context, const trl_failure *notice);

/**
 * @brief Gives each Notice to `handler` as it comes, on the thread of the
 * call that reads it; null keeps the last 64 instead, the oldest dropped.
 *
 * @param conn The connection.
 * @param handler The handler, or null.
 * @param context Passed to `handler`.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null connection.
 */
TRL_API trl_status trl_conn_set_notice_handler(trl_conn *conn, trl_notice_handler handler, void *context);

/**
 * @brief How many notices are kept.
 *
 * @param conn The connection; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_conn_notice_count(const trl_conn *conn);

/**
 * @brief A notice kept, the oldest first, as a failure of SQLCODE 0.
 *
 * @param conn The connection.
 * @param index Which, from 0.
 * @return It, borrowed until notices are cleared; null past the last.
 */
TRL_API const trl_failure *trl_conn_notice_at(const trl_conn *conn, size_t index);

/**
 * @brief Drops the notices kept, as a program does once it took them.
 *
 * @param conn The connection; null does nothing.
 */
TRL_API void trl_conn_clear_notices(trl_conn *conn);

/**
 * @brief Puts the session back as it was at login, with `reset`
 * [PROTOCOL Reset 'W']: its transaction rolled back, told by notice 01T01,
 * its prepared statements, cursors, temporary tables and settings gone,
 * its user kept.
 *
 * @param conn The connection.
 * @return TRL_OK; TRL_FAILED, sending nothing if `reset` was not granted;
 * TRL_INVALID_ARGUMENT.
 */
TRL_API trl_status trl_conn_reset(trl_conn *conn);

/**
 * @brief Authenticates the connection again as another user, whose new
 * session it then is, with `change-user` [PROTOCOL ChangeUser 'U']: the
 * session renewed as by Reset, under a new cancel key.
 *
 * @param conn The connection.
 * @param user The user.
 * @param mechanism What proves the user.
 * @return TRL_OK; TRL_FAILED, sending nothing if `change-user` was not
 * granted, or of a refusal, which ends the connection;
 * TRL_INVALID_ARGUMENT.
 */
TRL_API trl_status trl_conn_change_user(trl_conn *conn, const char *user, trl_mechanism *mechanism);

/**
 * @brief Sets attributes every later statement carries, read in SQL by
 * QUERY_ATTRIBUTE(name), until set again, with `query-attributes`; none
 * clears them. One named `traceparent` is written in the server's log of
 * a slow statement.
 *
 * @param conn The connection.
 * @param names The attributes' names, 1 to 64 bytes each; copied.
 * @param values Their values, at most 1,024 bytes each; copied.
 * @param count How many, at most 32.
 * @return TRL_OK; TRL_FAILED, of attributes asked for when
 * `query-attributes` was not granted, or past the protocol's bounds
 * (SQLSTATE 08006, the connection kept); TRL_INVALID_ARGUMENT;
 * TRL_NO_MEMORY.
 */
TRL_API trl_status trl_conn_set_query_attributes(trl_conn *conn, const char *const *names, const char *const *values,
                                                 size_t count);

#ifdef __cplusplus
}
#endif

#endif
