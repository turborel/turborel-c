# Debugging Routines

[Contents](README.md)

This page describes how the driver gives turborel-client's routine
debugger *(extension)*: one connection, the debugger, drives the
procedures, functions and triggers another connection's session runs,
as Turborel's [Debugging Routines](https://github.com/turborel/turborel/blob/main/doc/user/11-debugging.md)
describes.

## The debugger

### NAME

`trl_debug_id`, `trl_debug_attach`, `trl_debug_detach`,
`trl_debug_break`, `trl_debug_clear`, `trl_debug_breakpoints`,
`trl_debug_resume`, `trl_debug_interrupt`, `trl_debug_stack`,
`trl_debug_wait`: a session's routines debugged from another connection.

### SYNOPSIS

```c
#include <turborel/turborel.h>

typedef enum trl_debug_kind { TRL_DEBUG_PROCEDURE, TRL_DEBUG_FUNCTION, TRL_DEBUG_TRIGGER } trl_debug_kind;
typedef enum trl_debug_action { TRL_DEBUG_INTO, TRL_DEBUG_OVER, TRL_DEBUG_OUT, TRL_DEBUG_CONTINUE } trl_debug_action;
typedef struct trl_debug_location {
    trl_debug_kind kind;
    const char *schema;
    const char *name;
    uint32_t line;
    const char *condition;
} trl_debug_location;

trl_status trl_debug_id(trl_conn *conn, uint64_t *out);
trl_status trl_debug_attach(trl_conn *conn, uint64_t id);
trl_status trl_debug_detach(trl_conn *conn);
trl_status trl_debug_break(trl_conn *conn, const trl_debug_location *at, trl_debug_breakpoint_list **out);
trl_status trl_debug_clear(trl_conn *conn, uint32_t number);
trl_status trl_debug_breakpoints(trl_conn *conn, trl_debug_breakpoint_list **out);
trl_status trl_debug_resume(trl_conn *conn, trl_debug_action how);
trl_status trl_debug_interrupt(trl_conn *conn);
trl_status trl_debug_stack(trl_conn *conn, trl_debug_pause **out);
trl_status trl_debug_wait(trl_conn *conn, uint32_t within_ms, trl_debug_event **out);
```

### DESCRIPTION

A connection asks for `debugger`, as it asks for every capability but
compression [CLIENT Debugging routines]. `trl_debug_id` gives a
connection's ID, as INFORMATION_SCHEMA.CONNECTIONS shows it, and
`trl_debug_attach` makes another connection the debugger of that
connection's session, the target. It may attach to a connection of its
own user; the database's owner, and a user granted the role MONITOR, to
any. `trl_debug_detach` ends the debugging: the breakpoints go, a paused
target goes on, and the events kept are dropped.

`trl_debug_break` sets a breakpoint at a line, from 1, of the text of a
procedure, a function or a trigger, named by its specific name, or its
name where one routine of its kind has it, in `at->schema`, or the
debugger's default schema where that is null. Where `at->condition` is
given, a `<search condition>` over the frame's parameters and
variables, the target pauses there only where it is true. It gives the
breakpoint set, a list of one, the routine named by its specific name.
`trl_debug_clear` clears one, and `trl_debug_breakpoints` lists them,
in the order they were set. The debugger sees into, and pauses in, only
the routines its user owns or holds DEBUG on.

What the debugger hears of its target comes unasked, between its
requests: an event. The driver reads one wherever it reads a reply, and
keeps it, the oldest first, without ending the request it was read
among [PROTOCOL Event 'W']. `trl_debug_wait` gives the next event: one
kept, else the next the server sends, waited for at most `within_ms`,
or without limit for TRL_NO_LIMIT; none if none came in time, the
connection kept. A connection over a stream the program opened, which
its maker bounds, waits only without limit.

An event says the target paused, at a breakpoint or a step's end, with
its call stack; that, paused past DEBUGGER.PAUSE_TIMEOUT_MS, it went on
by itself; that its statement was cancelled while it was paused; or
that its session ended, the debugger then detached. `trl_debug_stack`
gives the pause again, or none if the target runs.
`trl_debug_resume` lets a paused target go on: into its next statement,
in whatever frame; over, to the next of this frame or the one it
returns to; out, to the next of the frame this one returns to; or on,
to a breakpoint. `trl_debug_interrupt` pauses a running target at its
next statement.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer, a kind or an action of no
code. `TRL_FAILED`, the connection's failure, the session kept:
SQLSTATE 08003 of an ID of no connection the user may see; 42000
(SQLCODE -104) of a breakpoint in a routine the user may not debug;
TBD01 (SQLCODE -910) of an action the target's state does not allow, as
a resume of a target not paused, a second attach, or any request but an
ID or an attach of a connection attached to none. Without `debugger`
granted, SQLSTATE 08006, nothing sent. `TRL_NO_MEMORY` of a list, a
pause or an event that memory could not be had for.

### EXAMPLES

A second connection attaches to `conn`'s session and sets a breakpoint
of a condition; nothing runs, so no event comes:

```c
trl_result *result = NULL;
trl_query(conn, "CREATE PROCEDURE RAISE_PAY(IN PCT INTEGER) MODIFIES SQL DATA\n"
                "BEGIN\n"
                "  DECLARE N INTEGER DEFAULT 0;\n"
                "  SET N = PCT * 2;\n"
                "END", &result);
trl_result_free(result);
trl_query(conn, "COMMIT", &result);
trl_result_free(result);
trl_conn *debugger = NULL;
uint64_t id = 0;
if (trl_connect_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable", &debugger) == TRL_OK &&
    trl_debug_id(conn, &id) == TRL_OK && trl_debug_attach(debugger, id) == TRL_OK) {
    trl_debug_location at = {TRL_DEBUG_PROCEDURE, NULL, "RAISE_PAY", 4, "PCT > 5"};
    trl_debug_breakpoint_list *set = NULL;
    if (trl_debug_break(debugger, &at, &set) == TRL_OK) {
        printf("breakpoint %u at %s.%s line %u if %s\n", (unsigned)trl_debug_breakpoint_number(set, 0),
               trl_debug_breakpoint_schema(set, 0), trl_debug_breakpoint_name(set, 0),
               (unsigned)trl_debug_breakpoint_line(set, 0), trl_debug_breakpoint_condition(set, 0));
    }
    trl_debug_breakpoint_list_free(set);
    trl_debug_event *event = NULL;
    trl_debug_wait(debugger, 100, &event);
    printf("%s\n", event == NULL ? "no event within 100 ms" : "an event");
    trl_debug_event_free(event);
    trl_debug_detach(debugger);
} else {
    print_failure(debugger);
}
trl_conn_free(debugger);
```

```
breakpoint 1 at ANN.RAISE_PAY_1 line 4 if PCT > 5
no event within 100 ms
```

A target's CALL pauses on the thread that runs it; the debugger waits
for the event on its own, reads the frames, and resumes the target,
as the driver's live tests do.

## Pauses, frames and breakpoints

### NAME

`trl_debug_event_said`, `trl_debug_event_pause`, `trl_debug_event_free`,
`trl_debug_pause_reason`, `trl_debug_pause_breakpoint`,
`trl_debug_pause_frame_count`, `trl_debug_pause_frame`,
`trl_debug_pause_free`, `trl_debug_frame_has_origin`,
`trl_debug_frame_kind`, `trl_debug_frame_specific_schema`,
`trl_debug_frame_specific_name`, `trl_debug_frame_schema`,
`trl_debug_frame_name`, `trl_debug_frame_visible`,
`trl_debug_frame_place`, `trl_debug_frame_line`,
`trl_debug_frame_value_count`, `trl_debug_frame_value_name`,
`trl_debug_frame_value_type`, `trl_debug_frame_value`,
`trl_debug_frame_cursor_count`, `trl_debug_frame_cursor_name`,
`trl_debug_frame_cursor_open`, `trl_debug_frame_cursor_query`,
`trl_debug_breakpoint_count`, `trl_debug_breakpoint_number`,
`trl_debug_breakpoint_schema`, `trl_debug_breakpoint_name`,
`trl_debug_breakpoint_line`, `trl_debug_breakpoint_condition`,
`trl_debug_breakpoint_list_free`: what the debugger is given.

### SYNOPSIS

```c
typedef enum trl_debug_said { TRL_DEBUG_PAUSED, TRL_DEBUG_TIMED_OUT, TRL_DEBUG_CANCELLED, TRL_DEBUG_ENDED } trl_debug_said;
typedef enum trl_debug_reason { TRL_DEBUG_AT_BREAKPOINT, TRL_DEBUG_AT_STEP } trl_debug_reason;
typedef enum trl_debug_values { TRL_DEBUG_PARAMETERS, TRL_DEBUG_VARIABLES } trl_debug_values;

trl_debug_said trl_debug_event_said(const trl_debug_event *event);
const trl_debug_pause *trl_debug_event_pause(const trl_debug_event *event);
void trl_debug_event_free(trl_debug_event *event);
trl_debug_reason trl_debug_pause_reason(const trl_debug_pause *pause);
uint32_t trl_debug_pause_breakpoint(const trl_debug_pause *pause);
size_t trl_debug_pause_frame_count(const trl_debug_pause *pause);
const trl_debug_frame *trl_debug_pause_frame(const trl_debug_pause *pause, size_t index);
void trl_debug_pause_free(trl_debug_pause *pause);
bool trl_debug_frame_has_origin(const trl_debug_frame *frame);
trl_debug_kind trl_debug_frame_kind(const trl_debug_frame *frame);
const char *trl_debug_frame_specific_schema(const trl_debug_frame *frame);
const char *trl_debug_frame_specific_name(const trl_debug_frame *frame);
const char *trl_debug_frame_schema(const trl_debug_frame *frame);
const char *trl_debug_frame_name(const trl_debug_frame *frame);
bool trl_debug_frame_visible(const trl_debug_frame *frame);
bool trl_debug_frame_place(const trl_debug_frame *frame, uint64_t *out);
bool trl_debug_frame_line(const trl_debug_frame *frame, uint32_t *line, uint32_t *column);
size_t trl_debug_frame_value_count(const trl_debug_frame *frame, trl_debug_values which);
const char *trl_debug_frame_value_name(const trl_debug_frame *frame, trl_debug_values which, size_t index);
const char *trl_debug_frame_value_type(const trl_debug_frame *frame, trl_debug_values which, size_t index);
const trl_value *trl_debug_frame_value(const trl_debug_frame *frame, trl_debug_values which, size_t index);
size_t trl_debug_frame_cursor_count(const trl_debug_frame *frame);
const char *trl_debug_frame_cursor_name(const trl_debug_frame *frame, size_t index);
bool trl_debug_frame_cursor_open(const trl_debug_frame *frame, size_t index);
const char *trl_debug_frame_cursor_query(const trl_debug_frame *frame, size_t index);
size_t trl_debug_breakpoint_count(const trl_debug_breakpoint_list *list);
uint32_t trl_debug_breakpoint_number(const trl_debug_breakpoint_list *list, size_t index);
const char *trl_debug_breakpoint_schema(const trl_debug_breakpoint_list *list, size_t index);
const char *trl_debug_breakpoint_name(const trl_debug_breakpoint_list *list, size_t index);
uint32_t trl_debug_breakpoint_line(const trl_debug_breakpoint_list *list, size_t index);
const char *trl_debug_breakpoint_condition(const trl_debug_breakpoint_list *list, size_t index);
void trl_debug_breakpoint_list_free(trl_debug_breakpoint_list *list);
```

### DESCRIPTION

An event, a pause `trl_debug_stack` gave and a list of breakpoints are
the caller's, each freed by its own `_free`; a pause an event holds,
its frames, and every string and value they hold are borrowed from what
holds them, valid until it is freed.

A pause's frames are its call stack, the outermost first, so the last
is where it paused [PROTOCOL Event 'W']. A frame of a routine's or a
trigger's body has an origin: its routine's kind, its specific schema
and name, and the schema and name it was called by; one of a statement
run directly has none. A frame the debugger may not see into is not
visible, and shows its origin alone. Its place counts characters from
the beginning of the routine's text, its line and column count from 1.
Its parameters, a trigger's its transition variables, and its
variables each give a name, a type as SQL writes it, and a value, read
as [Values](23-values.md) reads one; its cursors, a name, whether open,
and a query.

### ERRORS

None of their own: a null handle, or a place past the last, gives null,
0 or false.

### EXAMPLES

The debugger's example above, and the driver's live tests, read them.
