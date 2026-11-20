/**
 * @file reader.c
 * @brief Fields read from a body, each bounded by what is left
 * [PROTOCOL Values and fields].
 */
#include "codec/reader.h"

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/int128.h"
#include "codec/utf8.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bits of one byte. */
#define TRL_BYTE_BITS 8U

void trl_reader_init(trl_reader *reader, const uint8_t *data, size_t length, trl_arena *arena, trl_codec_error *error) {
    reader->data = data;
    reader->length = length;
    reader->at = 0U;
    reader->depth = 0U;
    reader->what = "reply";
    reader->arena = arena;
    reader->error = error;
    reader->failed = false;
}

bool trl_reader_fail(trl_reader *reader, const char *why) {
    /* The error keeps the first failure; a decoder stops at it. */
    reader->failed = true;
    trl_codec_malformed(reader->error, reader->what, reader->at, why);
    return false;
}

bool trl_reader_value_fail(trl_reader *reader, int32_t sqlcode, const char *sqlstate, const char *why) {
    reader->failed = true;
    trl_codec_fail(reader->error, sqlcode, sqlstate, why);
    return false;
}

bool trl_reader_at_end(const trl_reader *reader) {
    return reader->at >= reader->length;
}

bool trl_reader_finish(trl_reader *reader) {
    return trl_reader_at_end(reader) || trl_reader_fail(reader, "bytes follow its end");
}

bool trl_read_raw(trl_reader *reader, size_t count, trl_slice *out) {
    if (count > reader->length - reader->at) {
        return trl_reader_fail(reader, "it ends too soon");
    }
    out->bytes = reader->data + reader->at;
    out->length = count;
    reader->at += count;
    return true;
}

/* Reads `count` bytes, least significant first, as an integer. */
static bool read_little(trl_reader *reader, unsigned int count, uint64_t *out) {
    trl_slice bytes = {NULL, 0U};
    if (!trl_read_raw(reader, count, &bytes)) {
        return false;
    }
    uint64_t value = 0U;
    for (unsigned int i = 0U; i < count; ++i) {
        value |= (uint64_t)bytes.bytes[i] << (i * TRL_BYTE_BITS);
    }
    *out = value;
    return true;
}

bool trl_read_u8(trl_reader *reader, uint8_t *out) {
    uint64_t value = 0U;
    if (!read_little(reader, 1U, &value)) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool trl_read_u32(trl_reader *reader, uint32_t *out) {
    uint64_t value = 0U;
    if (!read_little(reader, TRL_U32_BYTES, &value)) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

bool trl_read_u64(trl_reader *reader, uint64_t *out) {
    return read_little(reader, TRL_U64_BYTES, out);
}

bool trl_read_i128(trl_reader *reader, trl_int128 *out) {
    trl_slice bytes = {NULL, 0U};
    if (!trl_read_raw(reader, TRL_I128_BYTES, &bytes)) {
        return false;
    }
    *out = trl_int128_of_bytes(bytes.bytes);
    return true;
}

bool trl_read_flag(trl_reader *reader, bool *out) {
    uint8_t byte = 0U;
    if (!trl_read_u8(reader, &byte)) {
        return false;
    }
    if (byte > 1U) {
        reader->at -= 1U;
        return trl_reader_fail(reader, "a flag is neither 0 nor 1");
    }
    *out = byte == 1U;
    return true;
}

bool trl_read_bytes(trl_reader *reader, trl_slice *out) {
    uint32_t length = 0U;
    return trl_read_u32(reader, &length) && trl_read_raw(reader, length, out);
}

bool trl_read_string(trl_reader *reader, trl_slice *out) {
    if (!trl_read_bytes(reader, out)) {
        return false;
    }
    return trl_utf8_valid(out->bytes, out->length) || trl_reader_fail(reader, "a string is not UTF-8");
}

bool trl_read_count(trl_reader *reader, size_t least, uint32_t *out) {
    uint32_t count = 0U;
    if (!trl_read_u32(reader, &count)) {
        return false;
    }
    size_t left = reader->length - reader->at;
    if (least != 0U && count > left / least) {
        return trl_reader_fail(reader, "a count is more than the bytes that follow hold");
    }
    *out = count;
    return true;
}

void *trl_reader_alloc(trl_reader *reader, size_t count, size_t size) {
    void *memory = trl_arena_alloc(reader->arena, count, size);
    if (memory == NULL) {
        reader->failed = true;
        trl_codec_no_memory(reader->error);
    }
    return memory;
}

bool trl_reader_enter(trl_reader *reader) {
    if (reader->depth >= TRL_MAX_NESTING) {
        return trl_reader_fail(reader, "its arrays and rows are nested too deep");
    }
    reader->depth += 1U;
    return true;
}

void trl_reader_leave(trl_reader *reader) {
    reader->depth -= 1U;
}
