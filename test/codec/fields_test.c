/**
 * @file fields_test.c
 * @brief The protocol's fields, read and written [PROTOCOL Values and fields].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/int128.h"
#include "codec/reader.h"
#include "codec/utf8.h"
#include "fixture.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdint.h>
#include <string.h>

/* A reader of `length` bytes, in the fixture. */
static trl_reader reader_of(fixture *f, const uint8_t *bytes, size_t length) {
    trl_reader reader;
    trl_reader_init(&reader, bytes, length, &f->arena, &f->error);
    return reader;
}

/**
 * Verifies: [PROTOCOL Values and fields] u8, u32 and u64 are written and
 * read least significant byte first.
 */
static void integers_are_little_endian(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, 0xABU);
    trl_put_u32(&f.buffer, 0x01020304U);
    trl_put_u64(&f.buffer, 0x0102030405060708U);
    const uint8_t expected[] = {0xAB, 0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    trl_reader r = reader_of(&f, expected, sizeof expected);
    uint8_t a = 0U;
    uint32_t b = 0U;
    uint64_t c = 0U;
    TRL_CHECK(test, trl_read_u8(&r, &a) && trl_read_u32(&r, &b) && trl_read_u64(&r, &c));
    TRL_CHECK(test, a == 0xABU && b == 0x01020304U && c == 0x0102030405060708U);
    TRL_CHECK(test, trl_reader_finish(&r));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] a signed number's bits in an
 * unsigned field of its size, `i32 as u32` and `i64 as u64`.
 */
static void signed_numbers_keep_their_bits(trl_test *test) {
    TRL_CHECK(test, trl_bits_i32(0xFFFFFFFFU) == -1);
    TRL_CHECK(test, trl_bits_i32(0x80000000U) == INT32_MIN);
    TRL_CHECK(test, trl_bits_i32(0x7FFFFFFFU) == INT32_MAX);
    TRL_CHECK(test, trl_bits_i64(0xFFFFFFFFFFFFFC19U) == -999);
    TRL_CHECK(test, trl_bits_i64(0x8000000000000000U) == INT64_MIN);
    TRL_CHECK(test, trl_bits_i16(0xFCE4U) == -796);
    TRL_CHECK(test, trl_bits_i16(0x0348U) == 840);
}

/**
 * Verifies: [PROTOCOL Values and fields] an i128 is 16 bytes, two's
 * complement, least significant first, at its limits too.
 */
static void i128_is_sixteen_bytes(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const trl_int128 values[] = {{0U, 0}, {1U, 0}, {UINT64_MAX, -1}, {0U, INT64_MIN}, {UINT64_MAX, INT64_MAX}};
    for (size_t i = 0U; i < TRL_TEST_COUNT(values); ++i) {
        fixture_reset(&f);
        trl_put_i128(&f.buffer, values[i]);
        TRL_CHECK(test, f.buffer.length == TRL_I128_BYTES);
        trl_reader r = reader_of(&f, f.buffer.data, f.buffer.length);
        trl_int128 back = {0U, 0};
        TRL_CHECK(test, trl_read_i128(&r, &back) && trl_int128_equal(back, values[i]));
    }
    fixture_reset(&f);
    trl_put_i128(&f.buffer, values[2]);
    const uint8_t minus_one[TRL_I128_BYTES] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    TRL_CHECK(test, buffer_is(&f.buffer, minus_one, sizeof minus_one));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] a flag is 0 or 1; any other byte
 * fails the connection.
 */
static void flags_are_zero_or_one(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t bytes[] = {0, 1, 2};
    trl_reader r = reader_of(&f, bytes, sizeof bytes);
    bool a = true;
    bool b = false;
    bool c = false;
    TRL_CHECK(test, trl_read_flag(&r, &a) && !a);
    TRL_CHECK(test, trl_read_flag(&r, &b) && b);
    TRL_CHECK(test, !trl_read_flag(&r, &c));
    TRL_CHECK(test, failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] a string is its length in bytes,
 * u32, then its UTF-8; bytes are their length then them.
 */
static void strings_and_bytes_are_counted(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_bytes(&f.buffer, text_of("ANN"));
    const uint8_t raw[] = {0x00, 0xFF};
    trl_put_bytes(&f.buffer, bytes_of(raw, sizeof raw));
    const uint8_t expected[] = {3, 0, 0, 0, 'A', 'N', 'N', 2, 0, 0, 0, 0x00, 0xFF};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    trl_reader r = reader_of(&f, expected, sizeof expected);
    trl_slice name = {NULL, 0U};
    trl_slice octets = {NULL, 0U};
    TRL_CHECK(test, trl_read_string(&r, &name) && slice_is(name, "ANN"));
    TRL_CHECK(test, trl_read_bytes(&r, &octets) && octets.length == 2U && octets.bytes[1] == 0xFFU);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] a string not UTF-8 fails the
 * connection, as turborel-client refuses it.
 */
static void strings_are_utf8(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t bytes[] = {1, 0, 0, 0, 0xFF};
    trl_reader r = reader_of(&f, bytes, sizeof bytes);
    trl_slice text = {NULL, 0U};
    TRL_CHECK(test, !trl_read_string(&r, &text));
    TRL_CHECK(test, failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] UTF-8 is checked as RFC 3629 has
 * it: no overlong form, no surrogate, nothing past U+10FFFF, nothing cut
 * short; every boundary of the well-formed ranges taken.
 */
static void utf8_is_checked_at_its_edges(trl_test *test) {
    const uint8_t good[] = {
        'a',  0xC2, 0x80, 0xDF, 0xBF, 0xE0, 0xA0, 0x80, 0xED, 0x9F, 0xBF,
        0xEE, 0x80, 0x80, 0xF0, 0x90, 0x80, 0x80, 0xF4, 0x8F, 0xBF, 0xBF,
    };
    TRL_CHECK(test, trl_utf8_valid(good, sizeof good));
    TRL_CHECK(test, trl_utf8_valid(NULL, 0U));
    const uint8_t overlong2[] = {0xC1, 0xBF};
    const uint8_t overlong3[] = {0xE0, 0x9F, 0xBF};
    const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    const uint8_t overlong4[] = {0xF0, 0x8F, 0xBF, 0xBF};
    const uint8_t past[] = {0xF4, 0x90, 0x80, 0x80};
    const uint8_t lead[] = {0xF5, 0x80, 0x80, 0x80};
    const uint8_t cut[] = {0xE2, 0x82};
    const uint8_t continuation[] = {0x80};
    const uint8_t bad_third[] = {0xE2, 0x82, 0x41};
    TRL_CHECK(test, !trl_utf8_valid(overlong2, sizeof overlong2));
    TRL_CHECK(test, !trl_utf8_valid(overlong3, sizeof overlong3));
    TRL_CHECK(test, !trl_utf8_valid(surrogate, sizeof surrogate));
    TRL_CHECK(test, !trl_utf8_valid(overlong4, sizeof overlong4));
    TRL_CHECK(test, !trl_utf8_valid(past, sizeof past));
    TRL_CHECK(test, !trl_utf8_valid(lead, sizeof lead));
    TRL_CHECK(test, !trl_utf8_valid(cut, sizeof cut));
    TRL_CHECK(test, !trl_utf8_valid(continuation, sizeof continuation));
    TRL_CHECK(test, !trl_utf8_valid(bad_third, sizeof bad_third));
}

/**
 * Verifies: [PROTOCOL Values and fields] a key is 16 bytes with no length
 * before them; fewer fail.
 */
static void keys_are_sixteen_bytes(trl_test *test) {
    fixture f;
    fixture_init(&f);
    uint8_t bytes[TRL_KEY_BYTES];
    memset(bytes, 7, sizeof bytes);
    trl_reader r = reader_of(&f, bytes, sizeof bytes);
    trl_slice key = {NULL, 0U};
    TRL_CHECK(test, trl_read_raw(&r, TRL_KEY_BYTES, &key) && key.length == TRL_KEY_BYTES);
    trl_reader shorter = reader_of(&f, bytes, sizeof bytes - 1U);
    TRL_CHECK(test, !trl_read_raw(&shorter, TRL_KEY_BYTES, &key));
    TRL_CHECK(test, failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] a count larger than the bytes
 * that follow could hold fails at once, reserving nothing for it.
 */
static void a_count_past_the_body_fails(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t huge[] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    trl_reader r = reader_of(&f, huge, sizeof huge);
    uint32_t count = 0U;
    TRL_CHECK(test, !trl_read_count(&r, 1U, &count));
    TRL_CHECK(test, failed_connection(&f));
    TRL_CHECK(test, f.arena.blocks == NULL);
    fixture_reset(&f);
    const uint8_t exact[] = {4, 0, 0, 0, 1, 2, 3, 4};
    trl_reader fits = reader_of(&f, exact, sizeof exact);
    TRL_CHECK(test, trl_read_count(&fits, 1U, &count) && count == 4U);
    trl_reader four_each = reader_of(&f, exact, sizeof exact);
    TRL_CHECK(test, !trl_read_count(&four_each, 2U, &count));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Frames] a body cut short, or with bytes after its
 * last field, fails the connection.
 */
static void a_body_is_read_whole(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t bytes[] = {1, 2, 3};
    trl_reader r = reader_of(&f, bytes, sizeof bytes);
    uint32_t word = 0U;
    TRL_CHECK(test, !trl_read_u32(&r, &word));
    TRL_CHECK(test, failed_connection(&f));
    fixture_reset(&f);
    trl_reader more = reader_of(&f, bytes, sizeof bytes);
    uint8_t byte = 0U;
    TRL_CHECK(test, trl_read_u8(&more, &byte));
    TRL_CHECK(test, !trl_reader_finish(&more));
    TRL_CHECK(test, failed_connection(&f));
    TRL_CHECK(test, strstr(f.error.message, "bytes follow its end") != NULL);
    fixture_free(&f);
}

/**
 * Verifies: [DESIGN] D9 a buffer that cannot take a field longer than its
 * u32 length holds refuses it, and frames lengths it can.
 */
static void a_frame_is_framed(trl_test *test) {
    fixture f;
    fixture_init(&f);
    size_t start = trl_frame_start(&f.buffer, 'Q');
    trl_put_u8(&f.buffer, 1U);
    trl_frame_finish(&f.buffer, start);
    const uint8_t expected[] = {'Q', 1, 0, 0, 0, 1};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    trl_put_count(&f.buffer, (size_t)UINT32_MAX + 1U);
    TRL_CHECK(test, !trl_buffer_ok(&f.buffer, &f.error));
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_API, TRL_SQLSTATE_API));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(integers_are_little_endian),
        TRL_TEST_CASE(signed_numbers_keep_their_bits),
        TRL_TEST_CASE(i128_is_sixteen_bytes),
        TRL_TEST_CASE(flags_are_zero_or_one),
        TRL_TEST_CASE(strings_and_bytes_are_counted),
        TRL_TEST_CASE(strings_are_utf8),
        TRL_TEST_CASE(utf8_is_checked_at_its_edges),
        TRL_TEST_CASE(keys_are_sixteen_bytes),
        TRL_TEST_CASE(a_count_past_the_body_fails),
        TRL_TEST_CASE(a_body_is_read_whole),
        TRL_TEST_CASE(a_frame_is_framed),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
