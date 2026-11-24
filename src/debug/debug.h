/**
 * @file debug.h
 * @brief The routine debugger's own: what the server sent of breakpoints,
 * pauses and events, copied out of a reply into structures the program
 * owns, and the events a connection keeps until trl_debug_wait() gives
 * them.
 */
#ifndef TRL_DEBUG_DEBUG_H
#define TRL_DEBUG_DEBUG_H

#include "codec/arena.h"
#include "codec/debug.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief A parameter or a variable of a frame. */
typedef struct trl_debug_named {
    const char *name;
    const char *type;
    trl_value value;
} trl_debug_named;

/** @brief A cursor of a frame. */
typedef struct trl_debug_cursor {
    const char *name;
    bool open;
    const char *query;
} trl_debug_cursor;

/** @brief How many kinds of named values a frame has: parameters and variables. */
#define TRL_DEBUG_NAMED_KINDS 2U

/** @brief A frame, its strings and values in its pause's arena. */
struct trl_debug_frame {
    bool has_origin;
    trl_debug_kind kind;
    const char *specific_schema;
    const char *specific_name;
    const char *schema;
    const char *name;
    bool visible;
    bool has_place;
    uint64_t place;
    bool has_line;
    uint32_t line;
    uint32_t column;
    /** @brief Its parameters, then its variables, by trl_debug_values. */
    trl_debug_named *named[TRL_DEBUG_NAMED_KINDS];
    size_t named_count[TRL_DEBUG_NAMED_KINDS];
    trl_debug_cursor *cursors;
    size_t cursor_count;
};

/** @brief A pause, holding all it says in its own arena. */
struct trl_debug_pause {
    trl_arena arena;
    trl_debug_reason reason;
    uint32_t breakpoint;
    trl_debug_frame *frames;
    size_t frame_count;
};

/** @brief An event, a pause's with its pause; the next kept after it. */
struct trl_debug_event {
    trl_debug_said said;
    trl_debug_pause *pause;
    trl_debug_event *next;
};

/** @brief A breakpoint of a list. */
typedef struct trl_debug_breakpoint {
    uint32_t number;
    const char *schema;
    const char *name;
    uint32_t line;
    const char *condition;
} trl_debug_breakpoint;

/** @brief Breakpoints, their strings in the list's arena. */
struct trl_debug_breakpoint_list {
    trl_arena arena;
    trl_debug_breakpoint *items;
    size_t count;
};

/** @brief A pause of its own of what `wire` says; null if memory cannot be had. */
trl_debug_pause *trl_debug_pause_made(const trl_wire_pause *wire);

/** @brief An event of its own of what `wire` says; null if memory cannot be had. */
trl_debug_event *trl_debug_event_made(const trl_event *wire);

/** @brief A list of its own of `count` breakpoints; null if memory cannot be had. */
trl_debug_breakpoint_list *trl_debug_list_made(const trl_wire_breakpoint *items, uint32_t count);

/** @brief Keeps an event at the end of the connection's, to give in order. */
void trl_conn_event_keep(trl_conn *conn, trl_debug_event *event);

/** @brief The first event the connection keeps, taken from it; null if none. */
trl_debug_event *trl_conn_event_take(trl_conn *conn);

/** @brief Frees every event the connection keeps. */
void trl_conn_events_drop(trl_conn *conn);

#endif
