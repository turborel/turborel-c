/**
 * @file buffer.c
 * @brief Fields appended to a growing buffer, little-endian
 * [PROTOCOL Values and fields].
 */
#include "codec/buffer.h"

#include "codec/error.h"
#include "codec/frame.h"
#include "codec/int128.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The least bytes a buffer first holds; it doubles as it fills. */
#define TRL_BUFFER_FIRST_BYTES 256U
/* How a buffer grows when it fills. */
#define TRL_BUFFER_GROWTH 2U
/* The bits of one byte, and its mask. */
#define TRL_BYTE_BITS 8U
#define TRL_BYTE_MASK 0xFFU

void trl_buffer_init(trl_buffer *buffer) {
    buffer->data = NULL;
    buffer->length = 0U;
    buffer->capacity = 0U;
    buffer->state = TRL_BUFFER_OK;
    buffer->limit = 0U;
}

void trl_buffer_free(trl_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }
    size_t limit = buffer->limit;
    free(buffer->data);
    trl_buffer_init(buffer);
    buffer->limit = limit;
}

bool trl_buffer_ok(const trl_buffer *buffer, trl_codec_error *error) {
    if (buffer->state == TRL_BUFFER_NO_MEMORY) {
        trl_codec_no_memory(error);
    } else if (buffer->state == TRL_BUFFER_TOO_LONG) {
        trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API,
                       "a field or a message is longer than the protocol's lengths hold");
    }
    return buffer->state == TRL_BUFFER_OK;
}

/* Room for `more` bytes past those written; false, the buffer failed, if
   none can be had. */
static bool reserve(trl_buffer *buffer, size_t more) {
    if (buffer->state != TRL_BUFFER_OK) {
        return false;
    }
    if (buffer->capacity - buffer->length >= more) {
        return true;
    }
    if (more > SIZE_MAX - buffer->length) {
        buffer->state = TRL_BUFFER_NO_MEMORY;
        return false;
    }
    size_t needed = buffer->length + more;
    if (buffer->limit != 0U && needed > buffer->limit) {
        buffer->state = TRL_BUFFER_NO_MEMORY;
        return false;
    }
    size_t capacity = buffer->capacity == 0U ? TRL_BUFFER_FIRST_BYTES : buffer->capacity;
    while (capacity < needed) {
        capacity = capacity > SIZE_MAX / TRL_BUFFER_GROWTH ? needed : capacity * TRL_BUFFER_GROWTH;
    }
    if (buffer->limit != 0U && capacity > buffer->limit) {
        capacity = buffer->limit;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (grown == NULL) {
        buffer->state = TRL_BUFFER_NO_MEMORY;
        return false;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

void trl_put_raw(trl_buffer *buffer, const uint8_t *bytes, size_t length) {
    if (length == 0U || !reserve(buffer, length)) {
        return;
    }
    memcpy(buffer->data + buffer->length, bytes, length);
    buffer->length += length;
}

uint8_t *trl_put_space(trl_buffer *buffer, size_t length) {
    if (!reserve(buffer, length) || buffer->data == NULL) {
        return NULL;
    }
    uint8_t *at = buffer->data + buffer->length;
    buffer->length += length;
    return at;
}

/* Appends the low `count` bytes of `value`, least significant first. */
static void put_little(trl_buffer *buffer, uint64_t value, unsigned int count) {
    uint8_t bytes[TRL_U64_BYTES];
    for (unsigned int i = 0U; i < count; ++i) {
        bytes[i] = (uint8_t)((value >> (i * TRL_BYTE_BITS)) & TRL_BYTE_MASK);
    }
    trl_put_raw(buffer, bytes, count);
}

void trl_put_u8(trl_buffer *buffer, uint8_t value) {
    trl_put_raw(buffer, &value, 1U);
}

void trl_put_u32(trl_buffer *buffer, uint32_t value) {
    put_little(buffer, value, TRL_U32_BYTES);
}

void trl_put_u64(trl_buffer *buffer, uint64_t value) {
    put_little(buffer, value, TRL_U64_BYTES);
}

void trl_put_i128(trl_buffer *buffer, trl_int128 value) {
    uint8_t bytes[TRL_I128_BYTES];
    trl_int128_to_bytes(value, bytes);
    trl_put_raw(buffer, bytes, sizeof bytes);
}

void trl_put_flag(trl_buffer *buffer, bool set) {
    trl_put_u8(buffer, set ? 1U : 0U);
}

void trl_put_count(trl_buffer *buffer, size_t count) {
    if (count > UINT32_MAX) {
        if (buffer->state == TRL_BUFFER_OK) {
            buffer->state = TRL_BUFFER_TOO_LONG;
        }
        return;
    }
    trl_put_u32(buffer, (uint32_t)count);
}

void trl_put_bytes(trl_buffer *buffer, trl_slice bytes) {
    trl_put_count(buffer, bytes.length);
    trl_put_raw(buffer, bytes.bytes, bytes.length);
}

void trl_patch_u32(trl_buffer *buffer, size_t at, uint32_t value) {
    if (buffer->state != TRL_BUFFER_OK || at > buffer->length || buffer->length - at < TRL_U32_BYTES) {
        return;
    }
    for (unsigned int i = 0U; i < TRL_U32_BYTES; ++i) {
        buffer->data[at + i] = (uint8_t)((value >> (i * TRL_BYTE_BITS)) & TRL_BYTE_MASK);
    }
}

size_t trl_frame_start(trl_buffer *buffer, uint8_t kind) {
    size_t start = buffer->length;
    trl_put_u8(buffer, kind);
    trl_put_u32(buffer, 0U);
    return start;
}

void trl_frame_finish(trl_buffer *buffer, size_t start) {
    if (buffer->state != TRL_BUFFER_OK) {
        return;
    }
    size_t body = buffer->length - start - TRL_FRAME_HEADER_BYTES;
    if (body > UINT32_MAX) {
        buffer->state = TRL_BUFFER_TOO_LONG;
        return;
    }
    trl_frame_header header = {buffer->data[start], (uint32_t)body};
    (void)trl_frame_header_encode(header, buffer->data + start, TRL_FRAME_HEADER_BYTES);
}
