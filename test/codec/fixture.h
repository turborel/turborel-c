/**
 * @file fixture.h
 * @brief What the codec's tests share (RULES.md R-TEST-7): an arena and an
 * error for each case, decoding a reply from bytes the spec prints, and
 * comparing what was written with the bytes expected.
 */
#ifndef TRL_TEST_CODEC_FIXTURE_H
#define TRL_TEST_CODEC_FIXTURE_H

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/replies.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** A case's arena, error and buffer, freed by fixture_free(). */
typedef struct fixture {
    trl_arena arena;
    trl_codec_error error;
    trl_buffer buffer;
} fixture;

/** Readies a fixture. */
void fixture_init(fixture *f);

/** Frees what a fixture holds. */
void fixture_free(fixture *f);

/** The slice of a C string, its null not counted. */
trl_slice text_of(const char *text);

/** The slice of `length` bytes. */
trl_slice bytes_of(const uint8_t *bytes, size_t length);

/** Whether the buffer holds exactly `length` bytes, equal to `expected`. */
bool buffer_is(const trl_buffer *buffer, const uint8_t *expected, size_t length);

/** Whether a slice holds exactly the C string `text`. */
bool slice_is(trl_slice slice, const char *text);

/** Decodes a reply of `kind` from the fixture's buffer, as its body. */
bool decode_buffer(fixture *f, uint8_t kind, trl_reply *out);

/** Decodes a reply of `kind` from `length` bytes. */
bool decode_bytes(fixture *f, uint8_t kind, const uint8_t *body, size_t length, trl_reply *out);

/** Whether the fixture's error is a connection failure, -919 and 08006. */
bool failed_connection(const fixture *f);

/** Whether the case failed on a value it cannot take, its request's failure alone: SQLSTATE TB916, or 0A000. */
bool failed_value(const fixture *f);

/** Whether the fixture's error has the SQLCODE and SQLSTATE given. */
bool failed_with(const fixture *f, int32_t sqlcode, const char *sqlstate);

/** Empties the fixture's buffer and clears its error, for another try. */
void fixture_reset(fixture *f);

#endif
