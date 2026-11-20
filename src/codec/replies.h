/**
 * @file replies.h
 * @brief Every reply a client receives, decoded from its kind and body
 * [PROTOCOL Replies]: Challenge to Pong. A body that is not its kind's
 * fails the connection (SQLCODE -919, SQLSTATE 08006).
 *
 * What a reply holds is views into its body and arrays from an arena;
 * both outlive the reply.
 */
#ifndef TRL_CODEC_REPLIES_H
#define TRL_CODEC_REPLIES_H

#include "codec/arena.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The times of a Complete's tag 18: total, queued and locks. */
#define TRL_COMPLETE_TIMES 3U
/** @brief Which of them is the total. */
#define TRL_TIME_TOTAL 0U
/** @brief Which is the time queued for a worker. */
#define TRL_TIME_QUEUED 1U
/** @brief Which is the time waiting for locks. */
#define TRL_TIME_LOCKS 2U

/** @brief A row of a Rows reply: its values. */
typedef struct trl_row {
    /** @brief The values. */
    trl_value *values;
    /** @brief How many. */
    uint32_t count;
} trl_row;

/** @brief A Complete's fields and tail [PROTOCOL Complete 'K']. */
typedef struct trl_completed {
    /** @brief Its SQLCODE, 0 or 100. */
    int32_t sqlcode;
    /** @brief Its SQLSTATE. */
    trl_slice sqlstate;
    /** @brief Whether a transaction is active after it. */
    bool active;
    /** @brief Whether tag 4, the commit's log position, was told. */
    bool has_position;
    /** @brief The position. */
    uint64_t position;
    /** @brief Tag 5: another statement's replies follow. */
    bool more;
    /** @brief Whether tag 6, the rows, was told. */
    bool has_rows;
    /** @brief The rows its statement counted. */
    uint64_t rows;
    /** @brief Tag 16: the identity values generated. */
    trl_strings generated;
    /** @brief Tag 17: the conditions raised. */
    trl_wire_failure *conditions;
    /** @brief How many. */
    uint32_t condition_count;
    /** @brief Whether tag 18, the server's time, was told. */
    bool has_time;
    /** @brief Total, queued and locks, in microseconds. */
    uint64_t time[TRL_COMPLETE_TIMES];
} trl_completed;

/** @brief A reply, by its kind (`kinds.h`). */
typedef struct trl_reply {
    /** @brief Its kind. */
    uint8_t kind;
    /** @brief What it holds. */
    union {
        /** @brief Challenge: the next step's bytes. */
        trl_slice challenge;
        /** @brief Ready. */
        struct {
            trl_slice authorization;
            trl_slice key;
            trl_strings capabilities;
            trl_slice server;
        } ready;
        /** @brief Prepared. */
        struct {
            uint32_t statement;
            trl_parameter *parameters;
            uint32_t parameter_count;
            trl_column *columns;
            uint32_t column_count;
        } prepared;
        /** @brief Description: the columns. */
        struct {
            trl_column *columns;
            uint32_t count;
        } description;
        /** @brief Rows: a batch of rows. */
        struct {
            trl_row *rows;
            uint32_t count;
        } rows;
        /** @brief Complete. */
        trl_completed complete;
        /** @brief Error: the failure. */
        trl_wire_failure error;
        /** @brief A Lob reply: data or a locator. */
        struct {
            uint8_t what;
            trl_slice data;
            uint32_t locator;
        } lob;
        /** @brief Notice. */
        struct {
            trl_slice sqlstate;
            trl_slice message;
            trl_diagnostics diagnostics;
        } notice;
        /** @brief Status: a setting's name and value. */
        struct {
            trl_slice name;
            trl_slice value;
        } status;
    } as;
} trl_reply;

/**
 * @brief Decodes the reply of kind `kind` whose body is `body`.
 * @param kind The frame's kind.
 * @param body The body.
 * @param arena Where what the reply holds is allocated.
 * @param out The reply.
 * @param error Where a failure is recorded: a kind no reply has, or a body
 * not of its kind, is a connection failure; memory not had, -918.
 * @return Nonzero if decoded.
 */
bool trl_decode_reply(uint8_t kind, trl_slice body, trl_arena *arena, trl_reply *out, trl_codec_error *error);

#endif
