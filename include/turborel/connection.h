/**
 * @file connection.h
 * @brief Connections [CLIENT Connecting]: a session of the server's,
 * opened by a target, a user and a mechanism, or over a stream the
 * program opened; what it was granted and what the server tells it; and
 * how it ends, or its statement is cancelled from another thread.
 *
 * A connection is used by one thread at a time (RULES.md D11). A
 * canceller, made of it, is used by another, to cancel its statement.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_CONNECTION_H
#define TRL_CONNECTION_H

#include "turborel/base.h"
#include "turborel/connect.h"
#include "turborel/failure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A connection, and the session it holds. */
typedef struct trl_conn trl_conn;

/**
 * @brief Connects to a target as a user, proved by a mechanism.
 *
 * To a target over plain TCP, `password` and SCRAM (unless in the clear)
 * are refused before anything is sent (SQLSTATE 28000). A TLS target
 * binds SCRAM to its connection, as SCRAM-SHA-256-PLUS.
 *
 * @param target Where the server is.
 * @param user The user, as an identifier, as "ann" for ANN.
 * @param mechanism What proves the user; it keeps the exchange's state,
 * so it serves one login at a time.
 * @param options What to ask for and how long to wait; null for the
 * defaults.
 * @param out Where the connection goes. On TRL_FAILED it is made, lost,
 * holding the failure; free it either way.
 * @return TRL_OK; TRL_FAILED; TRL_INVALID_ARGUMENT if a pointer is null;
 * TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_connect(const trl_target *target, const char *user, trl_mechanism *mechanism,
                               const trl_conn_options *options, trl_conn **out);

/**
 * @brief A stream the program opened, such as a TLS stream of its own:
 * functions the connection reads and writes through.
 *
 * The connection waits as the stream's maker set it to; its timeouts do
 * not apply. The caller answers for the stream's privacy: no mechanism is
 * refused over it.
 */
typedef struct trl_stream {
    /** @brief Passed to each function. */
    void *context;
    /**
     * @brief Reads at most `capacity` bytes into `into`: how many, 0 at the
     * stream's end, or a negative number if it failed.
     */
    int64_t (*read)(void *context, uint8_t *into, size_t capacity);
    /**
     * @brief Writes up to `length` bytes of `bytes`: how many, at least 1,
     * or a negative number if it failed.
     */
    int64_t (*write)(void *context, const uint8_t *bytes, size_t length);
    /** @brief Closes the stream, once, as the connection is freed; may be null. */
    void (*close)(void *context);
} trl_stream;

/**
 * @brief Opens a connection over a stream the program opened, as a user,
 * proved by a mechanism; one over TLS is bound by trl_mechanism_bind()
 * first.
 *
 * @param stream The stream's functions, copied; the connection closes it
 * as it is freed, even if this fails.
 * @param user The user.
 * @param mechanism What proves the user.
 * @param options What to ask for; null for the defaults. Its timeouts do
 * not apply.
 * @param out Where the connection goes, as trl_connect() gives it.
 * @return As trl_connect() returns.
 */
TRL_API trl_status trl_connect_stream(const trl_stream *stream, const char *user, trl_mechanism *mechanism,
                                      const trl_conn_options *options, trl_conn **out);

/**
 * @brief The connection's last failure.
 *
 * @param conn The connection.
 * @return The failure, owned by the connection; null if `conn` is null.
 */
TRL_API const trl_failure *trl_conn_failure(const trl_conn *conn);

/**
 * @brief Whether the connection is lost: every call on it then fails as
 * it was lost.
 *
 * @param conn The connection; null gives true.
 * @return Whether it is.
 */
TRL_API bool trl_conn_is_lost(const trl_conn *conn);

/**
 * @brief The server's product, as Ready told it, as "turborel 0.0.0
 * (alpha)".
 *
 * @param conn The connection; null gives "".
 * @return A string the connection owns.
 */
TRL_API const char *trl_conn_server(const trl_conn *conn);

/**
 * @brief The session's authorization identifier, as Ready told it, as
 * "ANN".
 *
 * @param conn The connection; null gives "".
 * @return A string the connection owns.
 */
TRL_API const char *trl_conn_authorization(const trl_conn *conn);

/**
 * @brief Whether the server granted a capability.
 *
 * @param conn The connection.
 * @param capability Its name, as TRL_CAPABILITY_STATUS.
 * @return Whether it did; false if a pointer is null.
 */
TRL_API bool trl_conn_granted(const trl_conn *conn, const char *capability);

/**
 * @brief How many capabilities the server granted.
 *
 * @param conn The connection; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_conn_granted_count(const trl_conn *conn);

/**
 * @brief A capability granted, in the order Ready gave them.
 *
 * @param conn The connection.
 * @param index Which, from 0.
 * @return A string the connection owns; null past the last.
 */
TRL_API const char *trl_conn_granted_at(const trl_conn *conn, size_t index);

/**
 * @brief A session setting as the server last told it, with no round
 * trip, as "SESSION.TIME_ZONE" or "AUTHORIZATION"
 * [CLIENT What the server tells].
 *
 * @param conn The connection.
 * @param name The setting's name, as the server spells it.
 * @return Its value, a string the connection owns, empty for a null; null
 * if the server told none of that name.
 */
TRL_API const char *trl_conn_setting(const trl_conn *conn, const char *name);

/**
 * @brief How many settings the server told.
 *
 * @param conn The connection; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_conn_setting_count(const trl_conn *conn);

/**
 * @brief A setting the server told, by its place, in the order first told.
 *
 * @param conn The connection.
 * @param index Which, from 0.
 * @param name Where its name goes, a string the connection owns; may be
 * null.
 * @param value Where its value goes, a string the connection owns; may be
 * null.
 * @return Whether there is one at `index`.
 */
TRL_API bool trl_conn_setting_at(const trl_conn *conn, size_t index, const char **name, const char **value);

/**
 * @brief Changes the read timeout, for each read or write after.
 *
 * @param conn The connection.
 * @param read_ms The milliseconds, or TRL_NO_LIMIT.
 * @return TRL_OK; TRL_FAILED if it cannot be set, or of a connection over
 * a stream the program opened, which its maker bounds (SQLSTATE 08006);
 * TRL_INVALID_ARGUMENT if `conn` is null.
 */
TRL_API trl_status trl_conn_set_read_timeout(trl_conn *conn, uint32_t read_ms);

/**
 * @brief The read timeout, in milliseconds, or TRL_NO_LIMIT.
 *
 * @param conn The connection; null gives TRL_NO_LIMIT.
 * @return The timeout.
 */
TRL_API uint32_t trl_conn_read_timeout(const trl_conn *conn);

/**
 * @brief Takes replies of at most `bytes` bytes, a longer one losing the
 * connection; TRL_MAX_BODY_BYTES at first.
 *
 * @param conn The connection.
 * @param bytes The bytes, at most TRL_MAX_BODY_BYTES.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `conn` is null, or `bytes` is 0
 * or past TRL_MAX_BODY_BYTES.
 */
TRL_API trl_status trl_conn_set_max_body(trl_conn *conn, uint32_t bytes);

/**
 * @brief Asks whether the server and the connection are alive, with no
 * statement run: Ping, answered by Pong.
 *
 * @param conn The connection.
 * @return TRL_OK; TRL_FAILED, without sending anything if `ping` was not
 * granted (SQLSTATE 08006, the connection kept); TRL_INVALID_ARGUMENT if
 * `conn` is null.
 */
TRL_API trl_status trl_conn_ping(trl_conn *conn);

/**
 * @brief Ends the session, its transaction as the setting
 * SESSION.COMMIT_AT_END says, and waits for the server to close, at most
 * the read timeout, else TRL_TERMINATE_WITHIN_MS.
 *
 * The connection is lost after, and is still freed.
 *
 * @param conn The connection.
 * @return TRL_OK; TRL_FAILED with the failure of a commit that failed, as
 * of a deferred constraint (SQLSTATE 40002), or of the connection;
 * TRL_INVALID_ARGUMENT if `conn` is null.
 */
TRL_API trl_status trl_conn_terminate(trl_conn *conn);

/**
 * @brief Closes the connection, if open, without Terminate, and frees it.
 *
 * @param conn The connection; null does nothing.
 */
TRL_API void trl_conn_free(trl_conn *conn);

/**
 * @brief What cancels a connection's statement from another thread, by
 * the key its Ready gave, over a connection of its own.
 */
typedef struct trl_canceller trl_canceller;

/**
 * @brief Makes a connection's canceller, on the connection's thread.
 *
 * @param conn The connection, reached by a target.
 * @param out Where the canceller goes.
 * @return TRL_OK; TRL_FAILED, of the connection, if it was opened over a
 * stream the program opened, or never logged in (SQLCODE -910, SQLSTATE
 * TB910); TRL_INVALID_ARGUMENT if a pointer is null; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_canceller_new(trl_conn *conn, trl_canceller **out);

/**
 * @brief Cancels the statement the connection is running, giving up after
 * TRL_CANCEL_WITHIN_MS if the server does not answer. The statement fails
 * with SQLCODE -920 (SQLSTATE TB920).
 *
 * @param canceller The canceller, used on one thread at a time, which may
 * be another than the connection's.
 * @return TRL_OK; TRL_FAILED; TRL_INVALID_ARGUMENT if it is null.
 */
TRL_API trl_status trl_cancel(trl_canceller *canceller);

/**
 * @brief Cancels as trl_cancel() does, giving up after `within_ms`.
 *
 * @param canceller The canceller.
 * @param within_ms The milliseconds, or TRL_NO_LIMIT.
 * @return As trl_cancel() returns.
 */
TRL_API trl_status trl_cancel_within(trl_canceller *canceller, uint32_t within_ms);

/**
 * @brief The canceller's last failure.
 *
 * @param canceller The canceller.
 * @return The failure, owned by the canceller; null if it is null.
 */
TRL_API const trl_failure *trl_canceller_failure(const trl_canceller *canceller);

/**
 * @brief Frees a canceller, its key cleared from memory first.
 *
 * @param canceller The canceller; null does nothing.
 */
TRL_API void trl_canceller_free(trl_canceller *canceller);

#ifdef __cplusplus
}
#endif

#endif
