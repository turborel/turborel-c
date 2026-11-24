# Debugging Routines

[Contents](README.md)

*(extension)* A procedure, a function or a trigger written in SQL can be
run under a debugger. The debugger is another connection to the same
server. It attaches to the session that runs the routine, sets
breakpoints at lines of the routine's text, and, each time that session
pauses, reads its call stack, each frame's parameters, variables and
cursors, and tells it how to go on. [Debugging Routines](../client/31-debugging.md)
in the API Reference gives each function in full.

## Who may debug what

A connection attaches to the session of another connection of its own
user. The database's owner, and a user granted the role MONITOR, attach
to any connection's session, as they see every connection in
INFORMATION_SCHEMA.CONNECTIONS.

Attaching shows nothing by itself. The debugger sets breakpoints in,
pauses in and sees the variables of only the routines its user owns or
holds DEBUG on, as Turborel's
[GRANT DEBUG](https://github.com/turborel/turborel/blob/main/doc/sql/statements/grant-debug.md)
grants it. A frame of any other routine shows its name alone.

## Two connections

A program debugs with two connections: the target, whose session runs
the routine, and the debugger. The target's CALL pauses at a breakpoint
and completes only once the debugger lets it go on, so the target runs
it on a thread of its own while the debugger waits beside it.

The target defines a procedure, and `trl_debug_id` gives its
connection's ID, which the debugger attaches by. The debugger sets a
breakpoint at line 4 of the procedure's text, its UPDATE, counting the
line of CREATE as 1. The breakpoint names the procedure by its specific
name, RAISE_PAY_1 here, as a routine's name may be overloaded.

The target's CALL pauses as it comes to line 4. `trl_debug_wait`, given
at most ten seconds here, gives the pause and its call stack, the
outermost frame first, so the last frame is where the session paused.
`trl_debug_resume` with `TRL_DEBUG_OVER` runs the UPDATE and pauses at
the next statement; `TRL_DEBUG_CONTINUE` lets the session run to its
next breakpoint, or here to the end of its CALL. The debugger detaches.

```c
#include <turborel/turborel.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define URI "turborel://ann@127.0.0.1:17438/?tls_mode=disable"

static bool called;

/* The target's CALL, which pauses at the breakpoint until let go on. */
static void *call_raise_pay(void *target) {
    trl_result *result = NULL;
    called = trl_query(target, "CALL RAISE_PAY(10)", &result) == TRL_OK;
    trl_result_free(result);
    return NULL;
}

/* Shows a pause: why, then the innermost frame, its parameters and variables. */
static void show(const trl_debug_pause *pause) {
    if (trl_debug_pause_reason(pause) == TRL_DEBUG_AT_BREAKPOINT) {
        printf("Paused at breakpoint %u.\n", (unsigned)trl_debug_pause_breakpoint(pause));
    } else {
        printf("Paused.\n");
    }
    const trl_debug_frame *frame = trl_debug_pause_frame(pause, trl_debug_pause_frame_count(pause) - 1);
    uint32_t line = 0;
    uint32_t column = 0;
    trl_debug_frame_line(frame, &line, &column);
    printf("#1 PROCEDURE %s.%s line %u, column %u\n", trl_debug_frame_schema(frame), trl_debug_frame_name(frame),
           (unsigned)line, (unsigned)column);
    for (int which = TRL_DEBUG_PARAMETERS; which <= TRL_DEBUG_VARIABLES; ++which) {
        for (size_t i = 0; i < trl_debug_frame_value_count(frame, (trl_debug_values)which); ++i) {
            char text[64];
            trl_value_to_text(trl_debug_frame_value(frame, (trl_debug_values)which, i), text, sizeof text, NULL);
            printf("    %s %s = %s\n", trl_debug_frame_value_name(frame, (trl_debug_values)which, i),
                   trl_debug_frame_value_type(frame, (trl_debug_values)which, i), text);
        }
    }
}

int main(void) {
    trl_conn *target = NULL;
    trl_conn *debugger = NULL;
    if (trl_connect_uri(URI, &target) != TRL_OK || trl_connect_uri(URI, &debugger) != TRL_OK) {
        return 1;
    }
    static const char *const setup[] = {
        "CREATE TABLE STAFF (ID INTEGER PRIMARY KEY, SALARY INTEGER)",
        "INSERT INTO STAFF VALUES (1, 50000), (2, 60000)",
        "CREATE PROCEDURE RAISE_PAY(IN PCT INTEGER) MODIFIES SQL DATA\n"
        "BEGIN\n"
        "  DECLARE N INTEGER DEFAULT 0;\n"
        "  UPDATE STAFF SET SALARY = SALARY * (100 + PCT) / 100;\n"
        "  SET N = (SELECT COUNT(*) FROM STAFF);\n"
        "END",
        "COMMIT",
    };
    for (size_t i = 0; i < sizeof setup / sizeof setup[0]; ++i) {
        trl_result *result = NULL;
        trl_query(target, setup[i], &result);
        trl_result_free(result);
    }
    uint64_t id = 0;
    trl_debug_id(target, &id);
    if (trl_debug_attach(debugger, id) == TRL_OK) {
        printf("Attached to connection %llu.\n", (unsigned long long)id);
    }
    trl_debug_location at = {TRL_DEBUG_PROCEDURE, NULL, "RAISE_PAY", 4, NULL};
    trl_debug_breakpoint_list *set = NULL;
    if (trl_debug_break(debugger, &at, &set) == TRL_OK) {
        printf("Breakpoint %u at %s.%s line %u.\n", (unsigned)trl_debug_breakpoint_number(set, 0),
               trl_debug_breakpoint_schema(set, 0), trl_debug_breakpoint_name(set, 0),
               (unsigned)trl_debug_breakpoint_line(set, 0));
    }
    trl_debug_breakpoint_list_free(set);
    pthread_t thread;
    pthread_create(&thread, NULL, call_raise_pay, target);
    trl_debug_event *event = NULL;
    if (trl_debug_wait(debugger, 10000, &event) == TRL_OK && event != NULL) {
        show(trl_debug_event_pause(event));
    }
    trl_debug_event_free(event);
    trl_debug_resume(debugger, TRL_DEBUG_OVER);
    if (trl_debug_wait(debugger, 10000, &event) == TRL_OK && event != NULL) {
        show(trl_debug_event_pause(event));
    }
    trl_debug_event_free(event);
    trl_debug_resume(debugger, TRL_DEBUG_CONTINUE);
    pthread_join(thread, NULL);
    printf("The CALL %s.\n", called ? "completed" : "failed");
    if (trl_debug_detach(debugger) == TRL_OK) {
        printf("Detached.\n");
    }
    trl_conn_free(debugger);
    trl_conn_free(target);
    return 0;
}
```

```
Attached to connection 2.
Breakpoint 1 at ANN.RAISE_PAY_1 line 4.
Paused at breakpoint 1.
#1 PROCEDURE ANN.RAISE_PAY line 4, column 3
    PCT INTEGER = 10
    N INTEGER = 0
Paused.
#1 PROCEDURE ANN.RAISE_PAY line 5, column 3
    PCT INTEGER = 10
    N INTEGER = 0
The CALL completed.
Detached.
```

## Stepping

`TRL_DEBUG_INTO` pauses at the next statement, within a routine the
statement invokes too. `TRL_DEBUG_OVER` pauses at the next statement of
the same frame, or of the one it returns to. `TRL_DEBUG_OUT` pauses at
the next statement of the frame this one returns to.
`TRL_DEBUG_CONTINUE` runs to a breakpoint, and `trl_debug_interrupt`
pauses a running session at its next statement.

A breakpoint may carry a condition over the frame's parameters and
variables, and then pauses only where it is true:

```text
trl_debug_location at = {TRL_DEBUG_PROCEDURE, NULL, "RAISE_PAY", 5, "PCT > 5"};
```

`trl_debug_breakpoints` lists the breakpoints, `trl_debug_clear` clears
one, and `trl_debug_stack` gives the paused session's frames again.

## When a pause ends by itself

A paused session holds what it holds, its locks among them. It waits
for its debugger at most DEBUGGER.PAUSE_TIMEOUT_MS, and then goes on by
itself, its debugger told it timed out, `TRL_DEBUG_TIMED_OUT`. A cancel
of its statement ends a pause too, `TRL_DEBUG_CANCELLED`. A debugger
that detaches, or whose connection ends, lets its session go on as if
never debugged. A session that ends detaches its debugger, which is
told so, `TRL_DEBUG_ENDED`.

## What is refused

A refusal keeps both sessions. An ID of no connection the debugger's
user may see is SQLSTATE 08003; a breakpoint in a routine the user may
not debug, 42000, SQLCODE -104; an action the target's state does not
allow, as a resume of a session not paused or a second attach, TBD01,
SQLCODE -910. [Errors and Diagnostics](10-errors.md) reads them.
