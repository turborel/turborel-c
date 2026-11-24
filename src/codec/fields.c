/**
 * @file fields.c
 * @brief Strings, columns, parameters, failures and diagnostics read as
 * turborel-protocol's `fields.rs` and `diagnostics.rs` read them
 * [PROTOCOL Composite fields].
 */
#include "codec/fields.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/int128.h"
#include "codec/reader.h"
#include "codec/tail.h"
#include "codec/types.h"
#include "codec/utf8.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The fewest bytes a string takes: its length. */
#define TRL_STRING_LEAST_BYTES 4U
/* The fewest bytes a column takes: its named flag, a type's tag, its not
   null flag; and a parameter: its varying flag and a type's tag. */
#define TRL_COLUMN_LEAST_BYTES 3U
#define TRL_PARAMETER_LEAST_BYTES 2U

bool trl_read_strings(trl_reader *reader, trl_strings *out) {
    if (!trl_read_count(reader, TRL_STRING_LEAST_BYTES, &out->count)) {
        return false;
    }
    uint32_t room = 0U;
    out->items = trl_reader_items(reader, out->count, sizeof(trl_slice), &room);
    for (uint32_t i = 0U; out->items != NULL && i < out->count; ++i) {
        out->items = trl_reader_room(reader, out->items, &room, i, sizeof(trl_slice));
        if (out->items == NULL || !trl_read_string(reader, &out->items[i])) {
            return false;
        }
    }
    return out->items != NULL;
}

bool trl_write_string(trl_buffer *buffer, trl_slice text, trl_codec_error *error) {
    if (!trl_utf8_valid(text.bytes, text.length)) {
        trl_codec_fail(error, TRL_SQLCODE_DATA, TRL_SQLSTATE_NOT_UTF8, "a string to send is not UTF-8");
        return false;
    }
    trl_put_bytes(buffer, text);
    return true;
}

bool trl_write_strings(trl_buffer *buffer, const trl_slice *strings, size_t count, trl_codec_error *error) {
    if (count > 0U && strings == NULL) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, "a list of strings to send has none");
        return false;
    }
    trl_put_count(buffer, count);
    for (size_t i = 0U; i < count; ++i) {
        if (!trl_write_string(buffer, strings[i], error)) {
            return false;
        }
    }
    return true;
}

/* Reads a column: named flag, name if named, type, not null flag. */
static bool read_column(trl_reader *reader, trl_column *out) {
    if (!trl_read_flag(reader, &out->named) || (out->named && !trl_read_string(reader, &out->name))) {
        return false;
    }
    return trl_read_data_type(reader, &out->type) && trl_read_flag(reader, &out->not_null);
}

bool trl_read_columns(trl_reader *reader, trl_column **out, uint32_t *count) {
    if (!trl_read_count(reader, TRL_COLUMN_LEAST_BYTES, count)) {
        return false;
    }
    uint32_t room = 0U;
    *out = trl_reader_items(reader, *count, sizeof(trl_column), &room);
    for (uint32_t i = 0U; *out != NULL && i < *count; ++i) {
        *out = trl_reader_room(reader, *out, &room, i, sizeof(trl_column));
        if (*out == NULL || !read_column(reader, &(*out)[i])) {
            return false;
        }
    }
    return *out != NULL;
}

bool trl_read_parameters(trl_reader *reader, trl_parameter **out, uint32_t *count) {
    if (!trl_read_count(reader, TRL_PARAMETER_LEAST_BYTES, count)) {
        return false;
    }
    uint32_t room = 0U;
    *out = trl_reader_items(reader, *count, sizeof(trl_parameter), &room);
    for (uint32_t i = 0U; *out != NULL && i < *count; ++i) {
        *out = trl_reader_room(reader, *out, &room, i, sizeof(trl_parameter));
        if (*out == NULL) {
            return false;
        }
        trl_parameter *parameter = &(*out)[i];
        if (!trl_read_flag(reader, &parameter->varying) || !trl_read_data_type(reader, &parameter->type)) {
            return false;
        }
    }
    return *out != NULL;
}

/* The fewest bytes of a marker: its mode, ordinal and parameter. */
#define TRL_MARKER_LEAST_BYTES (1U + 4U + TRL_PARAMETER_LEAST_BYTES)

/* Whether `mode` is one of tail 20's codes. */
static bool mode_known(uint8_t mode) {
    return mode == TRL_MODE_IN || mode == TRL_MODE_INOUT || mode == TRL_MODE_OUT;
}

bool trl_read_markers(trl_reader *reader, trl_marker **out, uint32_t *count) {
    if (!trl_read_count(reader, TRL_MARKER_LEAST_BYTES, count)) {
        return false;
    }
    uint32_t room = 0U;
    *out = trl_reader_items(reader, *count, sizeof(trl_marker), &room);
    for (uint32_t i = 0U; *out != NULL && i < *count; ++i) {
        *out = trl_reader_room(reader, *out, &room, i, sizeof(trl_marker));
        if (*out == NULL) {
            return false;
        }
        trl_marker *marker = &(*out)[i];
        if (!trl_read_u8(reader, &marker->mode) || !trl_read_u32(reader, &marker->ordinal) ||
            !trl_read_flag(reader, &marker->parameter.varying) ||
            !trl_read_data_type(reader, &marker->parameter.type)) {
            return false;
        }
        if (!mode_known(marker->mode)) {
            return trl_reader_fail(reader, "a parameter's mode is of no code tail 20 has");
        }
        if (marker->ordinal == 0U) {
            return trl_reader_fail(reader, "a parameter's ordinal position is 0");
        }
    }
    return *out != NULL;
}

bool trl_read_diagnostics(trl_reader *reader, trl_diagnostics *out) {
    uint32_t taken = 0U;
    for (unsigned int tag = TRL_TAIL_SEVERITY; tag <= TRL_TAIL_CONSTRAINT; ++tag) {
        taken |= TRL_TAG_BIT(tag);
    }
    trl_tail tail = {0U};
    uint8_t tag = 0U;
    while (trl_tail_next(reader, &tail, taken, &tag) && tag != 0U) {
        unsigned int at = (unsigned int)tag - TRL_TAIL_SEVERITY;
        if (!trl_read_string(reader, &out->text[at])) {
            return false;
        }
        out->told[at] = true;
    }
    return !reader->failed;
}

bool trl_read_failure(trl_reader *reader, trl_wire_failure *out) {
    uint32_t sqlcode = 0U;
    if (!trl_read_u32(reader, &sqlcode) || !trl_read_string(reader, &out->sqlstate) ||
        !trl_read_string(reader, &out->message)) {
        return false;
    }
    out->sqlcode = trl_bits_i32(sqlcode);
    if (!trl_read_flag(reader, &out->cited) || (out->cited && !trl_read_string(reader, &out->rule))) {
        return false;
    }
    if (!trl_read_flag(reader, &out->placed) || (out->placed && !trl_read_u64(reader, &out->position))) {
        return false;
    }
    return trl_read_flag(reader, &out->active) && trl_read_diagnostics(reader, &out->diagnostics);
}
