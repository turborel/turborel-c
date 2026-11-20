/**
 * @file frame_test.c
 * @brief The frame header's encoding and decoding [PROTOCOL Frames].
 */
#include "codec/frame.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/** The header bytes of a frame, as the spec's worked example prints them. */
typedef struct example {
    uint8_t bytes[TRL_FRAME_HEADER_BYTES];
    uint8_t kind;
    uint32_t length;
} example;

/* The headers of the spec's worked example: Hello, Ping, Query, Rows and
   Complete, each as printed there. */
static const example examples[] = {
    {{0x48, 0x24, 0x00, 0x00, 0x00}, 'H', 36U}, {{0x47, 0x00, 0x00, 0x00, 0x00}, 'G', 0U},
    {{0x51, 0x0D, 0x00, 0x00, 0x00}, 'Q', 13U}, {{0x42, 0x1D, 0x00, 0x00, 0x00}, 'B', 29U},
    {{0x4B, 0x0E, 0x00, 0x00, 0x00}, 'K', 14U},
};

/** The byte a buffer is filled with, to show what a call left alone. */
#define UNTOUCHED 0xAAU

/**
 * Verifies: [PROTOCOL A worked example] each header the worked example
 * prints, Hello, Ping, Query, Rows and Complete, is written byte for byte.
 */
static void encodes_the_worked_example(trl_test *test) {
    for (size_t i = 0U; i < TRL_TEST_COUNT(examples); ++i) {
        uint8_t out[TRL_FRAME_HEADER_BYTES];
        trl_frame_header header = {examples[i].kind, examples[i].length};
        TRL_CHECK(test, trl_frame_header_encode(header, out, sizeof out) == TRL_FRAME_OK);
        TRL_CHECK(test, memcmp(out, examples[i].bytes, sizeof out) == 0);
    }
}

/**
 * Verifies: [PROTOCOL A worked example] each header the worked example
 * prints is read as its kind and its body's length.
 */
static void decodes_the_worked_example(trl_test *test) {
    for (size_t i = 0U; i < TRL_TEST_COUNT(examples); ++i) {
        trl_frame_header header = {0U, 0U};
        TRL_CHECK(test, trl_frame_header_decode(examples[i].bytes, sizeof examples[i].bytes, TRL_FRAME_MAX_BODY,
                                                &header) == TRL_FRAME_OK);
        TRL_CHECK(test, header.kind == examples[i].kind);
        TRL_CHECK(test, header.length == examples[i].length);
    }
}

/**
 * Verifies: [PROTOCOL Values and fields] the length is a u32 written least
 * significant byte first, each of its four bytes in its place.
 */
static void length_is_little_endian(trl_test *test) {
    const uint8_t bytes[TRL_FRAME_HEADER_BYTES] = {'E', 0x04, 0x03, 0x02, 0x01};
    uint8_t out[TRL_FRAME_HEADER_BYTES];
    trl_frame_header header = {0U, 0U};
    TRL_CHECK(test, trl_frame_header_decode(bytes, sizeof bytes, UINT32_MAX, &header) == TRL_FRAME_OK);
    TRL_CHECK(test, header.kind == 'E');
    TRL_CHECK(test, header.length == 0x01020304U);
    TRL_CHECK(test, trl_frame_header_encode(header, out, sizeof out) == TRL_FRAME_OK);
    TRL_CHECK(test, memcmp(out, bytes, sizeof out) == 0);
}

/**
 * Verifies: [PROTOCOL Frames] the longest body taken, 16 MiB by default,
 * is read; one byte more fails, leaving the header given as it was.
 */
static void refuses_a_body_longer_than_taken(trl_test *test) {
    uint8_t bytes[TRL_FRAME_HEADER_BYTES];
    trl_frame_header longest = {'B', TRL_FRAME_MAX_BODY};
    trl_frame_header longer = {'B', TRL_FRAME_MAX_BODY + 1U};
    trl_frame_header header = {0U, 0U};
    TRL_CHECK(test, trl_frame_header_encode(longest, bytes, sizeof bytes) == TRL_FRAME_OK);
    TRL_CHECK(test, trl_frame_header_decode(bytes, sizeof bytes, TRL_FRAME_MAX_BODY, &header) == TRL_FRAME_OK);
    TRL_CHECK(test, header.length == TRL_FRAME_MAX_BODY);
    header.kind = 0U;
    header.length = 0U;
    TRL_CHECK(test, trl_frame_header_encode(longer, bytes, sizeof bytes) == TRL_FRAME_OK);
    TRL_CHECK(test, trl_frame_header_decode(bytes, sizeof bytes, TRL_FRAME_MAX_BODY, &header) == TRL_FRAME_TOO_LONG);
    TRL_CHECK(test, header.kind == 0U && header.length == 0U);
}

/**
 * Verifies: [PROTOCOL Frames] a reader told to take less, as the server
 * takes 16 KiB before login, refuses a body past that bound.
 */
static void honours_a_smaller_bound(trl_test *test) {
    const uint32_t bound = 16U * 1024U;
    uint8_t bytes[TRL_FRAME_HEADER_BYTES];
    trl_frame_header header = {0U, 0U};
    TRL_CHECK(test, trl_frame_header_encode((trl_frame_header){'H', bound + 1U}, bytes, sizeof bytes) == TRL_FRAME_OK);
    TRL_CHECK(test, trl_frame_header_decode(bytes, sizeof bytes, bound, &header) == TRL_FRAME_TOO_LONG);
    TRL_CHECK(test, trl_frame_header_encode((trl_frame_header){'H', bound}, bytes, sizeof bytes) == TRL_FRAME_OK);
    TRL_CHECK(test, trl_frame_header_decode(bytes, sizeof bytes, bound, &header) == TRL_FRAME_OK);
    TRL_CHECK(test, header.length == bound);
}

/**
 * Verifies: [PROTOCOL Frames] the header is five bytes: fewer are not
 * read, and a buffer of fewer is not written to.
 */
static void needs_five_bytes(trl_test *test) {
    uint8_t out[TRL_FRAME_HEADER_BYTES];
    trl_frame_header header = {'G', 0U};
    memset(out, (int)UNTOUCHED, sizeof out);
    TRL_CHECK(test, trl_frame_header_encode(header, out, sizeof out - 1U) == TRL_FRAME_SHORT);
    TRL_CHECK(test, out[0] == UNTOUCHED);
    TRL_CHECK(test, trl_frame_header_encode(header, out, sizeof out) == TRL_FRAME_OK);
    header.kind = 0U;
    TRL_CHECK(test, trl_frame_header_decode(out, sizeof out - 1U, TRL_FRAME_MAX_BODY, &header) == TRL_FRAME_SHORT);
    TRL_CHECK(test, header.kind == 0U);
}

/**
 * Verifies: [DESIGN] D9 a null buffer or result is refused, never
 * dereferenced.
 */
static void refuses_null_pointers(trl_test *test) {
    uint8_t bytes[TRL_FRAME_HEADER_BYTES] = {'G', 0x00, 0x00, 0x00, 0x00};
    trl_frame_header header = {'G', 0U};
    TRL_CHECK(test, trl_frame_header_encode(header, NULL, sizeof bytes) == TRL_FRAME_SHORT);
    TRL_CHECK(test, trl_frame_header_decode(NULL, sizeof bytes, TRL_FRAME_MAX_BODY, &header) == TRL_FRAME_SHORT);
    TRL_CHECK(test, trl_frame_header_decode(bytes, sizeof bytes, TRL_FRAME_MAX_BODY, NULL) == TRL_FRAME_SHORT);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(encodes_the_worked_example), TRL_TEST_CASE(decodes_the_worked_example),
        TRL_TEST_CASE(length_is_little_endian),    TRL_TEST_CASE(refuses_a_body_longer_than_taken),
        TRL_TEST_CASE(honours_a_smaller_bound),    TRL_TEST_CASE(needs_five_bytes),
        TRL_TEST_CASE(refuses_null_pointers),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
