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
#include "codec/debug.h"
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
            /** @brief Whether it told tail 20, of a CALL of an SQL-invoked procedure. */
            bool called;
            /** @brief The procedure's specific schema and name, of tail 20. */
            trl_slice procedure_schema;
            trl_slice procedure_name;
            /** @brief Every `?`, OUT ones too, in the order written, of tail 20. */
            trl_marker *markers;
            uint32_t marker_count;
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
        /** @brief Debugged, with `debugger`. */
        trl_debugged debugged;
        /** @brief Event, with `debugger`. */
        trl_event event;
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

/**
 * @brief The head of a Complete that may not decode whole: its SQLCODE,
 * SQLSTATE, whether a transaction is active, and of its tail the commit's
 * position and whether more replies follow, which come before any field a
 * client may fail to take [PROTOCOL Complete 'K'].
 * @param kind The reply's kind.
 * @param body Its body.
 * @param arena Where its strings go.
 * @param out Where the head goes.
 * @return Whether it is a Complete whose head reads.
 */
bool trl_completed_head(uint8_t kind, trl_slice body, trl_arena *arena, trl_completed *out);

/**
 * @brief Whether a reply, decoded or not, is the last its request is
 * answered with, by the order of a statement's replies: a Description,
 * Rows, a Notice or a Status has more after it; a Complete has if its tail
 * says more (tag 5); any other reply ends its request's [PROTOCOL
 * Conversations].
 * @param kind The reply's kind.
 * @param body Its body.
 * @param arena Where a Complete's head is read into.
 * @return Whether it ends its request's replies.
 */
bool trl_reply_ends_request(uint8_t kind, trl_slice body, trl_arena *arena);

#endif
