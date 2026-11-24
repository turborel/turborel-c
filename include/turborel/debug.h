/**
 * @file debug.h
 * @brief The routine debugger *(extension)* [CLIENT Debugging routines]:
 * with `debugger` granted, one connection, the debugger, attaches to the
 * session of another, the target, by that one's ID; sets breakpoints in
 * the procedures, functions and triggers its user may debug; and drives
 * the target's routines as they pause, step by step, reading their call
 * stack, parameters, variables and cursors. What it hears of the target,
 * an event, comes unasked between its requests, and is kept until
 * trl_debug_wait() gives it.
 *
 * Two connections: the target runs its statements as ever, often on
 * another thread; the debugger waits for its events beside it.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_DEBUG_H
#define TRL_DEBUG_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The kind of routine a breakpoint, or a frame, is of. */
typedef enum trl_debug_kind {
    /** @brief A procedure. */
    TRL_DEBUG_PROCEDURE = 0,
    /** @brief A function. */
    TRL_DEBUG_FUNCTION = 1,
    /** @brief A trigger. */
    TRL_DEBUG_TRIGGER = 2,
} trl_debug_kind;

/** @brief How a paused target goes on. */
typedef enum trl_debug_action {
    /** @brief To its next statement, in whatever frame: a step into. */
    TRL_DEBUG_INTO = 0,
    /** @brief To the next statement of this frame, or of one it returns to: a step over. */
    TRL_DEBUG_OVER = 1,
    /** @brief To the next statement of the frame this one returns to: a step out. */
    TRL_DEBUG_OUT = 2,
    /** @brief To a breakpoint. */
    TRL_DEBUG_CONTINUE = 3,
} trl_debug_action;

/** @brief What an event says of the target. */
typedef enum trl_debug_said {
    /** @brief It paused, at a breakpoint or a step's end: trl_debug_event_pause() gives the pause. */
    TRL_DEBUG_PAUSED = 0,
    /** @brief Paused past DEBUGGER.PAUSE_TIMEOUT_MS, it went on by itself. */
    TRL_DEBUG_TIMED_OUT = 1,
    /** @brief Its statement was cancelled while it was paused. */
    TRL_DEBUG_CANCELLED = 2,
    /** @brief Its session ended, and the debugger is detached. */
    TRL_DEBUG_ENDED = 3,
} trl_debug_said;

/** @brief Why the target paused. */
typedef enum trl_debug_reason {
    /** @brief At a breakpoint, trl_debug_pause_breakpoint() its number. */
    TRL_DEBUG_AT_BREAKPOINT = 0,
    /** @brief At the end of a step. */
    TRL_DEBUG_AT_STEP = 1,
} trl_debug_reason;

/** @brief Which of a frame's named values: its parameters, or its variables. */
typedef enum trl_debug_values {
    /** @brief Its parameters; a trigger's, its transition variables. */
    TRL_DEBUG_PARAMETERS = 0,
    /** @brief Its variables. */
    TRL_DEBUG_VARIABLES = 1,
} trl_debug_values;

/** @brief Where a breakpoint goes: a routine's kind and name, a line of its text, and a condition. */
typedef struct trl_debug_location {
    /** @brief The kind of routine. */
    trl_debug_kind kind;
    /** @brief Its schema; null or empty for the debugger's session's default schema. */
    const char *schema;
    /** @brief The routine's specific name, or its name if one routine of its kind has it, or the trigger's. */
    const char *name;
    /** @brief The line of its text, from 1. */
    uint32_t line;
    /** @brief A `<search condition>` over the frame's parameters and variables, paused only where true; null for none.
     */
    const char *condition;
} trl_debug_location;

/** @brief Breakpoints, as the server gives them; the caller's, freed by trl_debug_breakpoint_list_free(). */
typedef struct trl_debug_breakpoint_list trl_debug_breakpoint_list;

/** @brief An event of the target; the caller's, freed by trl_debug_event_free(). */
typedef struct trl_debug_event trl_debug_event;

/** @brief A pause, with the target's call stack; of trl_debug_stack() the caller's, freed by trl_debug_pause_free(). */
typedef struct trl_debug_pause trl_debug_pause;

/** @brief A frame of a pause's call stack, borrowed from its pause. */
typedef struct trl_debug_frame trl_debug_frame;

/**
 * @brief This connection's ID, as INFORMATION_SCHEMA.CONNECTIONS shows it:
 * what a debugger attaches to its session by.
 *
 * @param conn The connection.
 * @param out Where the ID goes.
 * @return TRL_OK; TRL_FAILED with the connection's failure, SQLSTATE
 * 08006 sending nothing if `debugger` was not granted;
 * TRL_INVALID_ARGUMENT of a null pointer.
 */
TRL_API trl_status trl_debug_id(trl_conn *conn, uint64_t *out);

/**
 * @brief Makes this connection the debugger of the session of the
 * connection `id` names: one of the same user, unless the user is the
 * database's owner or holds the role MONITOR.
 *
 * @param conn The debugger's connection.
 * @param id The target's ID, as trl_debug_id() gave it.
 * @return TRL_OK; TRL_FAILED, SQLSTATE 08003 of an ID of no connection the
 * user may see, TBD01 (SQLCODE -910) if attached already, the session
 * kept; TRL_INVALID_ARGUMENT of a null connection.
 */
TRL_API trl_status trl_debug_attach(trl_conn *conn, uint64_t id);

/**
 * @brief Ends the debugging: the breakpoints go, the target goes on if
 * paused, and the events kept are dropped.
 *
 * @param conn The debugger's connection.
 * @return TRL_OK; TRL_FAILED, TBD01 if attached to none;
 * TRL_INVALID_ARGUMENT of a null connection.
 */
TRL_API trl_status trl_debug_detach(trl_conn *conn);

/**
 * @brief Sets a breakpoint at a line of a routine's or a trigger's text.
 *
 * @param conn The debugger's connection.
 * @param at Where, and on what condition.
 * @param out Where the breakpoint set goes, a list of one, the caller's.
 * @return TRL_OK; TRL_FAILED, SQLSTATE 42000 (SQLCODE -104) of a routine
 * the user may not debug, TBD01 if attached to none; TRL_INVALID_ARGUMENT
 * of a null pointer or a kind of no code; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_debug_break(trl_conn *conn, const trl_debug_location *at, trl_debug_breakpoint_list **out);

/**
 * @brief Clears a breakpoint.
 *
 * @param conn The debugger's connection.
 * @param number The breakpoint's number.
 * @return TRL_OK; TRL_FAILED, TBD01 if attached to none;
 * TRL_INVALID_ARGUMENT of a null connection.
 */
TRL_API trl_status trl_debug_clear(trl_conn *conn, uint32_t number);

/**
 * @brief Every breakpoint set, in the order they were set.
 *
 * @param conn The debugger's connection.
 * @param out Where the list goes, the caller's.
 * @return As trl_debug_break() returns.
 */
TRL_API trl_status trl_debug_breakpoints(trl_conn *conn, trl_debug_breakpoint_list **out);

/**
 * @brief How many breakpoints a list holds.
 *
 * @param list The list; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_debug_breakpoint_count(const trl_debug_breakpoint_list *list);

/**
 * @brief A breakpoint's number.
 *
 * @param list The list.
 * @param index Which, from 0.
 * @return Its number; 0 past the last.
 */
TRL_API uint32_t trl_debug_breakpoint_number(const trl_debug_breakpoint_list *list, size_t index);

/**
 * @brief The schema of a breakpoint's routine.
 *
 * @param list The list.
 * @param index Which, from 0.
 * @return A string the list owns; null past the last.
 */
TRL_API const char *trl_debug_breakpoint_schema(const trl_debug_breakpoint_list *list, size_t index);

/**
 * @brief The specific name of a breakpoint's routine.
 *
 * @param list The list.
 * @param index Which, from 0.
 * @return A string the list owns; null past the last.
 */
TRL_API const char *trl_debug_breakpoint_name(const trl_debug_breakpoint_list *list, size_t index);

/**
 * @brief A breakpoint's line.
 *
 * @param list The list.
 * @param index Which, from 0.
 * @return Its line, from 1; 0 past the last.
 */
TRL_API uint32_t trl_debug_breakpoint_line(const trl_debug_breakpoint_list *list, size_t index);

/**
 * @brief A breakpoint's condition.
 *
 * @param list The list.
 * @param index Which, from 0.
 * @return A string the list owns; null of none, or past the last.
 */
TRL_API const char *trl_debug_breakpoint_condition(const trl_debug_breakpoint_list *list, size_t index);

/**
 * @brief Frees a list of breakpoints.
 *
 * @param list The list; null does nothing.
 */
TRL_API void trl_debug_breakpoint_list_free(trl_debug_breakpoint_list *list);

/**
 * @brief Tells the paused target how to go on.
 *
 * @param conn The debugger's connection.
 * @param how A step into, over or out, or on to a breakpoint.
 * @return TRL_OK; TRL_FAILED, TBD01 of a target not paused;
 * TRL_INVALID_ARGUMENT of a null connection or an action of no code.
 */
TRL_API trl_status trl_debug_resume(trl_conn *conn, trl_debug_action how);

/**
 * @brief Tells the running target to pause at its next statement.
 *
 * @param conn The debugger's connection.
 * @return TRL_OK; TRL_FAILED, TBD01 if attached to none;
 * TRL_INVALID_ARGUMENT of a null connection.
 */
TRL_API trl_status trl_debug_interrupt(trl_conn *conn);

/**
 * @brief The target's pause again, with its call stack.
 *
 * @param conn The debugger's connection.
 * @param out Where the pause goes, the caller's; null if the target runs.
 * @return As trl_debug_break() returns.
 */
TRL_API trl_status trl_debug_stack(trl_conn *conn, trl_debug_pause **out);

/**
 * @brief The next event of the target not yet given: one kept, as an
 * event is kept wherever a reply is read, else the next the server sends,
 * waited for at most `within_ms`. It is read between requests, never
 * while one is answered, and a Notice or a Status that comes instead is
 * kept as ever.
 *
 * @param conn The debugger's connection.
 * @param within_ms The milliseconds to wait, or TRL_NO_LIMIT.
 * @param out Where the event goes, the caller's; null if none came in time.
 * @return TRL_OK; TRL_FAILED, SQLSTATE 08006, of a time given a
 * connection over a stream the program opened, which its maker bounds, of
 * a reply where an event was waited for, or of the server gone;
 * TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_debug_wait(trl_conn *conn, uint32_t within_ms, trl_debug_event **out);

/**
 * @brief What an event says.
 *
 * @param event The event.
 * @return What it says; TRL_DEBUG_ENDED of a null event.
 */
TRL_API trl_debug_said trl_debug_event_said(const trl_debug_event *event);

/**
 * @brief A pause event's pause.
 *
 * @param event The event.
 * @return The pause, borrowed from the event; null of another event.
 */
TRL_API const trl_debug_pause *trl_debug_event_pause(const trl_debug_event *event);

/**
 * @brief Frees an event.
 *
 * @param event The event; null does nothing.
 */
TRL_API void trl_debug_event_free(trl_debug_event *event);

/**
 * @brief Why the target paused.
 *
 * @param pause The pause.
 * @return The reason; TRL_DEBUG_AT_STEP of a null pause.
 */
TRL_API trl_debug_reason trl_debug_pause_reason(const trl_debug_pause *pause);

/**
 * @brief The number of the breakpoint the target paused at.
 *
 * @param pause The pause.
 * @return The number; 0 of a pause at a step's end.
 */
TRL_API uint32_t trl_debug_pause_breakpoint(const trl_debug_pause *pause);

/**
 * @brief How many frames the call stack holds.
 *
 * @param pause The pause; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_debug_pause_frame_count(const trl_debug_pause *pause);

/**
 * @brief A frame of the call stack, the outermost first.
 *
 * @param pause The pause.
 * @param index Which, from 0.
 * @return The frame, borrowed from the pause; null past the last.
 */
TRL_API const trl_debug_frame *trl_debug_pause_frame(const trl_debug_pause *pause, size_t index);

/**
 * @brief Frees a pause trl_debug_stack() gave.
 *
 * @param pause The pause; null does nothing.
 */
TRL_API void trl_debug_pause_free(trl_debug_pause *pause);

/**
 * @brief Whether a frame has an origin, a routine or a trigger; one of a
 * statement run directly has none.
 *
 * @param frame The frame.
 * @return Whether it has.
 */
TRL_API bool trl_debug_frame_has_origin(const trl_debug_frame *frame);

/**
 * @brief The kind of a frame's routine.
 *
 * @param frame The frame.
 * @return The kind; TRL_DEBUG_PROCEDURE of a frame of no origin.
 */
TRL_API trl_debug_kind trl_debug_frame_kind(const trl_debug_frame *frame);

/**
 * @brief The specific schema of a frame's routine.
 *
 * @param frame The frame.
 * @return A string the pause owns; null of a frame of no origin.
 */
TRL_API const char *trl_debug_frame_specific_schema(const trl_debug_frame *frame);

/**
 * @brief The specific name of a frame's routine.
 *
 * @param frame The frame.
 * @return A string the pause owns; null of a frame of no origin.
 */
TRL_API const char *trl_debug_frame_specific_name(const trl_debug_frame *frame);

/**
 * @brief The schema of a frame's routine, as named.
 *
 * @param frame The frame.
 * @return A string the pause owns; null of a frame of no origin.
 */
TRL_API const char *trl_debug_frame_schema(const trl_debug_frame *frame);

/**
 * @brief The name of a frame's routine, or the trigger's.
 *
 * @param frame The frame.
 * @return A string the pause owns; null of a frame of no origin.
 */
TRL_API const char *trl_debug_frame_name(const trl_debug_frame *frame);

/**
 * @brief Whether the debugger may see into a frame; one it may not shows
 * its origin alone.
 *
 * @param frame The frame.
 * @return Whether it may.
 */
TRL_API bool trl_debug_frame_visible(const trl_debug_frame *frame);

/**
 * @brief A frame's place, in characters from the beginning of its
 * routine's text.
 *
 * @param frame The frame.
 * @param out Where the place goes; may be null.
 * @return Whether it has one.
 */
TRL_API bool trl_debug_frame_place(const trl_debug_frame *frame, uint64_t *out);

/**
 * @brief A frame's line and column, each from 1.
 *
 * @param frame The frame.
 * @param line Where the line goes; may be null.
 * @param column Where the column goes; may be null.
 * @return Whether it has them.
 */
TRL_API bool trl_debug_frame_line(const trl_debug_frame *frame, uint32_t *line, uint32_t *column);

/**
 * @brief How many parameters, or variables, a frame has.
 *
 * @param frame The frame; null gives 0.
 * @param which Its parameters, or its variables.
 * @return The count.
 */
TRL_API size_t trl_debug_frame_value_count(const trl_debug_frame *frame, trl_debug_values which);

/**
 * @brief A parameter's, or a variable's, name.
 *
 * @param frame The frame.
 * @param which Its parameters, or its variables.
 * @param index Which, from 0.
 * @return A string the pause owns; null past the last.
 */
TRL_API const char *trl_debug_frame_value_name(const trl_debug_frame *frame, trl_debug_values which, size_t index);

/**
 * @brief A parameter's, or a variable's, data type, as SQL writes it.
 *
 * @param frame The frame.
 * @param which Its parameters, or its variables.
 * @param index Which, from 0.
 * @return A string the pause owns; null past the last.
 */
TRL_API const char *trl_debug_frame_value_type(const trl_debug_frame *frame, trl_debug_values which, size_t index);

/**
 * @brief A parameter's, or a variable's, value.
 *
 * @param frame The frame.
 * @param which Its parameters, or its variables.
 * @param index Which, from 0.
 * @return The value, borrowed from the pause; null past the last.
 */
TRL_API const trl_value *trl_debug_frame_value(const trl_debug_frame *frame, trl_debug_values which, size_t index);

/**
 * @brief How many cursors a frame has.
 *
 * @param frame The frame; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_debug_frame_cursor_count(const trl_debug_frame *frame);

/**
 * @brief A cursor's name.
 *
 * @param frame The frame.
 * @param index Which, from 0.
 * @return A string the pause owns; null past the last.
 */
TRL_API const char *trl_debug_frame_cursor_name(const trl_debug_frame *frame, size_t index);

/**
 * @brief Whether a cursor is open.
 *
 * @param frame The frame.
 * @param index Which, from 0.
 * @return Whether it is; false past the last.
 */
TRL_API bool trl_debug_frame_cursor_open(const trl_debug_frame *frame, size_t index);

/**
 * @brief A cursor's query.
 *
 * @param frame The frame.
 * @param index Which, from 0.
 * @return A string the pause owns; null past the last.
 */
TRL_API const char *trl_debug_frame_cursor_query(const trl_debug_frame *frame, size_t index);

#ifdef __cplusplus
}
#endif

#endif
