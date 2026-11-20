/**
 * @file conn.h
 * @brief A connection's state, and how its requests are sent and its
 * replies received [PROTOCOL Conversations].
 */
#ifndef TRL_CONN_CONN_H
#define TRL_CONN_CONN_H

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/replies.h"
#include "failure.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The bytes a connection reads at a time. */
#define TRL_READ_BUFFER_BYTES 16384U
/** @brief The most bytes held for a reply before its bytes arrive. */
#define TRL_RESERVED_BYTES 65536U
/** @brief The bytes of a cancel key. */
#define TRL_CANCEL_KEY_BYTES 16U

/** @brief A setting the server told: its name and value. */
typedef struct trl_setting {
    /** @brief Its name. */
    char *name;
    /** @brief Its value, empty for a null. */
    char *value;
} trl_setting;

/** @brief A connection. */
struct trl_conn {
    /** @brief What it reads and writes through. */
    trl_transport transport;
    /** @brief Its last failure. */
    trl_failure failure;
    /** @brief Whether it is lost, and the failure that lost it. */
    bool lost;
    /** @brief The failure that lost it. */
    trl_failure lost_by;
    /** @brief The target it was reached by; null over a program's stream. */
    trl_target *reach;
    /** @brief The session's authorization identifier, as Ready told it. */
    char *authorization;
    /** @brief The server's product, as Ready told it. */
    char *server;
    /** @brief Whether Ready gave a key. */
    bool has_key;
    /** @brief The key that cancels its statements, a secret. */
    uint8_t key[TRL_CANCEL_KEY_BYTES];
    /** @brief The capabilities granted, as Ready gave them. */
    char **granted;
    /** @brief How many. */
    size_t granted_count;
    /** @brief Whether frames are compressed, `compression` granted. */
    bool compressed;
    /** @brief The shortest frame sent compressed. */
    size_t compress_from;
    /** @brief The settings told, in the order first told. */
    trl_setting *settings;
    /** @brief How many. */
    size_t setting_count;
    /** @brief The read timeout. */
    uint32_t read_ms;
    /** @brief The longest reply taken. */
    uint32_t max_body;
    /** @brief Whether the session's transaction is active, as last told. */
    bool active;
    /** @brief The bytes read and not yet taken. */
    uint8_t *input;
    /** @brief Where those not taken begin. */
    size_t input_at;
    /** @brief Where they end. */
    size_t input_end;
    /** @brief The last reply's body. */
    trl_buffer body;
    /** @brief A compressed reply's inflated body. */
    trl_buffer inflated;
    /** @brief What the last reply decoded to holds. */
    trl_arena arena;
    /** @brief The requests being sent. */
    trl_buffer out;
    /** @brief The requests compressed. */
    trl_buffer compressed_out;
};

/** @brief Loses the connection by `failure`, kept for every call after. */
void trl_conn_lose(trl_conn *conn, const trl_failure *failure);

/** @brief Loses the connection by a failure of `message` (SQLSTATE 08006). */
void trl_conn_lose_by(trl_conn *conn, const char *message);

/** @brief Sets the connection's failure, losing it if the failure ends it. */
void trl_conn_failed(trl_conn *conn, const trl_failure *failure);

/**
 * @brief Sends the requests encoded in `conn->out`, compressed if granted,
 * and empties it; a connection lost fails with what lost it.
 */
bool trl_conn_send(trl_conn *conn);

/**
 * @brief Receives the next reply but Notices and Statuses, which it keeps;
 * `out` holds views into the connection, valid until the next receive.
 */
bool trl_conn_receive(trl_conn *conn, trl_reply *out);

/** @brief Keeps a setting the server told. */
bool trl_conn_tell(trl_conn *conn, trl_slice name, trl_slice value);

#endif
