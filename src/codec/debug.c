/**
 * @file debug.c
 * @brief The routine debugger's frames, as turborel-protocol's encode and
 * decode them, with `debugger` *(extension)*: Debug 'V' written by what it
 * asks, Debugged 'V' and Event 'W' read by what they give
 * [PROTOCOL Debug 'V'] [PROTOCOL Debugged 'V'] [PROTOCOL Event 'W'].
 */
#include "codec/debug.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/kinds.h"
#include "codec/reader.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The fewest bytes of a breakpoint, a named value, a cursor and a frame. */
#define TRL_BREAKPOINT_LEAST_BYTES 17U
#define TRL_NAMED_LEAST_BYTES 9U
#define TRL_CURSOR_LEAST_BYTES 9U
#define TRL_FRAME_LEAST_BYTES 16U

/* Writes Break's fields: kind, schema, name, line, then its condition. */
static bool put_break(trl_buffer *buffer, const trl_debug_request *request, trl_codec_error *error) {
    if (request->kind > TRL_DEBUG_KIND_MOST) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "a breakpoint's kind is 0, 1 or 2");
        return false;
    }
    trl_put_u8(buffer, request->kind);
    if (!trl_write_string(buffer, request->schema, error) || !trl_write_string(buffer, request->name, error)) {
        return false;
    }
    trl_put_u32(buffer, request->line);
    trl_put_flag(buffer, request->conditioned);
    return !request->conditioned || trl_write_string(buffer, request->condition, error);
}

/* Writes the fields of what the request asks. */
static bool put_asked(trl_buffer *buffer, const trl_debug_request *request, trl_codec_error *error) {
    switch (request->what) {
    case TRL_DEBUG_ATTACH:
        trl_put_u64(buffer, request->connection);
        return true;
    case TRL_DEBUG_BREAK:
        return put_break(buffer, request, error);
    case TRL_DEBUG_CLEAR:
        trl_put_u32(buffer, request->number);
        return true;
    case TRL_DEBUG_RESUME:
        if (request->action > TRL_DEBUG_ACTION_MOST) {
            trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "a resume's action is 0 to 3");
            return false;
        }
        trl_put_u8(buffer, request->action);
        return true;
    case TRL_DEBUG_IDENTIFY:
    case TRL_DEBUG_DETACH:
    case TRL_DEBUG_BREAKPOINTS:
    case TRL_DEBUG_INTERRUPT:
    case TRL_DEBUG_STACK:
        return true;
    default:
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "no Debug request is of this kind");
        return false;
    }
}

bool trl_encode_debug(trl_buffer *buffer, const trl_debug_request *request, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_DEBUG);
    trl_put_u8(buffer, request->what);
    bool written = put_asked(buffer, request, error);
    if (written) {
        trl_frame_finish(buffer, start);
        if (trl_buffer_ok(buffer, error)) {
            return true;
        }
    }
    (void)trl_buffer_ok(buffer, error);
    if (buffer->state == TRL_BUFFER_OK) {
        buffer->length = start;
    }
    return false;
}

/* Reads a breakpoint. */
static bool read_breakpoint(trl_reader *reader, trl_wire_breakpoint *out) {
    if (!trl_read_u32(reader, &out->number) || !trl_read_string(reader, &out->schema) ||
        !trl_read_string(reader, &out->name) || !trl_read_u32(reader, &out->line) ||
        !trl_read_flag(reader, &out->conditioned)) {
        return false;
    }
    return !out->conditioned || trl_read_string(reader, &out->condition);
}

/* Reads a count of breakpoints, then each. */
static bool read_breakpoints(trl_reader *reader, trl_debugged *out) {
    if (!trl_read_count(reader, TRL_BREAKPOINT_LEAST_BYTES, &out->breakpoint_count)) {
        return false;
    }
    uint32_t room = 0U;
    out->breakpoints = trl_reader_items(reader, out->breakpoint_count, sizeof(trl_wire_breakpoint), &room);
    for (uint32_t i = 0U; out->breakpoints != NULL && i < out->breakpoint_count; ++i) {
        out->breakpoints = trl_reader_room(reader, out->breakpoints, &room, i, sizeof(trl_wire_breakpoint));
        if (out->breakpoints == NULL || !read_breakpoint(reader, &out->breakpoints[i])) {
            return false;
        }
    }
    return out->breakpoints != NULL;
}

/* Reads a count of named values, each its name, type and value. */
static bool read_named(trl_reader *reader, trl_wire_named **out, uint32_t *count) {
    if (!trl_read_count(reader, TRL_NAMED_LEAST_BYTES, count)) {
        return false;
    }
    uint32_t room = 0U;
    *out = trl_reader_items(reader, *count, sizeof(trl_wire_named), &room);
    for (uint32_t i = 0U; *out != NULL && i < *count; ++i) {
        *out = trl_reader_room(reader, *out, &room, i, sizeof(trl_wire_named));
        if (*out == NULL || !trl_read_string(reader, &(*out)[i].name) || !trl_read_string(reader, &(*out)[i].type) ||
            !trl_read_value(reader, &(*out)[i].value)) {
            return false;
        }
    }
    return *out != NULL;
}

/* Reads a count of cursors, each its name, whether open, and its query. */
static bool read_cursors(trl_reader *reader, trl_wire_frame *frame) {
    if (!trl_read_count(reader, TRL_CURSOR_LEAST_BYTES, &frame->cursor_count)) {
        return false;
    }
    uint32_t room = 0U;
    frame->cursors = trl_reader_items(reader, frame->cursor_count, sizeof(trl_wire_cursor), &room);
    for (uint32_t i = 0U; frame->cursors != NULL && i < frame->cursor_count; ++i) {
        frame->cursors = trl_reader_room(reader, frame->cursors, &room, i, sizeof(trl_wire_cursor));
        trl_wire_cursor *cursor = frame->cursors;
        if (cursor == NULL || !trl_read_string(reader, &cursor[i].name) || !trl_read_flag(reader, &cursor[i].open) ||
            !trl_read_string(reader, &cursor[i].query)) {
            return false;
        }
    }
    return frame->cursors != NULL;
}

/* Reads a frame's origin, if it has one: kind, specific name, name. */
static bool read_origin(trl_reader *reader, trl_wire_frame *frame) {
    if (!trl_read_flag(reader, &frame->has_origin) || !frame->has_origin) {
        return !reader->failed;
    }
    if (!trl_read_u8(reader, &frame->kind) || !trl_read_string(reader, &frame->specific_schema) ||
        !trl_read_string(reader, &frame->specific_name) || !trl_read_string(reader, &frame->schema) ||
        !trl_read_string(reader, &frame->name)) {
        return false;
    }
    return frame->kind <= TRL_DEBUG_KIND_MOST || trl_reader_fail(reader, "a frame's kind is of no code");
}

/* Reads a frame. */
static bool read_frame(trl_reader *reader, trl_wire_frame *frame) {
    if (!read_origin(reader, frame) || !trl_read_flag(reader, &frame->visible) ||
        !trl_read_flag(reader, &frame->has_place)) {
        return false;
    }
    if (frame->has_place && !trl_read_u64(reader, &frame->place)) {
        return false;
    }
    if (!trl_read_flag(reader, &frame->has_line)) {
        return false;
    }
    if (frame->has_line && (!trl_read_u32(reader, &frame->line) || !trl_read_u32(reader, &frame->column))) {
        return false;
    }
    return read_named(reader, &frame->parameters, &frame->parameter_count) &&
           read_named(reader, &frame->variables, &frame->variable_count) && read_cursors(reader, frame);
}

/* Reads a pause: its reason, then its frames. */
static bool read_pause(trl_reader *reader, trl_wire_pause *out) {
    if (!trl_read_u8(reader, &out->reason)) {
        return false;
    }
    if (out->reason > TRL_DEBUG_REASON_MOST) {
        return trl_reader_fail(reader, "a pause's reason is of no code");
    }
    if (out->reason == 0U && !trl_read_u32(reader, &out->breakpoint)) {
        return false;
    }
    if (!trl_read_count(reader, TRL_FRAME_LEAST_BYTES, &out->frame_count)) {
        return false;
    }
    uint32_t room = 0U;
    out->frames = trl_reader_items(reader, out->frame_count, sizeof(trl_wire_frame), &room);
    for (uint32_t i = 0U; out->frames != NULL && i < out->frame_count; ++i) {
        out->frames = trl_reader_room(reader, out->frames, &room, i, sizeof(trl_wire_frame));
        if (out->frames == NULL || !read_frame(reader, &out->frames[i])) {
            return false;
        }
    }
    return out->frames != NULL;
}

bool trl_read_debugged(trl_reader *reader, trl_debugged *out) {
    if (!trl_read_u8(reader, &out->what)) {
        return false;
    }
    switch (out->what) {
    case TRL_DEBUGGED_CONNECTION:
        return trl_read_u64(reader, &out->connection);
    case TRL_DEBUGGED_BREAKPOINT:
        return read_breakpoint(reader, &out->breakpoint);
    case TRL_DEBUGGED_BREAKPOINTS:
        return read_breakpoints(reader, out);
    case TRL_DEBUGGED_STACK:
        if (!trl_read_flag(reader, &out->paused)) {
            return false;
        }
        return !out->paused || read_pause(reader, &out->pause);
    default:
        return trl_reader_fail(reader, "no Debugged reply is of this kind");
    }
}

bool trl_read_event(trl_reader *reader, trl_event *out) {
    if (!trl_read_u8(reader, &out->what)) {
        return false;
    }
    if (out->what == TRL_EVENT_PAUSED) {
        return read_pause(reader, &out->pause);
    }
    return out->what <= TRL_EVENT_ENDED || trl_reader_fail(reader, "no Event is of this kind");
}
