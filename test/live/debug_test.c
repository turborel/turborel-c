/**
 * @file debug_test.c
 * @brief The routine debugger against a real server (RULES.md R-TEST-8),
 * two connections: a target whose CALL runs on a thread of its own, and
 * a debugger that attaches to it, breaks, waits for the pause, reads the
 * frames, steps, resumes and detaches; each wait signalled, and still
 * waiting; and the refusals that keep the session
 * [CLIENT Debugging routines] [PROTOCOL Debug 'V'].
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "format.h"
#include "harness.h"
#include "live.h"
#include "server.h"
#include "signals.h"
#include "turborel/turborel.h"

/* The platform declares its threads in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
/* How long the debugger waits for an event, and a pause to let one come. */
#define WAIT_MS 10000U
#define SETTLE_NS 300000000L
/* A wait that nothing ends, and the most it may take though signalled. */
#define SHORT_WAIT_MS 200U
#define SHORT_WAIT_AT_MOST_MS 1000U
#define MS_PER_SECOND 1000U
#define NS_PER_MS 1000000L

/* The procedure debugged: line 4 its UPDATE, line 5 its SET. */
#define RAISE_PAY                                                                                                      \
    "CREATE PROCEDURE RAISE_PAY(IN PCT INTEGER) MODIFIES SQL DATA\n"                                                   \
    "BEGIN\n"                                                                                                          \
    "  DECLARE N INTEGER DEFAULT 0;\n"                                                                                 \
    "  UPDATE STAFF SET SALARY = SALARY * (100 + PCT) / 100;\n"                                                        \
    "  SET N = (SELECT COUNT(*) FROM STAFF);\n"                                                                        \
    "END"

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    if (ran != TRL_OK) {
        char line[PATH_BYTES];
        (void)trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        (void)fprintf(stderr, "%s: %s\n", sql, line);
    }
    trl_result_free(result);
    return ran == TRL_OK;
}

/* A statement the target runs on a thread of its own, and what it came to. */
typedef struct calling {
    trl_conn *conn;
    const char *sql;
    bool ran;
} calling;

static void *call_in(void *context) {
    calling *c = context;
    c->ran = run(c->conn, c->sql);
    return NULL;
}

/* Lets the server's event come before the debugger reads. */
static void settle(void) {
    struct timespec pause = {0, SETTLE_NS};
    (void)nanosleep(&pause, NULL);
}

/* Runs the statement as call_in() does, after a settle. */
static void *call_late(void *context) {
    settle();
    return call_in(context);
}

/* Milliseconds of a clock that only moves on. */
static uint64_t now_ms(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return ((uint64_t)now.tv_sec * MS_PER_SECOND) + (uint64_t)(now.tv_nsec / NS_PER_MS);
}

/* Whether the connection's failure is of `sqlcode` and `sqlstate`, the session kept. */
static bool refused(const trl_conn *conn, int32_t sqlcode, const char *sqlstate) {
    const trl_failure *failure = trl_conn_failure(conn);
    return trl_failure_sqlcode(failure) == sqlcode && strcmp(trl_failure_sqlstate(failure), sqlstate) == 0 &&
           !trl_conn_is_lost(conn);
}

/* Whether a value is the exact number `text`. */
static bool exact_is(const trl_value *value, const char *text) {
    trl_decimal number;
    char written[TRL_DECIMAL_TEXT_BYTES];
    return value != NULL && trl_value_decimal(value, &number) == TRL_OK &&
           trl_decimal_text(&number, written, sizeof written) == TRL_OK && strcmp(written, text) == 0;
}

/* Whether the innermost frame of `pause` is RAISE_PAY at `line`, PCT 10 and N `n`. */
static bool raise_pay_at(const trl_debug_pause *pause, uint32_t line, const char *n) {
    size_t count = trl_debug_pause_frame_count(pause);
    const trl_debug_frame *frame = count == 0U ? NULL : trl_debug_pause_frame(pause, count - 1U);
    uint32_t at = 0U;
    uint32_t column = 0U;
    return frame != NULL && trl_debug_frame_has_origin(frame) && trl_debug_frame_kind(frame) == TRL_DEBUG_PROCEDURE &&
           strcmp(trl_debug_frame_name(frame), "RAISE_PAY") == 0 && strcmp(trl_debug_frame_schema(frame), "ANN") == 0 &&
           trl_debug_frame_visible(frame) && trl_debug_frame_line(frame, &at, &column) && at == line && column == 3U &&
           trl_debug_frame_value_count(frame, TRL_DEBUG_PARAMETERS) == 1U &&
           strcmp(trl_debug_frame_value_name(frame, TRL_DEBUG_PARAMETERS, 0U), "PCT") == 0 &&
           strcmp(trl_debug_frame_value_type(frame, TRL_DEBUG_PARAMETERS, 0U), "INTEGER") == 0 &&
           exact_is(trl_debug_frame_value(frame, TRL_DEBUG_PARAMETERS, 0U), "10") &&
           trl_debug_frame_value_count(frame, TRL_DEBUG_VARIABLES) == 1U &&
           exact_is(trl_debug_frame_value(frame, TRL_DEBUG_VARIABLES, 0U), n);
}

/* The next event of `debugger`, waited for; whether it is a pause of `reason`, kept in `*out`. */
static bool paused(trl_conn *debugger, trl_debug_reason reason, trl_debug_event **out) {
    *out = NULL;
    return trl_debug_wait(debugger, WAIT_MS, out) == TRL_OK && *out != NULL &&
           trl_debug_event_said(*out) == TRL_DEBUG_PAUSED &&
           trl_debug_pause_reason(trl_debug_event_pause(*out)) == reason;
}

/* Sets the breakpoint at line 4 of RAISE_PAY; its number, 0 if none was set. */
static uint32_t broken_at_update(trl_test *test, trl_conn *debugger) {
    trl_debug_location at = {TRL_DEBUG_PROCEDURE, NULL, "RAISE_PAY", 4U, NULL};
    trl_debug_breakpoint_list *set = NULL;
    TRL_CHECK(test, trl_debug_break(debugger, &at, &set) == TRL_OK && trl_debug_breakpoint_count(set) == 1U);
    TRL_CHECK(test, trl_debug_breakpoint_line(set, 0U) == 4U && trl_debug_breakpoint_condition(set, 0U) == NULL &&
                        strncmp(trl_debug_breakpoint_name(set, 0U), "RAISE_PAY", 9U) == 0 &&
                        strcmp(trl_debug_breakpoint_schema(set, 0U), "ANN") == 0);
    uint32_t number = trl_debug_breakpoint_number(set, 0U);
    trl_debug_breakpoint_list_free(set);
    return number;
}

/**
 * Verifies: [CLIENT Debugging routines] a debugger attached to the
 * target's session by its ID sets a breakpoint at a line of a procedure;
 * the target's CALL pauses there, and the debugger, waiting, is given the
 * pause: the breakpoint, the frame of the procedure at its line and
 * column, its parameter and its variable with their types and values; the
 * stack again; a step over to the next statement, its event read past a
 * request's reply and kept; on to the CALL's end; the breakpoints listed
 * and cleared; detached [PROTOCOL Event 'W'] [PROTOCOL Debugged 'V'].
 */
static void a_debugger_drives_a_paused_procedure(trl_test *test) {
    trl_conn *target = live_trusted(NULL);
    trl_conn *debugger = live_trusted(NULL);
    TRL_CHECK(test, trl_conn_granted(debugger, TRL_CAPABILITY_DEBUGGER));
    uint64_t id = 0U;
    TRL_CHECK(test, trl_debug_id(target, &id) == TRL_OK && trl_debug_attach(debugger, id) == TRL_OK);
    uint32_t number = broken_at_update(test, debugger);
    trl_debug_pause *again = NULL;
    TRL_CHECK(test, trl_debug_stack(debugger, &again) == TRL_OK && again == NULL);
    calling call = {target, "CALL RAISE_PAY(10)", false};
    pthread_t thread;
    TRL_CHECK(test, pthread_create(&thread, NULL, call_in, &call) == 0);
    trl_debug_event *event = NULL;
    TRL_CHECK(test, paused(debugger, TRL_DEBUG_AT_BREAKPOINT, &event) &&
                        trl_debug_pause_breakpoint(trl_debug_event_pause(event)) == number &&
                        raise_pay_at(trl_debug_event_pause(event), 4U, "0"));
    trl_debug_event_free(event);
    TRL_CHECK(test, trl_debug_stack(debugger, &again) == TRL_OK && raise_pay_at(again, 4U, "0"));
    trl_debug_pause_free(again);
    TRL_CHECK(test, trl_debug_resume(debugger, TRL_DEBUG_OVER) == TRL_OK);
    settle();
    trl_debug_breakpoint_list *all = NULL;
    TRL_CHECK(test, trl_debug_breakpoints(debugger, &all) == TRL_OK && trl_debug_breakpoint_count(all) == 1U &&
                        trl_debug_breakpoint_number(all, 0U) == number);
    trl_debug_breakpoint_list_free(all);
    TRL_CHECK(test, paused(debugger, TRL_DEBUG_AT_STEP, &event) && raise_pay_at(trl_debug_event_pause(event), 5U, "0"));
    trl_debug_event_free(event);
    TRL_CHECK(test, trl_debug_resume(debugger, TRL_DEBUG_CONTINUE) == TRL_OK);
    (void)pthread_join(thread, NULL);
    TRL_CHECK(test, call.ran);
    TRL_CHECK(test, trl_debug_clear(debugger, number) == TRL_OK && trl_debug_breakpoints(debugger, &all) == TRL_OK &&
                        trl_debug_breakpoint_count(all) == 0U);
    trl_debug_breakpoint_list_free(all);
    TRL_CHECK(test, trl_debug_detach(debugger) == TRL_OK);
    trl_conn_free(debugger);
    trl_conn_free(target);
}

/**
 * Verifies: [CLIENT Debugging routines] signals of a handler installed
 * without SA_RESTART interrupt no wait (turborel's RULES.md D1047): the debugger's
 * wait with nothing sent, signalled again and again, gives no event
 * within about its bound, the time before each signal kept; its wait for
 * the pause, signalled until the event comes, is given it; and the
 * target's CALL, its read of the reply signalled while it is paused,
 * completes once resumed [PROTOCOL Event 'W'].
 */
static void signals_interrupt_no_wait(trl_test *test) {
    trl_conn *target = live_trusted(NULL);
    trl_conn *debugger = live_trusted(NULL);
    uint64_t id = 0U;
    TRL_CHECK(test, trl_debug_id(target, &id) == TRL_OK && trl_debug_attach(debugger, id) == TRL_OK);
    uint32_t number = broken_at_update(test, debugger);
    int before = signals_taken();
    signaller on_debugger;
    TRL_CHECK(test, signaller_start(&on_debugger, pthread_self()));
    trl_debug_event *event = NULL;
    uint64_t began = now_ms();
    TRL_CHECK(test, trl_debug_wait(debugger, SHORT_WAIT_MS, &event) == TRL_OK && event == NULL &&
                        !trl_conn_is_lost(debugger));
    TRL_CHECK(test, now_ms() - began < SHORT_WAIT_AT_MOST_MS);
    calling call = {target, "CALL RAISE_PAY(10)", false};
    pthread_t thread;
    TRL_CHECK(test, pthread_create(&thread, NULL, call_late, &call) == 0);
    TRL_CHECK(test, paused(debugger, TRL_DEBUG_AT_BREAKPOINT, &event) &&
                        trl_debug_pause_breakpoint(trl_debug_event_pause(event)) == number);
    trl_debug_event_free(event);
    signaller_stop(&on_debugger);
    signaller on_call;
    TRL_CHECK(test, signaller_start(&on_call, thread));
    settle();
    signaller_stop(&on_call);
    TRL_CHECK(test, trl_debug_resume(debugger, TRL_DEBUG_CONTINUE) == TRL_OK);
    (void)pthread_join(thread, NULL);
    TRL_CHECK(test, call.ran && !trl_conn_is_lost(target) && signals_taken() > before);
    TRL_CHECK(test, trl_debug_clear(debugger, number) == TRL_OK && trl_debug_detach(debugger) == TRL_OK);
    trl_conn_free(debugger);
    trl_conn_free(target);
}

/* A procedure that runs long: a loop of a billion steps. */
#define LOOPS                                                                                                          \
    "CREATE PROCEDURE LOOPS() MODIFIES SQL DATA\n"                                                                     \
    "BEGIN\n"                                                                                                          \
    "  DECLARE I INTEGER DEFAULT 0;\n"                                                                                 \
    "  WHILE I < 1000000000 DO SET I = I + 1; END WHILE;\n"                                                            \
    "END"

/**
 * Verifies: [CLIENT Debugging routines] a running target interrupted
 * pauses at its next statement, a pause of a step; its statement then
 * cancelled from another connection, the debugger is told it was
 * cancelled, and the target's CALL fails as cancelled (SQLCODE -920)
 * [PROTOCOL Event 'W'].
 */
static void a_running_target_is_interrupted_and_cancelled(trl_test *test) {
    trl_conn *target = live_trusted(NULL);
    trl_conn *debugger = live_trusted(NULL);
    TRL_CHECK(test, run(target, LOOPS) && run(target, "COMMIT"));
    uint64_t id = 0U;
    TRL_CHECK(test, trl_debug_id(target, &id) == TRL_OK && trl_debug_attach(debugger, id) == TRL_OK);
    trl_canceller *canceller = NULL;
    TRL_CHECK(test, trl_canceller_new(target, &canceller) == TRL_OK);
    calling call = {target, "CALL LOOPS()", true};
    pthread_t thread;
    TRL_CHECK(test, pthread_create(&thread, NULL, call_in, &call) == 0);
    settle();
    TRL_CHECK(test, trl_debug_interrupt(debugger) == TRL_OK);
    trl_debug_event *event = NULL;
    TRL_CHECK(test, paused(debugger, TRL_DEBUG_AT_STEP, &event));
    const trl_debug_pause *pause = trl_debug_event_pause(event);
    const trl_debug_frame *frame = trl_debug_pause_frame(pause, trl_debug_pause_frame_count(pause) - 1U);
    TRL_CHECK(test, frame != NULL && strcmp(trl_debug_frame_name(frame), "LOOPS") == 0 &&
                        strcmp(trl_debug_frame_value_name(frame, TRL_DEBUG_VARIABLES, 0U), "I") == 0);
    trl_debug_event_free(event);
    TRL_CHECK(test, trl_cancel(canceller) == TRL_OK);
    TRL_CHECK(test, trl_debug_wait(debugger, WAIT_MS, &event) == TRL_OK &&
                        trl_debug_event_said(event) == TRL_DEBUG_CANCELLED);
    trl_debug_event_free(event);
    (void)pthread_join(thread, NULL);
    TRL_CHECK(test, !call.ran && trl_failure_sqlcode(trl_conn_failure(target)) == -920);
    TRL_CHECK(test, trl_debug_detach(debugger) == TRL_OK);
    trl_canceller_free(canceller);
    trl_conn_free(debugger);
    trl_conn_free(target);
}

/* A connection of BOB, a user who owns no routine. */
static trl_conn *bobs(void) {
    trl_conn *conn = live_trusted(NULL);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    if (conn != NULL && trl_conn_change_user(conn, "bob", trust) != TRL_OK) {
        trl_conn_free(conn);
        conn = NULL;
    }
    trl_mechanism_free(trust);
    return conn;
}

/**
 * Verifies: [PROTOCOL Debug 'V'] the refusals that keep the session:
 * an action of a connection attached to none, a second attach, SQLSTATE
 * TBD01 (SQLCODE -910); a wait whose time runs out with nothing sent,
 * no event and the connection kept; an ID of no connection, or of another user's,
 * 08003; a breakpoint in a routine the user neither owns nor holds DEBUG
 * on, 42000 (SQLCODE -104), set once DEBUG is granted, its condition
 * given back; and without `debugger` granted, nothing sent (08006).
 */
static void the_refusals_keep_the_session(trl_test *test) {
    trl_conn *owner = live_trusted(NULL);
    trl_conn *debugger = live_trusted(NULL);
    TRL_CHECK(test, run(owner, "CREATE USER BOB PASSWORD 'bob secret'") && run(owner, "COMMIT"));
    TRL_CHECK(test, trl_debug_resume(debugger, TRL_DEBUG_CONTINUE) == TRL_FAILED && refused(debugger, -910, "TBD01"));
    TRL_CHECK(test, trl_debug_attach(debugger, 999999U) == TRL_FAILED && refused(debugger, -919, "08003"));
    uint64_t id = 0U;
    TRL_CHECK(test, trl_debug_id(owner, &id) == TRL_OK && trl_debug_attach(debugger, id) == TRL_OK);
    TRL_CHECK(test, trl_debug_attach(debugger, id) == TRL_FAILED && refused(debugger, -910, "TBD01"));
    trl_debug_event *none = NULL;
    TRL_CHECK(test, trl_debug_wait(debugger, 100U, &none) == TRL_OK && none == NULL && !trl_conn_is_lost(debugger) &&
                        trl_conn_ping(debugger) == TRL_OK);
    trl_conn *bob = bobs();
    trl_conn *bob_target = bobs();
    uint64_t bob_id = 0U;
    TRL_CHECK(test, bob != NULL && trl_debug_attach(bob, id) == TRL_FAILED && refused(bob, -919, "08003"));
    TRL_CHECK(test, trl_debug_id(bob_target, &bob_id) == TRL_OK && trl_debug_attach(bob, bob_id) == TRL_OK);
    trl_debug_location at = {TRL_DEBUG_PROCEDURE, "ANN", "RAISE_PAY", 4U, "PCT > 5"};
    trl_debug_breakpoint_list *set = NULL;
    TRL_CHECK(test, trl_debug_break(bob, &at, &set) == TRL_FAILED && refused(bob, -104, "42000") && set == NULL);
    TRL_CHECK(test, run(owner, "GRANT DEBUG ON PROCEDURE ANN.RAISE_PAY TO BOB") && run(owner, "COMMIT"));
    TRL_CHECK(test, trl_debug_break(bob, &at, &set) == TRL_OK &&
                        strcmp(trl_debug_breakpoint_condition(set, 0U), "PCT > 5") == 0);
    trl_debug_breakpoint_list_free(set);
    TRL_CHECK(test, trl_debug_detach(bob) == TRL_OK && trl_debug_detach(debugger) == TRL_OK);
    trl_conn_free(bob);
    trl_conn_free(bob_target);
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_ask_none(options);
    trl_conn *bare = live_trusted(options);
    TRL_CHECK(test, trl_debug_id(bare, &id) == TRL_FAILED &&
                        strcmp(trl_failure_sqlstate(trl_conn_failure(bare)), "08006") == 0 && !trl_conn_is_lost(bare));
    TRL_CHECK(test, trl_debug_id(NULL, &id) == TRL_INVALID_ARGUMENT &&
                        trl_debug_wait(NULL, 0U, NULL) == TRL_INVALID_ARGUMENT);
    trl_conn_free(bare);
    trl_conn_options_free(options);
    trl_conn_free(debugger);
    trl_conn_free(owner);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-debug"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-debug", cwd);
    const char *why = NULL;
    if (!signals_install()) {
        return EXIT_FAILURE;
    }
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    /* The table and procedure each case debugs, made once. */
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") &&
                 run(owner, "CREATE TABLE STAFF (ID INTEGER PRIMARY KEY, SALARY INTEGER)") &&
                 run(owner, "INSERT INTO STAFF VALUES (1, 50000), (2, 60000)") && run(owner, RAISE_PAY) &&
                 run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_debugger_drives_a_paused_procedure),
        TRL_TEST_CASE(signals_interrupt_no_wait),
        TRL_TEST_CASE(the_refusals_keep_the_session),
        TRL_TEST_CASE(a_running_target_is_interrupted_and_cancelled),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
// NOLINTEND(misc-include-cleaner)
