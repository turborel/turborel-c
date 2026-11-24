/**
 * @file debug.h
 * @brief The routine debugger's frames, with `debugger` *(extension)*:
 * the request Debug 'V' encoded, and the replies Debugged 'V' and Event
 * 'W' decoded [PROTOCOL Debug 'V'] [PROTOCOL Debugged 'V']
 * [PROTOCOL Event 'W'].
 */
#ifndef TRL_CODEC_DEBUG_H
#define TRL_CODEC_DEBUG_H

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/reader.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief What a Debug asks, its first byte. */
#define TRL_DEBUG_IDENTIFY 0U
#define TRL_DEBUG_ATTACH 1U
#define TRL_DEBUG_DETACH 2U
#define TRL_DEBUG_BREAK 3U
#define TRL_DEBUG_CLEAR 4U
#define TRL_DEBUG_BREAKPOINTS 5U
#define TRL_DEBUG_RESUME 6U
#define TRL_DEBUG_INTERRUPT 7U
#define TRL_DEBUG_STACK 8U

/** @brief What a Debugged gives, its first byte. */
#define TRL_DEBUGGED_CONNECTION 0U
#define TRL_DEBUGGED_BREAKPOINT 1U
#define TRL_DEBUGGED_BREAKPOINTS 2U
#define TRL_DEBUGGED_STACK 3U

/** @brief What an Event says, its first byte. */
#define TRL_EVENT_PAUSED 0U
#define TRL_EVENT_TIMED_OUT 1U
#define TRL_EVENT_CANCELLED 2U
#define TRL_EVENT_ENDED 3U

/** @brief The greatest code of a breakpoint's kind, a resume's action, and a pause's reason. */
#define TRL_DEBUG_KIND_MOST 2U
#define TRL_DEBUG_ACTION_MOST 3U
#define TRL_DEBUG_REASON_MOST 1U

/** @brief A Debug request: what it asks, and the fields of that. */
typedef struct trl_debug_request {
    /** @brief TRL_DEBUG_IDENTIFY to TRL_DEBUG_STACK. */
    uint8_t what;
    /** @brief Attach's connection. */
    uint64_t connection;
    /** @brief Break's kind, 0 a procedure, 1 a function, 2 a trigger. */
    uint8_t kind;
    /** @brief Break's schema, empty for the default, and name. */
    trl_slice schema;
    trl_slice name;
    /** @brief Break's line, from 1. */
    uint32_t line;
    /** @brief Whether Break has a condition, and it. */
    bool conditioned;
    trl_slice condition;
    /** @brief Clear's breakpoint. */
    uint32_t number;
    /** @brief Resume's action, 0 into, 1 over, 2 out, 3 continue. */
    uint8_t action;
} trl_debug_request;

/** @brief A breakpoint, as Debugged gives it. */
typedef struct trl_wire_breakpoint {
    uint32_t number;
    trl_slice schema;
    trl_slice name;
    uint32_t line;
    bool conditioned;
    trl_slice condition;
} trl_wire_breakpoint;

/** @brief A parameter or a variable of a frame: its name, its type as SQL writes it, and its value. */
typedef struct trl_wire_named {
    trl_slice name;
    trl_slice type;
    trl_value value;
} trl_wire_named;

/** @brief A cursor of a frame: its name, whether open, and its query. */
typedef struct trl_wire_cursor {
    trl_slice name;
    bool open;
    trl_slice query;
} trl_wire_cursor;

/** @brief A frame of a paused call stack. */
typedef struct trl_wire_frame {
    bool has_origin;
    uint8_t kind;
    trl_slice specific_schema;
    trl_slice specific_name;
    trl_slice schema;
    trl_slice name;
    bool visible;
    bool has_place;
    uint64_t place;
    bool has_line;
    uint32_t line;
    uint32_t column;
    trl_wire_named *parameters;
    uint32_t parameter_count;
    trl_wire_named *variables;
    uint32_t variable_count;
    trl_wire_cursor *cursors;
    uint32_t cursor_count;
} trl_wire_frame;

/** @brief A pause: why, and the call stack, the outermost frame first. */
typedef struct trl_wire_pause {
    /** @brief 0 a breakpoint, 1 a step. */
    uint8_t reason;
    /** @brief The breakpoint's number, of a breakpoint. */
    uint32_t breakpoint;
    trl_wire_frame *frames;
    uint32_t frame_count;
} trl_wire_pause;

/** @brief Debugged: what it gives, and that. */
typedef struct trl_debugged {
    uint8_t what;
    uint64_t connection;
    trl_wire_breakpoint breakpoint;
    trl_wire_breakpoint *breakpoints;
    uint32_t breakpoint_count;
    bool paused;
    trl_wire_pause pause;
} trl_debugged;

/** @brief Event: what it says, and the pause of a pause. */
typedef struct trl_event {
    uint8_t what;
    trl_wire_pause pause;
} trl_event;

/**
 * @brief Appends a Debug request to `buffer`. False, the buffer as it was,
 * if a field is past its codes or a string past its bounds.
 */
bool trl_encode_debug(trl_buffer *buffer, const trl_debug_request *request, trl_codec_error *error);

/** @brief Reads a Debugged body; a code of no kind fails the read. */
bool trl_read_debugged(trl_reader *reader, trl_debugged *out);

/** @brief Reads an Event body; a code of no kind fails the read. */
bool trl_read_event(trl_reader *reader, trl_event *out);

#endif
