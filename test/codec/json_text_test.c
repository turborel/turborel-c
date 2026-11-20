/**
 * @file json_text_test.c
 * @brief JSON values between their stored form and JSON text, as turborel
 * parses and serializes them [PROTOCOL Values].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/json_nodes.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/* Room for the texts these tests make. */
#define TEXT_BYTES 512U

/* Writes the text of the document in the fixture's buffer into `out`. */
static bool text_of_document(fixture *f, const uint8_t *document, size_t length, char *out) {
    trl_buffer text;
    trl_buffer_init(&text);
    bool written = trl_json_to_text(&text, document, length, &f->error);
    if (written && text.length < TEXT_BYTES) {
        memcpy(out, text.data, text.length);
        out[text.length] = '\0';
    }
    trl_buffer_free(&text);
    return written;
}

/* Parses `text`, writes it back as text into `out`. */
static bool reserialized(fixture *f, const char *text, char *out) {
    fixture_reset(f);
    if (!trl_json_from_text(&f->buffer, (const uint8_t *)text, strlen(text), &f->arena, &f->error)) {
        return false;
    }
    return text_of_document(f, f->buffer.data, f->buffer.length, out);
}

/* Whether `text` reads back as `expected`. */
static bool reads_as(fixture *f, const char *text, const char *expected) {
    char out[TEXT_BYTES];
    return reserialized(f, text, out) && strcmp(out, expected) == 0;
}

/**
 * Verifies: [PROTOCOL Values] a text is written back with no white space,
 * its members in the order written, those of one key all.
 */
static void structure_reads_back(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, reads_as(&f, " { \"b\" : 1 , \"a\" : [ true , false , null ] , \"b\" : {} } ",
                             "{\"b\":1,\"a\":[true,false,null],\"b\":{}}"));
    TRL_CHECK(test, reads_as(&f, "[]", "[]"));
    TRL_CHECK(test, reads_as(&f, "[[[\"x\"]]]", "[[[\"x\"]]]"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] numbers read back as turborel writes them: an
 * exact one's digits and scale, a long one as a DECFLOAT, plain.
 */
static void numbers_read_back(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, reads_as(&f, "0", "0"));
    TRL_CHECK(test, reads_as(&f, "-12.50", "-12.50"));
    TRL_CHECK(test, reads_as(&f, "0.000000000000000012", "0.000000000000000012"));
    TRL_CHECK(test, reads_as(&f, "12345678901234567890", "12345678901234567890"));
    TRL_CHECK(test, reads_as(&f, "1.5000000000000000000000000000000000000000", "1.5"));
    TRL_CHECK(test,
              reads_as(&f, "0.0000000000000000000000000000000000000001", "0.0000000000000000000000000000000000000001"));
    TRL_CHECK(test, reads_as(&f, "123456789012345678.9", "123456789012345678.9"));
    TRL_CHECK(test, reads_as(&f, "-12345678901234567890", "-12345678901234567890"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an approximate number reads back as its
 * shortest numeral with an exponent, as turborel writes one.
 */
static void approximate_numbers_read_back(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, reads_as(&f, "1.5E300", "1.5E300"));
    TRL_CHECK(test, reads_as(&f, "-2.5e-1", "-2.5E-1"));
    TRL_CHECK(test, reads_as(&f, "2.5e-1", "2.5E-1"));
    TRL_CHECK(test, reads_as(&f, "1e0", "1.0E0"));
    TRL_CHECK(test, reads_as(&f, "-0E0", "0E0"));
    TRL_CHECK(test, reads_as(&f, "0.1E1", "1.0E0"));
    TRL_CHECK(test, reads_as(&f, "4.9e-324", "5.0E-324"));
    TRL_CHECK(test, reads_as(&f, "1.7976931348623157e308", "1.7976931348623157E308"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a number no type of turborel's holds exactly
 * is refused with SQLSTATE 22003: past 34 significant digits, past an
 * approximate number's range, or too small for one but not zero.
 */
static void numbers_out_of_range_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    char out[TEXT_BYTES];
    const char *texts[] = {"12345678901234567890123456789012345", "1e400", "1e-400", "-1e309"};
    for (size_t i = 0U; i < TRL_TEST_COUNT(texts); ++i) {
        TRL_CHECK(test, !reserialized(&f, texts[i], out));
        TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_DATA, TRL_SQLSTATE_OUT_OF_RANGE));
    }
    TRL_CHECK(test, reads_as(&f, "0e-400", "0E0"));
    TRL_CHECK(test, reads_as(&f, "1234567890123456789012345678901234", "1234567890123456789012345678901234"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a string reads back with `"` and `\` escaped,
 * its control characters by their two-character escape or `\u` and four
 * lower-case digits, and every other character as it is.
 */
static void strings_read_back(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, reads_as(&f, "\"q\\\"b\\\\s\\/\\b\\f\\n\\r\\t\\u0001\\u001F\\u00e9\"",
                             "\"q\\\"b\\\\s/\\b\\f\\n\\r\\t\\u0001\\u001f\xC3\xA9\""));
    TRL_CHECK(test, reads_as(&f, "\"\\ud83d\\ude00\"", "\"\xF0\x9F\x98\x80\""));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a text that is not JSON is refused with
 * SQLSTATE 22032: empty, a leading zero, a trailing comma, a member with
 * no value, a name misspelt, a string not closed or holding a control
 * character, characters after the value, a lone surrogate, bytes not
 * UTF-8.
 */
static void invalid_texts_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    char out[TEXT_BYTES];
    const char *texts[] = {
        "",         "01",    "[1,]",       "{\"a\"}", "tru",         "\"open",      "1 2",
        "\"\x01\"", "-",     "1.",         ".5",      "1e",          "\"\\ud800\"", "\"\\udc00\"",
        "\"\\x\"",  "[1 2]", "{\"a\":1,}", "\xFF",    "\"\\u12G4\"", "{1:2}",       "nul",
    };
    for (size_t i = 0U; i < TRL_TEST_COUNT(texts); ++i) {
        TRL_CHECK(test, !reserialized(&f, texts[i], out));
        TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_JSON_TEXT));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a wide decimal node, units i128 and scale
 * u32, is written as the exact number it is; a datetime, or an
 * approximate number not finite, has no JSON text (22032).
 */
static void stored_nodes_without_text(trl_test *test) {
    fixture f;
    fixture_init(&f);
    char out[TEXT_BYTES];
    uint8_t wide[] = {1, TRL_JSON_WIDE, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0};
    TRL_CHECK(test, text_of_document(&f, wide, sizeof wide, out) && strcmp(out, "18446744073709551.616") == 0);
    const uint8_t datetime[] = {1, TRL_JSON_DATETIME, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 6};
    TRL_CHECK(test, !text_of_document(&f, datetime, sizeof datetime, out));
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_JSON_TEXT));
    fixture_reset(&f);
    const uint8_t nan[] = {1, TRL_JSON_APPROXIMATE, 1, 0, 0, 0, 0, 0, 0xF8, 0x7F};
    TRL_CHECK(test, !text_of_document(&f, nan, sizeof nan, out));
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_JSON_TEXT));
    fixture_reset(&f);
    const uint8_t damaged[] = {2, TRL_JSON_NULL};
    TRL_CHECK(test, !text_of_document(&f, damaged, sizeof damaged, out));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(structure_reads_back),
        TRL_TEST_CASE(numbers_read_back),
        TRL_TEST_CASE(approximate_numbers_read_back),
        TRL_TEST_CASE(numbers_out_of_range_fail),
        TRL_TEST_CASE(strings_read_back),
        TRL_TEST_CASE(invalid_texts_fail),
        TRL_TEST_CASE(stored_nodes_without_text),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
