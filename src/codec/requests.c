/**
 * @file requests.c
 * @brief Requests encoded as turborel-protocol's `request.rs` and
 * `request/added.rs` encode them [PROTOCOL Requests]: the fields of each,
 * then its tail in increasing order of tag, written only where it says
 * something.
 */
#include "codec/requests.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ends the frame begun at `start` if `written`; else takes the frame back
   out of the buffer, so it is left as it was. */
static bool finish(trl_buffer *buffer, size_t start, bool written, trl_codec_error *error) {
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

/* Writes tail tag 3 and the cursor, unless it is 0. */
static void put_cursor(trl_buffer *buffer, uint32_t cursor) {
    if (cursor != 0U) {
        trl_put_u8(buffer, TRL_TAIL_CURSOR);
        trl_put_u32(buffer, cursor);
    }
}

bool trl_encode_hello(trl_buffer *buffer, const trl_hello *hello, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_HELLO);
    trl_put_u32(buffer, hello->version);
    bool written = trl_write_string(buffer, hello->user, error) && trl_write_string(buffer, hello->mechanism, error);
    if (written) {
        trl_put_bytes(buffer, hello->initial);
        written = trl_write_strings(buffer, hello->capabilities, hello->capability_count, error) &&
                  trl_write_attributes(buffer, hello->attributes, hello->attribute_count, error);
    }
    return finish(buffer, start, written, error);
}

bool trl_encode_answer(trl_buffer *buffer, trl_slice answer, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_ANSWER);
    trl_put_bytes(buffer, answer);
    return finish(buffer, start, true, error);
}

bool trl_encode_query(trl_buffer *buffer, const trl_query_request *query, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_QUERY);
    bool written = trl_write_string(buffer, query->sql, error);
    if (written) {
        trl_put_flag(buffer, query->ends);
        written = trl_write_attributes(buffer, query->attributes, query->attribute_count, error);
    }
    if (written && query->multi) {
        trl_put_u8(buffer, TRL_TAIL_MULTI);
    }
    return finish(buffer, start, written, error);
}

bool trl_encode_prepare(trl_buffer *buffer, uint32_t statement, trl_slice sql, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_PREPARE);
    trl_put_u32(buffer, statement);
    return finish(buffer, start, trl_write_string(buffer, sql, error), error);
}

bool trl_encode_execute(trl_buffer *buffer, const trl_execute_request *execute, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_EXECUTE);
    trl_put_u32(buffer, execute->statement);
    bool written = trl_write_row(buffer, execute->values, execute->value_count, error);
    if (written) {
        trl_put_u32(buffer, execute->rows);
        trl_put_flag(buffer, execute->ends);
        written = trl_write_attributes(buffer, execute->attributes, execute->attribute_count, error);
    }
    if (written) {
        put_cursor(buffer, execute->cursor);
    }
    return finish(buffer, start, written, error);
}

bool trl_encode_fetch(trl_buffer *buffer, uint32_t statement, uint32_t rows, uint32_t cursor, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_FETCH);
    trl_put_u32(buffer, statement);
    trl_put_u32(buffer, rows);
    put_cursor(buffer, cursor);
    return finish(buffer, start, true, error);
}

bool trl_encode_close(trl_buffer *buffer, uint32_t statement, uint32_t cursor, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_CLOSE);
    trl_put_u32(buffer, statement);
    put_cursor(buffer, cursor);
    return finish(buffer, start, true, error);
}

bool trl_encode_deallocate(trl_buffer *buffer, uint32_t statement, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_DEALLOCATE);
    trl_put_u32(buffer, statement);
    return finish(buffer, start, true, error);
}

/* Writes a Lob request's op and its fields. */
static bool put_lob(trl_buffer *buffer, const trl_lob_request *request, trl_codec_error *error) {
    trl_put_u8(buffer, request->op);
    switch (request->op) {
    case TRL_LOB_READ:
        trl_put_u32(buffer, request->locator);
        trl_put_u64(buffer, request->from);
        trl_put_u64(buffer, request->count);
        trl_put_flag(buffer, request->characters);
        return true;
    case TRL_LOB_CREATE:
        if (request->kind > TRL_LOB_BINARY) {
            break;
        }
        trl_put_u8(buffer, request->kind);
        return true;
    case TRL_LOB_APPEND:
        trl_put_u32(buffer, request->locator);
        trl_put_bytes(buffer, request->bytes);
        return true;
    case TRL_LOB_FREE:
    case TRL_LOB_HOLD:
        trl_put_u32(buffer, request->locator);
        return true;
    default:
        break;
    }
    trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "no Lob request is of this op or kind");
    return false;
}

bool trl_encode_lob(trl_buffer *buffer, const trl_lob_request *request, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_LOB_REQUEST);
    return finish(buffer, start, put_lob(buffer, request, error), error);
}

bool trl_encode_cancel(trl_buffer *buffer, trl_slice key, trl_codec_error *error) {
    if (key.length != TRL_KEY_BYTES || key.bytes == NULL) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "a cancel key is 16 bytes");
        return false;
    }
    size_t start = trl_frame_start(buffer, TRL_KIND_CANCEL);
    trl_put_raw(buffer, key.bytes, key.length);
    return finish(buffer, start, true, error);
}

bool trl_encode_bare(trl_buffer *buffer, uint8_t kind, trl_codec_error *error) {
    if (kind != TRL_KIND_TERMINATE && kind != TRL_KIND_PING && kind != TRL_KIND_RESET) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "no request of this kind is a header alone");
        return false;
    }
    size_t start = trl_frame_start(buffer, kind);
    return finish(buffer, start, true, error);
}

bool trl_encode_describe(trl_buffer *buffer, uint8_t what, uint32_t statement, uint32_t cursor,
                         trl_codec_error *error) {
    if (what > TRL_DESCRIBE_CURSOR || (what == TRL_DESCRIBE_STATEMENT && cursor != 0U)) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API,
                       "Describe describes a statement, 0, of cursor 0, or a cursor, 1");
        return false;
    }
    size_t start = trl_frame_start(buffer, TRL_KIND_DESCRIBE);
    trl_put_u8(buffer, what);
    trl_put_u32(buffer, statement);
    trl_put_u32(buffer, cursor);
    return finish(buffer, start, true, error);
}

bool trl_encode_change_user(trl_buffer *buffer, trl_slice user, trl_slice mechanism, trl_slice initial,
                            trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_CHANGE_USER);
    bool written = trl_write_string(buffer, user, error) && trl_write_string(buffer, mechanism, error);
    if (written) {
        trl_put_bytes(buffer, initial);
    }
    return finish(buffer, start, written, error);
}

bool trl_encode_copy_in(trl_buffer *buffer, const trl_copy_in_request *copy, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_COPY_IN);
    bool written = trl_write_string(buffer, copy->table, error) &&
                   trl_write_strings(buffer, copy->columns, copy->column_count, error);
    if (written) {
        trl_put_flag(buffer, copy->ends);
    }
    return finish(buffer, start, written, error);
}

bool trl_encode_copy_data(trl_buffer *buffer, const trl_row_values *rows, size_t count, trl_codec_error *error) {
    if (count > 0U && rows == NULL) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "a CopyData has no rows");
        return false;
    }
    size_t start = trl_frame_start(buffer, TRL_KIND_COPY_DATA);
    trl_put_count(buffer, count);
    bool written = true;
    for (size_t i = 0U; i < count && written; ++i) {
        written = trl_write_row(buffer, rows[i].values, rows[i].count, error);
    }
    return finish(buffer, start, written, error);
}

bool trl_encode_copy_rows(trl_buffer *buffer, trl_slice rows, size_t count, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_COPY_DATA);
    trl_put_count(buffer, count);
    trl_put_raw(buffer, rows.bytes, rows.length);
    return finish(buffer, start, true, error);
}

bool trl_encode_copy_end(trl_buffer *buffer, bool abandon, trl_codec_error *error) {
    size_t start = trl_frame_start(buffer, TRL_KIND_COPY_END);
    trl_put_flag(buffer, abandon);
    return finish(buffer, start, true, error);
}
