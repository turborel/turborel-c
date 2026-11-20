/**
 * @file fixture.c
 * @brief What the codec's tests share (RULES.md R-TEST-7).
 */
#include "fixture.h"

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/replies.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void fixture_init(fixture *f) {
    trl_arena_init(&f->arena);
    memset(&f->error, 0, sizeof f->error);
    trl_buffer_init(&f->buffer);
}

void fixture_free(fixture *f) {
    trl_arena_free(&f->arena);
    trl_buffer_free(&f->buffer);
}

void fixture_reset(fixture *f) {
    f->buffer.length = 0U;
    f->buffer.state = TRL_BUFFER_OK;
    memset(&f->error, 0, sizeof f->error);
}

trl_slice text_of(const char *text) {
    trl_slice slice = {(const uint8_t *)text, strlen(text)};
    return slice;
}

trl_slice bytes_of(const uint8_t *bytes, size_t length) {
    trl_slice slice = {bytes, length};
    return slice;
}

bool buffer_is(const trl_buffer *buffer, const uint8_t *expected, size_t length) {
    return buffer->state == TRL_BUFFER_OK && buffer->length == length &&
           (length == 0U || memcmp(buffer->data, expected, length) == 0);
}

bool slice_is(trl_slice slice, const char *text) {
    size_t length = strlen(text);
    return slice.length == length && (length == 0U || memcmp(slice.bytes, text, length) == 0);
}

bool decode_bytes(fixture *f, uint8_t kind, const uint8_t *body, size_t length, trl_reply *out) {
    return trl_decode_reply(kind, bytes_of(body, length), &f->arena, out, &f->error);
}

bool decode_buffer(fixture *f, uint8_t kind, trl_reply *out) {
    return decode_bytes(f, kind, f->buffer.data, f->buffer.length, out);
}

bool failed_with(const fixture *f, int32_t sqlcode, const char *sqlstate) {
    return f->error.sqlcode == sqlcode && strcmp(f->error.sqlstate, sqlstate) == 0;
}

bool failed_connection(const fixture *f) {
    return failed_with(f, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_CONNECTION_FAILURE);
}

bool failed_value(const fixture *f) {
    return failed_with(f, TRL_SQLCODE_CORRUPT, TRL_SQLSTATE_CORRUPT) ||
           failed_with(f, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED);
}
