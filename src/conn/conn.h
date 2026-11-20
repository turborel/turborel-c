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
#include "codec/tail.h"
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

/** @brief The most notices kept with no handler, the oldest dropped first. */
#define TRL_KEPT_NOTICES 64U

/** @brief What the last statement's completion told, kept by the connection. */
typedef struct trl_told {
    /** @brief Whether a completion was told since login. */
    bool told;
    /** @brief Whether it told the rows its statement counted, and how many. */
    bool has_rows;
    /** @brief The rows. */
    uint64_t rows;
    /** @brief Whether it told the server's time, and the time. */
    bool has_time;
    /** @brief The time. */
    trl_timing time;
    /** @brief The identity values it told, each null-ended. */
    char **generated;
    /** @brief How many. */
    size_t generated_count;
    /** @brief The conditions it told. */
    trl_failure *conditions;
    /** @brief How many. */
    size_t condition_count;
} trl_told;

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
    /** @brief The failure that lost it. */
    trl_failure lost_by;
    /** @brief The target it was reached by; null over a program's stream. */
    trl_target *reach;
    /** @brief The session's authorization identifier, as Ready told it. */
    char *authorization;
    /** @brief The server's product, as Ready told it. */
    char *server;
    /** @brief The capabilities granted, as Ready gave them. */
    char **granted;
    /** @brief How many. */
    size_t granted_count;
    /** @brief The shortest frame sent compressed. */
    size_t compress_from;
    /** @brief The settings told, in the order first told. */
    trl_setting *settings;
    /** @brief How many. */
    size_t setting_count;
    /** @brief The last commit's log position told. */
    uint64_t position;
    /** @brief What the last completion told. */
    trl_told last;
    /** @brief The notices kept, the oldest first. */
    trl_failure notices[TRL_KEPT_NOTICES];
    /** @brief How many. */
    size_t notice_count;
    /** @brief The notice handler, or null. */
    trl_notice_handler handler;
    /** @brief Its context. */
    void *handler_context;
    /** @brief The query attributes every statement carries, if granted. */
    trl_attribute attributes[TRL_MAX_ATTRIBUTES];
    /** @brief How many. */
    size_t attribute_count;
    /** @brief The arena a reply's body is kept and decoded in, of a result
        that outlives the reply; null for the connection's own. */
    trl_arena *keep;
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
    /** @brief The read timeout. */
    uint32_t read_ms;
    /** @brief The longest reply taken. */
    uint32_t max_body;
    /** @brief The number the next statement prepared takes. */
    uint32_t next_statement;
    /** @brief Whether it is lost. */
    bool lost;
    /** @brief Whether Ready gave a key. */
    bool has_key;
    /** @brief The key that cancels its statements, a secret. */
    uint8_t key[TRL_CANCEL_KEY_BYTES];
    /** @brief Whether frames are compressed, `compression` granted. */
    bool compressed;
    /** @brief Whether the session's transaction is active, as last told. */
    bool active;
    /** @brief Whether each statement is its own transaction. */
    bool autocommit;
    /** @brief The last completion's SQLSTATE if a warning, else empty. */
    char warning[TRL_SQLSTATE_BYTES];
    /** @brief Whether a commit's log position was told. */
    bool has_position;
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

/**
 * @brief Receives a reply as trl_conn_receive() does, of a request
 * answered by it alone, as a Describe of a cursor is: one holding a value
 * the client cannot take is not read past.
 */
bool trl_conn_receive_one(trl_conn *conn, trl_reply *out);

/** @brief Keeps a setting the server told. */
bool trl_conn_tell(trl_conn *conn, trl_slice name, trl_slice value);

/** @brief Reads a statement's completion: Complete, or Error as its failure. */
bool trl_conn_completion(trl_conn *conn);

/** @brief Keeps a Notice, or hands it to the handler. */
void trl_conn_notice(trl_conn *conn, const trl_reply *notice);

/** @brief Keeps what a completion told. */
void trl_conn_completed(trl_conn *conn, const trl_completed *completed);

/** @brief Frees what the connection keeps of completions, notices and attributes. */
void trl_conn_free_told(trl_conn *conn);

/** @brief Fails, sending nothing, unless `ok` or the server granted `capability`. */
bool trl_conn_needs(trl_conn *conn, bool ok, const char *capability);

/** @brief Fails as the server's Error says, losing the connection if it ends it. */
void trl_conn_failed_by(trl_conn *conn, const trl_wire_failure *wire);

/** @brief Fails as the codec failed, the connection kept. */
bool trl_conn_codec_failed(trl_conn *conn, const trl_codec_error *error);

/** @brief The query attributes a statement carries: none unless granted; their count. */
size_t trl_conn_query_attributes(const trl_conn *conn, const trl_attribute **out);

#endif
