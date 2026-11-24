/**
 * @file debug_test.c
 * @brief The routine debugger over a server of the test's own: Debug
 * requests answered, an Event read among a request's replies kept for the
 * next wait and never ending the request, a reply of another kind losing
 * the connection, and each allocation of copying a pause failing in turn
 * [CLIENT Debugging routines] [PROTOCOL Event 'W'].
 */
#include "codec/buffer.h"
#include "codec/debug.h"
#include "codec/kinds.h"
#include "debug/debug.h"
#include "harness.h"
#include "samples.h"
#include "script.h"
#include "turborel/turborel.h"
#ifdef TRL_FAULTS
#include "faults.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What a Debug asks, its first byte, as the samples give them. */
#define ASK_IDENTIFY 0U
#define ASK_CLEAR 4U
#define ASK_BREAKPOINTS 5U
#define ASK_STACK 8U
/* The header of a frame: its kind and length. */
#define HEADER_BYTES 5U

/* The samples, read once. */
static samples kept_samples;

/* Writes the server sample `name` whole to the client. */
static void sample_sent(script *s, const char *name) {
    const sample *one = sample_named(&kept_samples, name);
    if (one != NULL) {
        trl_buffer body;
        trl_buffer_init(&body);
        trl_put_raw(&body, one->frame + HEADER_BYTES, one->length - HEADER_BYTES);
        script_reply(s, one->frame[0], &body);
        trl_buffer_free(&body);
    }
}

/* Answers Hello granting `debugger`, and each Debug as the samples do: a
   clear's Complete after an Event paused and one ended, the breakpoints
   after an Event timed out, the stack by a Pong, which is no Debug reply. */
static void respond(script *s, uint8_t kind, trl_slice body) {
    if (kind == TRL_KIND_HELLO) {
        static const char *const granted[] = {TRL_CAPABILITY_DEBUGGER};
        script_ready(s, "ANN", granted, 1U);
        return;
    }
    uint8_t asked = body.length > 0U ? body.bytes[0] : 0xFFU;
    if (asked == ASK_IDENTIFY) {
        sample_sent(s, "debugged-connection");
    } else if (asked == ASK_CLEAR) {
        sample_sent(s, "event-paused");
        sample_sent(s, "event-ended");
        script_complete(s, 0, "00000", false, false);
    } else if (asked == ASK_BREAKPOINTS) {
        sample_sent(s, "event-timed-out");
        sample_sent(s, "debugged-breakpoints");
    } else if (asked == ASK_STACK) {
        script_bare(s, TRL_KIND_PONG);
    } else {
        script_complete(s, 0, "00000", false, false);
    }
}

/* A connection to a script answering as respond() does. */
static trl_conn *connected(script *s) {
    script_init(s, respond, NULL);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    return conn;
}

/* The next event kept; whether it says `said`. */
static bool next_says(trl_conn *conn, trl_debug_said said) {
    trl_debug_event *event = NULL;
    bool is =
        trl_debug_wait(conn, TRL_NO_LIMIT, &event) == TRL_OK && event != NULL && trl_debug_event_said(event) == said;
    trl_debug_event_free(event);
    return is;
}

/**
 * Verifies: [PROTOCOL Event 'W'] an Event read among a request's replies
 * never ends them: kept, the request's own reply read after it, and given
 * by the next waits in the order sent, a pause with its frame; the
 * connection's ID read; a wait for a time over a stream the program
 * opened refused (08006), the connection kept; a reply of another kind
 * than a Debug reply losing the connection.
 */
static void events_among_replies_are_kept(trl_test *test) {
    script s;
    trl_conn *conn = connected(&s);
    uint64_t id = 0U;
    TRL_CHECK(test, trl_debug_id(conn, &id) == TRL_OK && id == 2U);
    TRL_CHECK(test, trl_debug_clear(conn, 1U) == TRL_OK);
    trl_debug_breakpoint_list *all = NULL;
    TRL_CHECK(test, trl_debug_breakpoints(conn, &all) == TRL_OK && trl_debug_breakpoint_count(all) == 1U);
    trl_debug_breakpoint_list_free(all);
    trl_debug_event *event = NULL;
    TRL_CHECK(test,
              trl_debug_wait(conn, TRL_NO_LIMIT, &event) == TRL_OK && trl_debug_event_said(event) == TRL_DEBUG_PAUSED);
    const trl_debug_frame *frame = trl_debug_pause_frame(trl_debug_event_pause(event), 0U);
    uint64_t place = 0U;
    TRL_CHECK(test, trl_debug_frame_place(frame, &place) && place == 1U && trl_debug_frame_cursor_count(frame) == 1U &&
                        !trl_debug_frame_cursor_open(frame, 0U) &&
                        strcmp(trl_debug_frame_cursor_query(frame, 0U), "SELECT ID FROM STAFF") == 0);
    TRL_CHECK(test, trl_debug_frame_value(frame, TRL_DEBUG_VARIABLES, 0U) != NULL &&
                        trl_value_is_null(trl_debug_frame_value(frame, TRL_DEBUG_VARIABLES, 0U)));
    trl_debug_event_free(event);
    TRL_CHECK(test, next_says(conn, TRL_DEBUG_ENDED) && next_says(conn, TRL_DEBUG_TIMED_OUT));
    TRL_CHECK(test, trl_debug_wait(conn, 50U, &event) == TRL_FAILED && event == NULL &&
                        strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0 && !trl_conn_is_lost(conn));
    trl_debug_pause *pause = NULL;
    TRL_CHECK(test, trl_debug_stack(conn, &pause) == TRL_FAILED && pause == NULL && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Debugging routines] each handle read null or past its
 * last gives nothing; a breakpoint of a kind of no code, an action of no
 * code, or a null pointer refused before anything is sent; the events
 * kept dropped as the debugger detaches.
 */
static void handles_at_their_edges(trl_test *test) {
    TRL_CHECK(test, trl_debug_breakpoint_count(NULL) == 0U && trl_debug_breakpoint_name(NULL, 0U) == NULL &&
                        trl_debug_breakpoint_number(NULL, 0U) == 0U && trl_debug_breakpoint_line(NULL, 0U) == 0U &&
                        trl_debug_breakpoint_schema(NULL, 0U) == NULL &&
                        trl_debug_breakpoint_condition(NULL, 0U) == NULL);
    TRL_CHECK(test, trl_debug_event_said(NULL) == TRL_DEBUG_ENDED && trl_debug_event_pause(NULL) == NULL &&
                        trl_debug_pause_reason(NULL) == TRL_DEBUG_AT_STEP && trl_debug_pause_breakpoint(NULL) == 0U &&
                        trl_debug_pause_frame_count(NULL) == 0U && trl_debug_pause_frame(NULL, 0U) == NULL);
    TRL_CHECK(test, !trl_debug_frame_has_origin(NULL) && trl_debug_frame_kind(NULL) == TRL_DEBUG_PROCEDURE &&
                        trl_debug_frame_name(NULL) == NULL && trl_debug_frame_schema(NULL) == NULL &&
                        trl_debug_frame_specific_name(NULL) == NULL && trl_debug_frame_specific_schema(NULL) == NULL &&
                        !trl_debug_frame_visible(NULL) && !trl_debug_frame_place(NULL, NULL) &&
                        !trl_debug_frame_line(NULL, NULL, NULL));
    TRL_CHECK(test, trl_debug_frame_value_count(NULL, TRL_DEBUG_PARAMETERS) == 0U &&
                        trl_debug_frame_value_name(NULL, TRL_DEBUG_VARIABLES, 0U) == NULL &&
                        trl_debug_frame_value_type(NULL, TRL_DEBUG_VARIABLES, 0U) == NULL &&
                        trl_debug_frame_value(NULL, TRL_DEBUG_VARIABLES, 0U) == NULL &&
                        trl_debug_frame_cursor_count(NULL) == 0U && trl_debug_frame_cursor_name(NULL, 0U) == NULL &&
                        !trl_debug_frame_cursor_open(NULL, 0U) && trl_debug_frame_cursor_query(NULL, 0U) == NULL);
    script s;
    trl_conn *conn = connected(&s);
    trl_debug_kind no_kind = (trl_debug_kind)7;       // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    trl_debug_action no_action = (trl_debug_action)9; // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    trl_debug_location bad = {no_kind, NULL, "P", 1U, NULL};
    trl_debug_breakpoint_list *set = NULL;
    TRL_CHECK(test, trl_debug_break(conn, &bad, &set) == TRL_INVALID_ARGUMENT && set == NULL);
    TRL_CHECK(test, trl_debug_resume(conn, no_action) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_debug_attach(NULL, 1U) == TRL_INVALID_ARGUMENT &&
                        trl_debug_detach(NULL) == TRL_INVALID_ARGUMENT &&
                        trl_debug_clear(NULL, 1U) == TRL_INVALID_ARGUMENT &&
                        trl_debug_interrupt(NULL) == TRL_INVALID_ARGUMENT &&
                        trl_debug_breakpoints(conn, NULL) == TRL_INVALID_ARGUMENT &&
                        trl_debug_stack(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_debug_clear(conn, 1U) == TRL_OK && trl_debug_detach(conn) == TRL_OK);
    trl_debug_event *event = NULL;
    TRL_CHECK(test, trl_debug_interrupt(conn) == TRL_OK && trl_debug_wait(conn, TRL_NO_LIMIT, &event) == TRL_FAILED &&
                        event == NULL && trl_conn_is_lost(conn));
    trl_debug_event_free(NULL);
    trl_debug_pause_free(NULL);
    trl_debug_breakpoint_list_free(NULL);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL Event 'W'] a frame of a statement run directly,
 * of no origin, place or line, and not visible, read as such: no names,
 * no place, no line, no values or cursors; a pause at a step of no
 * breakpoint; a list of no breakpoint; a kind of values of no code none.
 */
static void a_frame_of_no_origin_is_read(trl_test *test) {
    trl_wire_frame wire_frame;
    memset(&wire_frame, 0, sizeof wire_frame);
    trl_wire_pause wire = {1U, 7U, &wire_frame, 1U};
    trl_debug_pause *pause = trl_debug_pause_made(&wire);
    TRL_CHECK(test, pause != NULL && trl_debug_pause_reason(pause) == TRL_DEBUG_AT_STEP &&
                        trl_debug_pause_breakpoint(pause) == 0U && trl_debug_pause_frame_count(pause) == 1U &&
                        trl_debug_pause_frame(pause, 1U) == NULL);
    const trl_debug_frame *frame = trl_debug_pause_frame(pause, 0U);
    uint64_t place = 0U;
    uint32_t line = 0U;
    TRL_CHECK(test, frame != NULL && !trl_debug_frame_has_origin(frame) && trl_debug_frame_name(frame) == NULL &&
                        trl_debug_frame_specific_name(frame) == NULL &&
                        trl_debug_frame_kind(frame) == TRL_DEBUG_PROCEDURE && !trl_debug_frame_visible(frame) &&
                        !trl_debug_frame_place(frame, &place) && !trl_debug_frame_line(frame, &line, NULL));
    trl_debug_values no_kind = (trl_debug_values)5; // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    TRL_CHECK(test, trl_debug_frame_value_count(frame, TRL_DEBUG_PARAMETERS) == 0U &&
                        trl_debug_frame_value_count(frame, no_kind) == 0U &&
                        trl_debug_frame_value_name(frame, no_kind, 0U) == NULL &&
                        trl_debug_frame_value(frame, TRL_DEBUG_VARIABLES, 0U) == NULL &&
                        trl_debug_frame_cursor_name(frame, 0U) == NULL && !trl_debug_frame_cursor_open(frame, 0U) &&
                        trl_debug_frame_cursor_query(frame, 0U) == NULL);
    trl_debug_pause_free(pause);
    trl_debug_breakpoint_list *none = trl_debug_list_made(NULL, 0U);
    TRL_CHECK(test,
              none != NULL && trl_debug_breakpoint_count(none) == 0U && trl_debug_breakpoint_name(none, 0U) == NULL);
    trl_debug_breakpoint_list_free(none);
}

#ifdef TRL_FAULTS
/* A clear whose replies carry a pause, the pause waited for. */
static bool pause_kept(void) {
    script s;
    trl_conn *conn = connected(&s);
    bool fine = true;
    if (conn != NULL && trl_conn_failure(conn) != NULL && !trl_conn_is_lost(conn)) {
        trl_status cleared = trl_debug_clear(conn, 1U);
        fine = allowed(cleared, trl_conn_failure(conn));
        trl_debug_event *event = NULL;
        trl_status waited = cleared == TRL_OK ? trl_debug_wait(conn, TRL_NO_LIMIT, &event) : TRL_OK;
        fine = fine && allowed(waited, trl_conn_failure(conn));
        trl_debug_event_free(event);
    }
    trl_conn_free(conn);
    script_free(&s);
    return fine;
}

/**
 * Verifies: [TOOL] R-MEM-2 each allocation of keeping an Event and its
 * pause failing in turn fails the request with SQLCODE -918, or the
 * connection, leaking and crashing on nothing.
 */
static void keeping_a_pause_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(pause_kept));
}
#endif

int main(int argc, char **argv) {
    if (!samples_read(&kept_samples)) {
        return 1;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(events_among_replies_are_kept),
        TRL_TEST_CASE(handles_at_their_edges),
        TRL_TEST_CASE(a_frame_of_no_origin_is_read),
#ifdef TRL_FAULTS
        TRL_TEST_CASE(keeping_a_pause_survives_each_fault),
#endif
    };
    int result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    samples_free(&kept_samples);
    return result;
}
