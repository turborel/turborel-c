/**
 * @file json_test.c
 * @brief A JSON value's stored form, turborel's RULES.md D89 binary tree:
 * its nodes written from JSON text, read, and checked whole, damaged ones
 * refused [PROTOCOL Values] [PROTOCOL JSON values].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/json_nodes.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/* The stored form of `text`, in the fixture's buffer. */
static bool stored(fixture *f, const char *text) {
    fixture_reset(f);
    return trl_json_from_text(&f->buffer, (const uint8_t *)text, strlen(text), &f->arena, &f->error);
}

/**
 * Verifies: [PROTOCOL Values] a document is its version, 1, then its root
 * node; null, false and true are their tags alone.
 */
static void literals_are_their_tags(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t null[] = {1, TRL_JSON_NULL};
    const uint8_t no[] = {1, TRL_JSON_FALSE};
    const uint8_t yes[] = {1, TRL_JSON_TRUE};
    TRL_CHECK(test, stored(&f, "null") && buffer_is(&f.buffer, null, sizeof null));
    TRL_CHECK(test, stored(&f, " false ") && buffer_is(&f.buffer, no, sizeof no));
    TRL_CHECK(test, stored(&f, "true") && buffer_is(&f.buffer, yes, sizeof yes));
    TRL_CHECK(test, trl_json_valid(yes, sizeof yes));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an exact number is stored in the narrowest
 * of its forms, an integer i64 or a decimal of units i64 and scale u8; one
 * of more digits than an exact number holds a DECFLOAT; one with an
 * exponent approximate.
 */
static void numbers_take_their_narrowest_node(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t seven[] = {1, TRL_JSON_INTEGER, 7, 0, 0, 0, 0, 0, 0, 0};
    TRL_CHECK(test, stored(&f, "7") && buffer_is(&f.buffer, seven, sizeof seven));
    const uint8_t minus[] = {1, TRL_JSON_DECIMAL, 0x1E, 0xFB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 2};
    TRL_CHECK(test, stored(&f, "-12.50") && buffer_is(&f.buffer, minus, sizeof minus));
    TRL_CHECK(test, stored(&f, "12345678901234567890") && f.buffer.data[1] == TRL_JSON_DECFLOAT);
    TRL_CHECK(test, f.buffer.data[2] == 0x15 && f.buffer.length == 1U + 1U + 16U + 4U);
    const uint8_t exponent_one[] = {1, 0, 0, 0};
    TRL_CHECK(test, memcmp(f.buffer.data + 18, exponent_one, sizeof exponent_one) == 0);
    const uint8_t half[] = {1, TRL_JSON_APPROXIMATE, 0, 0, 0, 0, 0, 0, 0xE0, 0x3F};
    TRL_CHECK(test, stored(&f, "5E-1") && buffer_is(&f.buffer, half, sizeof half));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a string is its octets' count, then its
 * UTF-8, its escapes read: two-character ones, \u escapes, and a surrogate
 * pair as one character.
 */
static void strings_are_unescaped(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, stored(&f, "\"a\\n\\u00e9\\ud83d\\ude00\\/\""));
    const uint8_t expected[] = {1, TRL_JSON_STRING, 9, 0, 0, 0, 'a', '\n', 0xC3, 0xA9, 0xF0, 0x9F, 0x98, 0x80, '/'};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an array is its count, its size, an offset of
 * each element from the first, then the elements.
 */
static void arrays_are_offsets_then_elements(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, stored(&f, "[true,null,[]]"));
    const uint8_t expected[] = {
        1,
        TRL_JSON_ARRAY,
        3,
        0,
        0,
        0,
        11,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        2,
        0,
        0,
        0,
        TRL_JSON_TRUE,
        TRL_JSON_NULL,
        TRL_JSON_ARRAY,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    TRL_CHECK(test, trl_json_valid(f.buffer.data, f.buffer.length));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an object keeps its members in the order
 * written, those of one key all, and its directory sorted by key octets,
 * of one key in the order written.
 */
static void objects_keep_their_order_and_sort_their_directory(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, stored(&f, "{\"b\":1,\"a\":2,\"b\":3}"));
    trl_json_span node = {f.buffer.data + 1, f.buffer.length - 1U};
    trl_json_container container;
    TRL_CHECK(test, trl_json_container_at(node, &container) && container.count == 3U);
    /* Each member is a key of 4 + 1 octets and an integer of 9. */
    TRL_CHECK(test, trl_json_offset(container, 0U) == 14U);
    TRL_CHECK(test, trl_json_offset(container, 1U) == 0U);
    TRL_CHECK(test, trl_json_offset(container, 2U) == 28U);
    TRL_CHECK(test, container.contents.bytes[4] == 'b' && container.contents.bytes[18] == 'a');
    TRL_CHECK(test, trl_json_valid(f.buffer.data, f.buffer.length));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] arrays and objects nest 128 deep, as
 * turborel's MAX_NESTING, and a text nested deeper is refused (-900).
 */
static void nesting_is_bounded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    char text[(2U * 129U) + 1U];
    for (size_t depth = 128U; depth <= 129U; ++depth) {
        memset(text, '[', depth);
        memset(text + depth, ']', depth);
        text[2U * depth] = '\0';
        bool read = stored(&f, text);
        TRL_CHECK(test,
                  depth == 128U ? read : (!read && failed_with(&f, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED)));
        if (read) {
            TRL_CHECK(test, trl_json_valid(f.buffer.data, f.buffer.length));
        }
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a damaged document is refused, never read
 * past its end: cut at any octet, followed by an octet, of another version
 * or a tag no node has; any octet flipped is refused or read whole.
 */
static void damaged_documents_are_refused(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, stored(&f, "{\"a\":[1,\"x\"],\"b\":true}"));
    uint8_t good[64];
    size_t length = f.buffer.length;
    memcpy(good, f.buffer.data, length);
    TRL_CHECK(test, trl_json_valid(good, length));
    for (size_t cut = 0U; cut < length; ++cut) {
        TRL_CHECK(test, !trl_json_valid(good, cut));
    }
    uint8_t longer[65];
    memcpy(longer, good, length);
    longer[length] = 0U;
    TRL_CHECK(test, !trl_json_valid(longer, length + 1U));
    uint8_t changed[64];
    memcpy(changed, good, length);
    changed[0] = 2U;
    TRL_CHECK(test, !trl_json_valid(changed, length));
    changed[0] = 1U;
    changed[1] = 12U;
    TRL_CHECK(test, !trl_json_valid(changed, length));
    /* Every octet flipped is refused or reads as a document. */
    for (size_t at = 0U; at < length; ++at) {
        memcpy(changed, good, length);
        changed[at] ^= 0xFFU;
        (void)trl_json_valid(changed, length);
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a document whose directory is unsorted, a
 * string not UTF-8, a datetime's moment or flag out of range, or a DECFLOAT
 * out of range, is refused.
 */
static void damaged_nodes_are_refused(trl_test *test) {
    const uint8_t unsorted[] = {
        1,   TRL_JSON_OBJECT,
        2,   0,
        0,   0,
        12,  0,
        0,   0,
        0,   0,
        0,   0,
        6,   0,
        0,   0,
        1,   0,
        0,   0,
        'b', TRL_JSON_NULL,
        1,   0,
        0,   0,
        'a', TRL_JSON_NULL,
    };
    TRL_CHECK(test, !trl_json_valid(unsorted, sizeof unsorted));
    const uint8_t sorted[] = {
        1,   TRL_JSON_OBJECT,
        2,   0,
        0,   0,
        12,  0,
        0,   0,
        6,   0,
        0,   0,
        0,   0,
        0,   0,
        1,   0,
        0,   0,
        'b', TRL_JSON_NULL,
        1,   0,
        0,   0,
        'a', TRL_JSON_NULL,
    };
    TRL_CHECK(test, trl_json_valid(sorted, sizeof sorted));
    const uint8_t not_utf8[] = {1, TRL_JSON_STRING, 1, 0, 0, 0, 0xFF};
    TRL_CHECK(test, !trl_json_valid(not_utf8, sizeof not_utf8));
    const uint8_t moment[] = {1, TRL_JSON_DATETIME, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t zoned[] = {1, TRL_JSON_DATETIME, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0};
    const uint8_t datetime[] = {1, TRL_JSON_DATETIME, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 6};
    TRL_CHECK(test, !trl_json_valid(moment, sizeof moment));
    TRL_CHECK(test, !trl_json_valid(zoned, sizeof zoned));
    TRL_CHECK(test, trl_json_valid(datetime, sizeof datetime));
    const uint8_t decfloat[] = {1, TRL_JSON_DECFLOAT, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x18, 0, 0};
    TRL_CHECK(test, !trl_json_valid(decfloat, sizeof decfloat));
}

/**
 * Verifies: [PROTOCOL JSON values] the page's example: `{"a":[1,"x"]}` is
 * its 51 octets, byte for byte; and `{"b":1,"a":2}` keeps its members in
 * that order, its directory putting `a`, at 14, first.
 */
static void the_pages_examples_are_byte_exact(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t example[] = {
        0x01, 0x0B, 0x01, 0x00, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x61, 0x0A, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00,
        0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00, 0x00, 0x78,
    };
    TRL_CHECK(test, sizeof example == 51U);
    TRL_CHECK(test, stored(&f, "{\"a\":[1,\"x\"]}") && buffer_is(&f.buffer, example, sizeof example));
    TRL_CHECK(test, trl_json_valid(example, sizeof example));
    TRL_CHECK(test, stored(&f, "{\"b\":1,\"a\":2}") && f.buffer.length > 18U);
    const uint8_t directory[] = {0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x62};
    TRL_CHECK(test, memcmp(f.buffer.data + 10U, directory, sizeof directory) == 0);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL JSON values] a decimal keeps the scale it was
 * written with: 1.50 is units 150 and scale 2, a decimal node, not 1.5.
 */
static void a_decimal_keeps_its_scale(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t decimal[] = {1, TRL_JSON_DECIMAL, 150, 0, 0, 0, 0, 0, 0, 0, 2};
    TRL_CHECK(test, stored(&f, "1.50") && buffer_is(&f.buffer, decimal, sizeof decimal));
    const uint8_t integer[] = {1, TRL_JSON_INTEGER, 15, 0, 0, 0, 0, 0, 0, 0};
    TRL_CHECK(test, stored(&f, "15") && buffer_is(&f.buffer, integer, sizeof integer));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(literals_are_their_tags),
        TRL_TEST_CASE(numbers_take_their_narrowest_node),
        TRL_TEST_CASE(strings_are_unescaped),
        TRL_TEST_CASE(arrays_are_offsets_then_elements),
        TRL_TEST_CASE(objects_keep_their_order_and_sort_their_directory),
        TRL_TEST_CASE(nesting_is_bounded),
        TRL_TEST_CASE(damaged_documents_are_refused),
        TRL_TEST_CASE(damaged_nodes_are_refused),
        TRL_TEST_CASE(the_pages_examples_are_byte_exact),
        TRL_TEST_CASE(a_decimal_keeps_its_scale),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
