/**
 * @file pool.h
 * @brief Pools of connections [CLIENT Pools]: a bounded set a program's
 * threads share. A pool hands out an idle connection that answers, or
 * makes one while fewer than its most are made, or waits for one to come
 * back. A connection comes back as it is released, reset to the state it
 * logged in with; one lost, or whose reset fails, is closed instead, so
 * no borrower sees another's session. The pool starts no thread; each
 * wait is the waiting thread's own.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_POOL_H
#define TRL_POOL_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The connections a pool makes at most, and keeps idle at most, by default. */
#define TRL_POOL_MAX 10
/** @brief How long a connection may stay idle by default, in milliseconds: ten minutes. */
#define TRL_POOL_IDLE_TIMEOUT_MS 600000U
/** @brief How long a connection lives by default, in milliseconds: an hour. */
#define TRL_POOL_MAX_LIFETIME_MS 3600000U
/** @brief How long getting a connection waits for one to come back by default, in milliseconds. */
#define TRL_POOL_WAIT_MS 30000U
/** @brief The most statements a borrower may leave prepared for its connection to be reset rather than closed. */
#define TRL_POOL_RESET_MOST_STATEMENTS 256U

/** @brief How a pool keeps its connections. */
typedef struct trl_pool_config {
    /** @brief The most connections made at once, handed out and idle. */
    size_t max;
    /** @brief The most kept idle. */
    size_t max_idle;
    /** @brief How long one may stay idle, in milliseconds; 0, as long as it lives. */
    uint32_t idle_timeout_ms;
    /** @brief How long one lives from when it was made, in milliseconds; 0, without limit. */
    uint32_t max_lifetime_ms;
    /** @brief How long getting one waits for one to come back, in milliseconds. */
    uint32_t wait_ms;
    /** @brief Whether an idle connection is checked to answer before it is handed out. */
    bool check;
    /** @brief Statements run, after the pool's own, as each connection comes back; may be null of none. */
    const char *const *reset;
    /** @brief How many. */
    size_t reset_count;
} trl_pool_config;

/**
 * @brief Makes a new connection for a pool, logged in.
 *
 * @param context The program's.
 * @param out Where the connection goes, made on TRL_FAILED too, holding why.
 * @return TRL_OK, or why none was made.
 */
typedef trl_status (*trl_connector)(void *context, trl_conn **out);

/** @brief A pool of connections. */
typedef struct trl_pool trl_pool;

/**
 * @brief A pool's defaults: TRL_POOL_MAX made and idle, ten minutes idle,
 * an hour's life, 30 seconds' wait, checked, no statements of the
 * program's.
 *
 * @return The configuration.
 */
TRL_API trl_pool_config trl_pool_config_default(void);

/**
 * @brief Makes a pool of the connections `connect` makes, kept as
 * `config` says.
 *
 * @param connect What makes a connection; it may be called from any
 * thread that gets one, as several may at once.
 * @param context Its context.
 * @param config How, copied; null for the defaults.
 * @param out Where the pool goes, the caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer, or a `max` of
 * 0; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_pool_new(trl_connector connect, void *context, const trl_pool_config *config, trl_pool **out);

/**
 * @brief Makes a pool of the connections `settings` make, which it uses
 * until freed.
 *
 * @param settings The settings, which must last as long as the pool.
 * @param config How, copied; null for the defaults.
 * @param out Where the pool goes, the caller's to free.
 * @return As trl_pool_new() returns.
 */
TRL_API trl_status trl_pool_of_settings(const trl_settings *settings, const trl_pool_config *config, trl_pool **out);

/**
 * @brief Another hold of the pool, the same pool, for another part of
 * the program; each hold is freed by trl_pool_free().
 *
 * @param pool The pool.
 * @return The pool; null of a null pool.
 */
TRL_API trl_pool *trl_pool_hold(trl_pool *pool);

/**
 * @brief Gets a connection: an idle one that answers, else a new one
 * while the pool has room, else the first to come back within `wait_ms`;
 * one idle past `idle_timeout_ms`, past its life, or not answering its
 * check closed on the way.
 *
 * @param pool The pool.
 * @param out Where the connection goes, the caller's to give back by
 * trl_pool_release(); on TRL_FAILED, a connection holding why, the
 * caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED:
 * SQLSTATE 08001 if none came back in time, or the failure of making
 * one; TRL_NO_MEMORY, nothing made.
 */
TRL_API trl_status trl_pool_get(trl_pool *pool, trl_conn **out);

/**
 * @brief Gives a connection back: reset, by Reset where the server grants
 * it, autocommit off, then the program's statements; else ROLLBACK, each
 * statement it prepared closed and destroyed, its user, schema and zone
 * those of its login, and the program's statements, in one pipeline. One
 * lost, whose reset fails, that has lived its time, or beyond `max_idle`
 * is closed instead.
 *
 * @param pool The pool it came from.
 * @param conn The connection; the pool's again.
 */
TRL_API void trl_pool_release(trl_pool *pool, trl_conn *conn);

/**
 * @brief How many connections the pool has made, handed out and idle,
 * and how many are idle.
 *
 * @param pool The pool.
 * @param made Where the first goes; may be null.
 * @param idle Where the second goes; may be null.
 */
TRL_API void trl_pool_counts(trl_pool *pool, size_t *made, size_t *idle);

/**
 * @brief Frees a hold of the pool; the last frees it, closing each idle
 * connection. Connections handed out must be given back first.
 *
 * @param pool The pool; null does nothing.
 */
TRL_API void trl_pool_free(trl_pool *pool);

#ifdef __cplusplus
}
#endif

#endif
