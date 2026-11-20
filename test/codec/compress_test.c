/**
 * @file compress_test.c
 * @brief Compressed frames: made only where shorter, read no further than
 * they declare, malformed ones failing the connection
 * [PROTOCOL Compressed frames].
 */
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/error.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/* The bytes of a body that compresses well. */
#define LONG_BODY 4096U

/* Appends a frame of `kind` whose body is `length` bytes of `byte`. */
static void put_frame(trl_buffer *b, uint8_t kind, size_t length, uint8_t byte) {
    size_t start = trl_frame_start(b, kind);
    for (size_t i = 0U; i < length; ++i) {
        trl_put_u8(b, byte);
    }
    trl_frame_finish(b, start);
}

/**
 * Verifies: [PROTOCOL Compressed frames] a frame at least the threshold
 * long is sent as a z frame of its kind, its body's length and a zlib
 * stream, which reads back as the frame; a shorter one goes as it is.
 */
static void frames_compress_and_read_back(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    fixture f;
    fixture_init(&f);
    trl_buffer frames;
    trl_buffer_init(&frames);
    put_frame(&frames, 'B', LONG_BODY, 'x');
    put_frame(&frames, 'S', 0U, 0U);
    TRL_CHECK(test,
              trl_compress_frames(&f.buffer, bytes_of(frames.data, frames.length), TRL_COMPRESS_THRESHOLD, &f.error));
    TRL_CHECK(test, f.buffer.data[0] == TRL_KIND_COMPRESSED && f.buffer.length < frames.length);
    const uint8_t suspended[] = {'S', 0, 0, 0, 0};
    TRL_CHECK(test, memcmp(f.buffer.data + f.buffer.length - sizeof suspended, suspended, sizeof suspended) == 0);
    trl_slice z = {f.buffer.data + TRL_FRAME_HEADER_BYTES, f.buffer.length - TRL_FRAME_HEADER_BYTES - sizeof suspended};
    trl_buffer body;
    trl_buffer_init(&body);
    uint8_t kind = 0U;
    TRL_CHECK(test, trl_decompress_frame(z, TRL_FRAME_MAX_BODY, &kind, &body, &f.error));
    TRL_CHECK(test, kind == 'B' && body.length == LONG_BODY && memcmp(body.data, frames.data + 5, LONG_BODY) == 0);
    trl_buffer_free(&body);
    trl_buffer_free(&frames);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Compressed frames] a threshold of the client's own:
 * a frame below it, or one compressing would not shorten, goes as it is.
 */
static void the_threshold_is_the_clients(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    fixture f;
    fixture_init(&f);
    trl_buffer frames;
    trl_buffer_init(&frames);
    put_frame(&frames, 'B', 64U, 'x');
    TRL_CHECK(test, trl_compress_frames(&f.buffer, bytes_of(frames.data, frames.length), 1000U, &f.error));
    TRL_CHECK(test, buffer_is(&f.buffer, frames.data, frames.length));
    fixture_reset(&f);
    TRL_CHECK(test, trl_compress_frames(&f.buffer, bytes_of(frames.data, frames.length), 10U, &f.error));
    TRL_CHECK(test, f.buffer.data[0] == TRL_KIND_COMPRESSED);
    fixture_reset(&f);
    trl_buffer_free(&frames);
    trl_buffer_init(&frames);
    const uint8_t random[] = {0x8F, 0x13, 0xC2, 0x7A, 0x01, 0xEE, 0x55, 0x90};
    size_t start = trl_frame_start(&frames, 'B');
    trl_put_raw(&frames, random, sizeof random);
    trl_frame_finish(&frames, start);
    TRL_CHECK(test, trl_compress_frames(&f.buffer, bytes_of(frames.data, frames.length), 0U, &f.error));
    TRL_CHECK(test, buffer_is(&f.buffer, frames.data, frames.length));
    trl_buffer_free(&frames);
    fixture_free(&f);
}

/* Whether inflating the z body `z` of `length` bytes, taking at most `max`,
   fails the connection. */
static bool inflate_fails(fixture *f, const uint8_t *z, size_t length, uint32_t max) {
    fixture_reset(f);
    trl_buffer body;
    trl_buffer_init(&body);
    uint8_t kind = 0U;
    bool inflated = trl_decompress_frame(bytes_of(z, length), max, &kind, &body, &f->error);
    trl_buffer_free(&body);
    return !inflated && failed_connection(f);
}

/**
 * Verifies: [PROTOCOL Compressed frames] a z frame holding another, one
 * declaring more than the longest body taken, one inflating to another
 * length or past it, or a stream not of zlib, fails the connection.
 */
static void malformed_z_frames_fail(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    fixture f;
    fixture_init(&f);
    trl_buffer frames;
    trl_buffer_init(&frames);
    put_frame(&frames, 'B', LONG_BODY, 'x');
    TRL_CHECK(test, trl_compress_frames(&f.buffer, bytes_of(frames.data, frames.length), 0U, &f.error));
    uint8_t z[256];
    memset(z, 0, sizeof z);
    size_t length = f.buffer.length - TRL_FRAME_HEADER_BYTES;
    TRL_CHECK(test, length < sizeof z);
    memcpy(z, f.buffer.data + TRL_FRAME_HEADER_BYTES, length);
    TRL_CHECK(test, inflate_fails(&f, z, length, LONG_BODY - 1U));
    uint8_t changed[256];
    memcpy(changed, z, length);
    changed[0] = TRL_KIND_COMPRESSED;
    TRL_CHECK(test, inflate_fails(&f, changed, length, TRL_FRAME_MAX_BODY));
    const uint32_t lengths[] = {LONG_BODY - 1U, LONG_BODY + 1U};
    for (size_t i = 0U; i < TRL_TEST_COUNT(lengths); ++i) {
        memcpy(changed, z, length);
        changed[1] = (uint8_t)(lengths[i] & 0xFFU);
        changed[2] = (uint8_t)(lengths[i] >> 8U);
        TRL_CHECK(test, inflate_fails(&f, changed, length, TRL_FRAME_MAX_BODY));
    }
    memcpy(changed, z, length);
    changed[5] ^= 0xFFU;
    TRL_CHECK(test, inflate_fails(&f, changed, length, TRL_FRAME_MAX_BODY));
    memcpy(changed, z, sizeof z);
    TRL_CHECK(test, inflate_fails(&f, changed, length + 1U, TRL_FRAME_MAX_BODY));
    trl_buffer_free(&frames);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Compressed frames] a z frame holding a frame of no
 * body, as Pong, inflates to it, as turborel-protocol inflates one; a
 * stream cut short of what it declares fails, its output grown only as
 * far as it inflated.
 */
static void an_empty_frame_inflates(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    fixture f;
    fixture_init(&f);
    static const uint8_t empty[] = {'G', 0U, 0U, 0U, 0U, 0x78U, 0x9CU, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};
    trl_buffer body;
    trl_buffer_init(&body);
    uint8_t kind = 0U;
    TRL_CHECK(test, trl_decompress_frame(bytes_of(empty, sizeof empty), TRL_FRAME_MAX_BODY, &kind, &body, &f.error));
    TRL_CHECK(test, kind == 'G' && body.length == 0U);
    trl_buffer_free(&body);
    uint8_t cut[sizeof empty];
    memcpy(cut, empty, sizeof empty);
    cut[3] = 0xFFU;
    TRL_CHECK(test, inflate_fails(&f, cut, sizeof cut, TRL_FRAME_MAX_BODY));
    fixture_free(&f);
}

/**
 * Verifies: [DESIGN] D12 a library built without zlib refuses compressed
 * frames, saying so; one built with it reads them.
 */
static void zlib_is_needed(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t frame[] = {'S', 0, 0, 0, 0};
    bool done = trl_compress_frames(&f.buffer, bytes_of(frame, sizeof frame), 0U, &f.error);
    if (trl_compression_built()) {
        TRL_CHECK(test, done && buffer_is(&f.buffer, frame, sizeof frame));
    } else {
        TRL_CHECK(test, !done && failed_with(&f, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED));
    }
    fixture_free(&f);
}

/**
 * Verifies: [DESIGN] D9 frames compressed, or inflated, into a buffer
 * bounded at any size are whole or fail as memory not had.
 */
static void compression_within_any_buffer(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    fixture source;
    fixture_init(&source);
    size_t start = trl_frame_start(&source.buffer, 'B');
    for (unsigned int i = 0U; i < 2048U; ++i) {
        trl_put_u8(&source.buffer, (uint8_t)'y');
    }
    trl_frame_finish(&source.buffer, start);
    bool whole = false;
    for (size_t limit = 1U; limit <= 9000U && !whole; limit += 7U) {
        fixture f;
        fixture_init(&f);
        f.buffer.limit = limit;
        whole = trl_compress_frames(&f.buffer, bytes_of(source.buffer.data, source.buffer.length), 0U, &f.error);
        TRL_CHECK(test, whole || failed_with(&f, TRL_SQLCODE_RESOURCES, TRL_SQLSTATE_RESOURCES));
        if (whole) {
            trl_buffer body;
            trl_buffer_init(&body);
            body.limit = 1000U;
            uint8_t kind = 0U;
            trl_slice z = {f.buffer.data + 5, f.buffer.length - 5U};
            TRL_CHECK(test, !trl_decompress_frame(z, 1U << 24U, &kind, &body, &f.error) &&
                                failed_with(&f, TRL_SQLCODE_RESOURCES, TRL_SQLSTATE_RESOURCES));
            trl_buffer_free(&body);
        }
        fixture_free(&f);
    }
    TRL_CHECK(test, whole);
    fixture_free(&source);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(frames_compress_and_read_back),
        TRL_TEST_CASE(the_threshold_is_the_clients),
        TRL_TEST_CASE(malformed_z_frames_fail),
        TRL_TEST_CASE(an_empty_frame_inflates),
        TRL_TEST_CASE(zlib_is_needed),
        TRL_TEST_CASE(compression_within_any_buffer),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
