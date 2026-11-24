/**
 * @file replies.c
 * @brief Replies decoded as turborel-protocol's `reply.rs` decodes them
 * [PROTOCOL Replies]: each body read whole, its tail in order, nothing
 * left after it.
 */
#include "codec/replies.h"

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/debug.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/int128.h"
#include "codec/kinds.h"
#include "codec/reader.h"
#include "codec/tail.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The fewest bytes a row takes: its count; a condition: its bytes' count. */
#define TRL_ROW_LEAST_BYTES 4U
#define TRL_CONDITION_LEAST_BYTES 4U

/* Reads Ready: authorization, key, capabilities, server [PROTOCOL Ready 'R']. */
static bool read_ready(trl_reader *reader, trl_reply *out) {
    return trl_read_string(reader, &out->as.ready.authorization) &&
           trl_read_raw(reader, TRL_KEY_BYTES, &out->as.ready.key) &&
           trl_read_strings(reader, &out->as.ready.capabilities) && trl_read_string(reader, &out->as.ready.server);
}

/* The fewest bytes of a column's declared type: two empty strings. */
#define TRL_DECLARED_LEAST_BYTES 8U

/* Reads tag 19: the type each of `columns` was declared with, one for
   each [PROTOCOL Declared types]. */
static bool declared_read(trl_reader *reader, trl_column *columns, uint32_t count) {
    uint32_t told = 0U;
    if (!trl_read_count(reader, TRL_DECLARED_LEAST_BYTES, &told)) {
        return false;
    }
    if (told != count) {
        return trl_reader_fail(reader, "its declared types are not one for each column");
    }
    for (uint32_t i = 0U; i < count; ++i) {
        if (!trl_read_string(reader, &columns[i].declared_name) ||
            !trl_read_string(reader, &columns[i].declared_written)) {
            return false;
        }
        columns[i].declared = true;
    }
    return true;
}

/* Reads the tail of a Description: of tag 19, the columns' declared types. */
static bool read_declared(trl_reader *reader, trl_column *columns, uint32_t count) {
    trl_tail tail = {0U};
    uint8_t tag = 0U;
    while (trl_tail_next(reader, &tail, TRL_TAG_BIT(TRL_TAIL_DECLARED), &tag) && tag != 0U) {
        if (!declared_read(reader, columns, count)) {
            return false;
        }
    }
    return !reader->failed;
}

/* Reads tag 20 of a Prepared: the procedure's specific schema and name,
   then each `?` [PROTOCOL Parameter modes]. */
static bool read_called(trl_reader *reader, trl_reply *out) {
    out->as.prepared.called = true;
    return trl_read_string(reader, &out->as.prepared.procedure_schema) &&
           trl_read_string(reader, &out->as.prepared.procedure_name) &&
           trl_read_markers(reader, &out->as.prepared.markers, &out->as.prepared.marker_count);
}

/* Reads Prepared: statement, parameters, columns, then its tail, tags 19
   and 20 [PROTOCOL Prepared 'P']. */
static bool read_prepared(trl_reader *reader, trl_reply *out) {
    if (!trl_read_u32(reader, &out->as.prepared.statement) ||
        !trl_read_parameters(reader, &out->as.prepared.parameters, &out->as.prepared.parameter_count) ||
        !trl_read_columns(reader, &out->as.prepared.columns, &out->as.prepared.column_count)) {
        return false;
    }
    trl_tail tail = {0U};
    uint8_t tag = 0U;
    uint32_t taken = TRL_TAG_BIT(TRL_TAIL_DECLARED) | TRL_TAG_BIT(TRL_TAIL_CALLED);
    while (trl_tail_next(reader, &tail, taken, &tag) && tag != 0U) {
        bool read = tag == TRL_TAIL_CALLED
                        ? read_called(reader, out)
                        : declared_read(reader, out->as.prepared.columns, out->as.prepared.column_count);
        if (!read) {
            return false;
        }
    }
    return !reader->failed;
}

/* Reads Rows: a list of rows [PROTOCOL Rows 'B']. */
static bool read_rows(trl_reader *reader, trl_reply *out) {
    uint32_t count = 0U;
    if (!trl_read_count(reader, TRL_ROW_LEAST_BYTES, &count)) {
        return false;
    }
    uint32_t room = 0U;
    out->as.rows.rows = trl_reader_items(reader, count, sizeof(trl_row), &room);
    out->as.rows.count = count;
    for (uint32_t i = 0U; i < count; ++i) {
        out->as.rows.rows = trl_reader_room(reader, out->as.rows.rows, &room, i, sizeof(trl_row));
        if (out->as.rows.rows == NULL) {
            return false;
        }
        trl_row *row = &out->as.rows.rows[i];
        if (!trl_read_row(reader, &row->values, &row->count)) {
            return false;
        }
    }
    return true;
}

/* Reads one condition: a failure written as bytes, its tail ending where
   they do [PROTOCOL Complete 'K']. */
static bool read_condition(trl_reader *reader, trl_wire_failure *out) {
    trl_slice bytes = {NULL, 0U};
    if (!trl_read_bytes(reader, &bytes)) {
        return false;
    }
    trl_codec_error error = {0, "", ""};
    trl_reader inner;
    trl_reader_init(&inner, bytes.bytes, bytes.length, reader->arena, &error);
    inner.what = "condition";
    if (trl_read_failure(&inner, out) && trl_reader_finish(&inner)) {
        return true;
    }
    if (error.sqlcode == TRL_SQLCODE_RESOURCES) {
        reader->failed = true;
        trl_codec_no_memory(reader->error);
        return false;
    }
    /* A condition is read as stored data is: its failure the request's
       alone, not the connection's. */
    return trl_reader_value_fail(reader, TRL_SQLCODE_CORRUPT, TRL_SQLSTATE_CORRUPT, error.message);
}

/* Reads tag 17, the conditions. */
static bool read_conditions(trl_reader *reader, trl_completed *out) {
    if (!trl_read_count(reader, TRL_CONDITION_LEAST_BYTES, &out->condition_count)) {
        return false;
    }
    uint32_t room = 0U;
    out->conditions = trl_reader_items(reader, out->condition_count, sizeof(trl_wire_failure), &room);
    for (uint32_t i = 0U; out->conditions != NULL && i < out->condition_count; ++i) {
        out->conditions = trl_reader_room(reader, out->conditions, &room, i, sizeof(trl_wire_failure));
        if (out->conditions == NULL || !read_condition(reader, &out->conditions[i])) {
            return false;
        }
    }
    return out->conditions != NULL;
}

/* Reads one field of a Complete's tail, of tag `tag`. */
static bool read_complete_field(trl_reader *reader, uint8_t tag, trl_completed *out) {
    switch (tag) {
    case TRL_TAIL_POSITION:
        out->has_position = true;
        return trl_read_u64(reader, &out->position);
    case TRL_TAIL_MORE:
        out->more = true;
        return true;
    case TRL_TAIL_ROWS:
        out->has_rows = true;
        return trl_read_u64(reader, &out->rows);
    case TRL_TAIL_GENERATED:
        return trl_read_strings(reader, &out->generated);
    case TRL_TAIL_CONDITIONS:
        return read_conditions(reader, out);
    default:
        out->has_time = true;
        return trl_read_u64(reader, &out->time[TRL_TIME_TOTAL]) && trl_read_u64(reader, &out->time[TRL_TIME_QUEUED]) &&
               trl_read_u64(reader, &out->time[TRL_TIME_LOCKS]);
    }
}

/* Reads Complete: SQLCODE, SQLSTATE, active, then its tail. */
static bool read_complete(trl_reader *reader, trl_completed *out) {
    uint32_t sqlcode = 0U;
    if (!trl_read_u32(reader, &sqlcode) || !trl_read_string(reader, &out->sqlstate) ||
        !trl_read_flag(reader, &out->active)) {
        return false;
    }
    out->sqlcode = trl_bits_i32(sqlcode);
    const uint32_t taken = TRL_TAG_BIT(TRL_TAIL_POSITION) | TRL_TAG_BIT(TRL_TAIL_MORE) | TRL_TAG_BIT(TRL_TAIL_ROWS) |
                           TRL_TAG_BIT(TRL_TAIL_GENERATED) | TRL_TAG_BIT(TRL_TAIL_CONDITIONS) |
                           TRL_TAG_BIT(TRL_TAIL_TIME);
    trl_tail tail = {0U};
    uint8_t tag = 0U;
    while (trl_tail_next(reader, &tail, taken, &tag) && tag != 0U) {
        if (!read_complete_field(reader, tag, out)) {
            return false;
        }
    }
    return !reader->failed;
}

bool trl_completed_head(uint8_t kind, trl_slice body, trl_arena *arena, trl_completed *out) {
    if (kind != TRL_KIND_COMPLETE) {
        return false;
    }
    trl_codec_error error = {0, "", ""};
    trl_reader reader;
    trl_reader_init(&reader, body.bytes, body.length, arena, &error);
    memset(out, 0, sizeof *out);
    uint32_t sqlcode = 0U;
    if (!trl_read_u32(&reader, &sqlcode) || !trl_read_string(&reader, &out->sqlstate) ||
        !trl_read_flag(&reader, &out->active)) {
        return false;
    }
    out->sqlcode = trl_bits_i32(sqlcode);
    /* The fields before any a client may fail to take, a condition. */
    const uint32_t taken = TRL_TAG_BIT(TRL_TAIL_POSITION) | TRL_TAG_BIT(TRL_TAIL_MORE) | TRL_TAG_BIT(TRL_TAIL_ROWS) |
                           TRL_TAG_BIT(TRL_TAIL_GENERATED) | TRL_TAG_BIT(TRL_TAIL_CONDITIONS) |
                           TRL_TAG_BIT(TRL_TAIL_TIME);
    trl_tail tail = {0U};
    uint8_t tag = 0U;
    while (trl_tail_next(&reader, &tail, taken, &tag) && (tag == TRL_TAIL_POSITION || tag == TRL_TAIL_MORE)) {
        if (tag == TRL_TAIL_MORE) {
            out->more = true;
        } else if (!trl_read_u64(&reader, &out->position)) {
            break;
        } else {
            out->has_position = true;
        }
    }
    return true;
}

bool trl_reply_ends_request(uint8_t kind, trl_slice body, trl_arena *arena) {
    /* An Event, as a Notice or a Status, never ends a request's replies. */
    if (kind == TRL_KIND_DESCRIPTION || kind == TRL_KIND_ROWS || kind == TRL_KIND_NOTICE || kind == TRL_KIND_STATUS ||
        kind == TRL_KIND_EVENT) {
        return false;
    }
    trl_completed head;
    return kind != TRL_KIND_COMPLETE || !trl_completed_head(kind, body, arena, &head) || !head.more;
}

/* Reads a Lob reply: data or a locator [PROTOCOL Lob 'L']. */
static bool read_lob(trl_reader *reader, trl_reply *out) {
    if (!trl_read_u8(reader, &out->as.lob.what)) {
        return false;
    }
    if (out->as.lob.what == TRL_LOB_DATA) {
        return trl_read_bytes(reader, &out->as.lob.data);
    }
    if (out->as.lob.what == TRL_LOB_LOCATOR) {
        return trl_read_u32(reader, &out->as.lob.locator);
    }
    return trl_reader_fail(reader, "no Lob reply is of this kind");
}

/* Reads the replies of the protocol's later form and those of no body. */
static bool read_other(trl_reader *reader, trl_reply *out) {
    switch (out->kind) {
    case TRL_KIND_LOB_REPLY:
        return read_lob(reader, out);
    case TRL_KIND_NOTICE:
        return trl_read_string(reader, &out->as.notice.sqlstate) && trl_read_string(reader, &out->as.notice.message) &&
               trl_read_diagnostics(reader, &out->as.notice.diagnostics);
    case TRL_KIND_STATUS:
        return trl_read_string(reader, &out->as.status.name) && trl_read_string(reader, &out->as.status.value);
    case TRL_KIND_SUSPENDED:
    case TRL_KIND_PONG:
        return true;
    case TRL_KIND_DEBUGGED:
        return trl_read_debugged(reader, &out->as.debugged);
    case TRL_KIND_EVENT:
        return trl_read_event(reader, &out->as.event);
    default:
        return trl_reader_fail(reader, "no reply is of this kind");
    }
}

/* Reads the body of a reply of the protocol's first form. */
static bool read_body(trl_reader *reader, trl_reply *out) {
    switch (out->kind) {
    case TRL_KIND_CHALLENGE:
        return trl_read_bytes(reader, &out->as.challenge);
    case TRL_KIND_READY:
        return read_ready(reader, out);
    case TRL_KIND_PREPARED:
        return read_prepared(reader, out);
    case TRL_KIND_DESCRIPTION:
        return trl_read_columns(reader, &out->as.description.columns, &out->as.description.count) &&
               read_declared(reader, out->as.description.columns, out->as.description.count);
    case TRL_KIND_ROWS:
        return read_rows(reader, out);
    case TRL_KIND_COMPLETE:
        return read_complete(reader, &out->as.complete);
    case TRL_KIND_ERROR:
        return trl_read_failure(reader, &out->as.error);
    default:
        return read_other(reader, out);
    }
}

bool trl_decode_reply(uint8_t kind, trl_slice body, trl_arena *arena, trl_reply *out, trl_codec_error *error) {
    memset(out, 0, sizeof *out);
    out->kind = kind;
    trl_reader reader;
    trl_reader_init(&reader, body.bytes, body.length, arena, error);
    return read_body(&reader, out) && trl_reader_finish(&reader);
}
