/**
 * @file requests.c
 * @brief The routine debugger's requests, as turborel-client's `debug.rs`
 * makes them [CLIENT Debugging routines]: each a Debug of what it asks,
 * with `debugger` granted, answered by a Debugged or a Complete, or by an
 * Error the session goes on after [PROTOCOL Debug 'V'].
 */
#include "codec/buffer.h"
#include "codec/debug.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "conn/conn.h"
#include "debug/debug.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A slice of `text`, null as empty. */
static trl_slice slice_of(const char *text) {
    trl_slice slice = {(const uint8_t *)text, text == NULL ? 0U : strlen(text)};
    return slice;
}

/* Sends `request`, with `debugger` granted; whether it went. */
static bool sent(trl_conn *conn, const trl_debug_request *request) {
    if (!trl_conn_needs(conn, false, TRL_CAPABILITY_DEBUGGER)) {
        return false;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_debug(&conn->out, request, &error)) {
        return trl_conn_codec_failed(conn, &error);
    }
    return trl_conn_send(conn);
}

/* Sends `request` and reads its Debugged of `what` into `out`. */
static bool debugged(trl_conn *conn, const trl_debug_request *request, uint8_t what, trl_reply *out) {
    if (!sent(conn, request) || !trl_conn_receive(conn, out)) {
        return false;
    }
    if (out->kind == TRL_KIND_DEBUGGED && out->as.debugged.what == what) {
        return true;
    }
    if (out->kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &out->as.error);
        return false;
    }
    trl_conn_lose_by(conn, "the server sent no Debug reply");
    return false;
}

/* Sends a request of `what` alone, or of its one field, and reads its Complete. */
static trl_status done(trl_conn *conn, const trl_debug_request *request) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    return sent(conn, request) && trl_conn_completion(conn) ? TRL_OK : TRL_FAILED;
}

trl_status trl_debug_id(trl_conn *conn, uint64_t *out) {
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_debug_request request = {.what = TRL_DEBUG_IDENTIFY};
    trl_reply reply;
    if (!debugged(conn, &request, TRL_DEBUGGED_CONNECTION, &reply)) {
        return TRL_FAILED;
    }
    *out = reply.as.debugged.connection;
    return TRL_OK;
}

trl_status trl_debug_attach(trl_conn *conn, uint64_t id) {
    trl_debug_request request = {.what = TRL_DEBUG_ATTACH, .connection = id};
    return done(conn, &request);
}

trl_status trl_debug_detach(trl_conn *conn) {
    if (conn != NULL) {
        /* The events kept are of a target no longer debugged. */
        trl_conn_events_drop(conn);
    }
    trl_debug_request request = {.what = TRL_DEBUG_DETACH};
    return done(conn, &request);
}

/* A list of the breakpoints `reply` gives, of one or of all, into `out`. */
static trl_status listed(trl_conn *conn, const trl_reply *reply, trl_debug_breakpoint_list **out) {
    const trl_debugged *d = &reply->as.debugged;
    *out = d->what == TRL_DEBUGGED_BREAKPOINT ? trl_debug_list_made(&d->breakpoint, 1U)
                                              : trl_debug_list_made(d->breakpoints, d->breakpoint_count);
    if (*out == NULL) {
        trl_failure_no_memory(&conn->failure);
        return TRL_NO_MEMORY;
    }
    return TRL_OK;
}

trl_status trl_debug_break(trl_conn *conn, const trl_debug_location *at, trl_debug_breakpoint_list **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || at == NULL || out == NULL || at->name == NULL || (unsigned int)at->kind > TRL_DEBUG_KIND_MOST) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_debug_request request = {
        .what = TRL_DEBUG_BREAK,
        .kind = (uint8_t)at->kind,
        .schema = slice_of(at->schema),
        .name = slice_of(at->name),
        .line = at->line,
        .conditioned = at->condition != NULL,
        .condition = slice_of(at->condition),
    };
    trl_reply reply;
    if (!debugged(conn, &request, TRL_DEBUGGED_BREAKPOINT, &reply)) {
        return TRL_FAILED;
    }
    return listed(conn, &reply, out);
}

trl_status trl_debug_clear(trl_conn *conn, uint32_t number) {
    trl_debug_request request = {.what = TRL_DEBUG_CLEAR, .number = number};
    return done(conn, &request);
}

trl_status trl_debug_breakpoints(trl_conn *conn, trl_debug_breakpoint_list **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_debug_request request = {.what = TRL_DEBUG_BREAKPOINTS};
    trl_reply reply;
    if (!debugged(conn, &request, TRL_DEBUGGED_BREAKPOINTS, &reply)) {
        return TRL_FAILED;
    }
    return listed(conn, &reply, out);
}

trl_status trl_debug_resume(trl_conn *conn, trl_debug_action how) {
    if ((unsigned int)how > TRL_DEBUG_ACTION_MOST) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_debug_request request = {.what = TRL_DEBUG_RESUME, .action = (uint8_t)how};
    return done(conn, &request);
}

trl_status trl_debug_interrupt(trl_conn *conn) {
    trl_debug_request request = {.what = TRL_DEBUG_INTERRUPT};
    return done(conn, &request);
}

trl_status trl_debug_stack(trl_conn *conn, trl_debug_pause **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_debug_request request = {.what = TRL_DEBUG_STACK};
    trl_reply reply;
    if (!debugged(conn, &request, TRL_DEBUGGED_STACK, &reply)) {
        return TRL_FAILED;
    }
    if (!reply.as.debugged.paused) {
        return TRL_OK;
    }
    *out = trl_debug_pause_made(&reply.as.debugged.pause);
    if (*out == NULL) {
        trl_failure_no_memory(&conn->failure);
        return TRL_NO_MEMORY;
    }
    return TRL_OK;
}

trl_status trl_debug_wait(trl_conn *conn, uint32_t within_ms, trl_debug_event **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = trl_conn_event_take(conn);
    if (*out != NULL) {
        return TRL_OK;
    }
    int arrives = trl_conn_arrives_within(conn, within_ms);
    if (arrives <= 0) {
        return arrives == 0 ? TRL_OK : TRL_FAILED;
    }
    trl_reply reply;
    int taken = trl_conn_receive_unasked(conn, &reply);
    if (taken < 0) {
        return TRL_FAILED;
    }
    if (taken > 0) {
        /* An Event kept, or a Notice or a Status, kept as ever. */
        *out = trl_conn_event_take(conn);
        return TRL_OK;
    }
    if (reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
        return TRL_FAILED;
    }
    trl_conn_lose_by(conn, "the server answered no request");
    return TRL_FAILED;
}
