/**
 * @file tail_test.c
 * @brief A message's tail: tags in increasing order, only those the
 * message takes, attributes within their bounds [PROTOCOL The tail].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/reader.h"
#include "codec/tail.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/* A reader of the fixture's buffer. */
static trl_reader reader_of(fixture *f) {
    trl_reader r;
    trl_reader_init(&r, f->buffer.data, f->buffer.length, &f->arena, &f->error);
    return r;
}

/**
 * Verifies: [PROTOCOL The tail] tags are read in increasing order, each
 * one the message takes; an empty tail is its message's first form.
 */
static void tags_come_in_order(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint32_t taken = TRL_TAG_BIT(4U) | TRL_TAG_BIT(5U) | TRL_TAG_BIT(18U);
    trl_put_u8(&f.buffer, 4U);
    trl_put_u8(&f.buffer, 18U);
    trl_reader r = reader_of(&f);
    trl_tail tail = {0U};
    uint8_t tag = 0U;
    TRL_CHECK(test, trl_tail_next(&r, &tail, taken, &tag) && tag == 4U);
    TRL_CHECK(test, trl_tail_next(&r, &tail, taken, &tag) && tag == 18U);
    TRL_CHECK(test, trl_tail_next(&r, &tail, taken, &tag) && tag == 0U);
    trl_reader empty;
    trl_reader_init(&empty, NULL, 0U, &f.arena, &f.error);
    trl_tail fresh = {0U};
    TRL_CHECK(test, trl_tail_next(&empty, &fresh, taken, &tag) && tag == 0U);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL The tail] a tag not past the last, repeated, or one
 * the message does not take, fails the connection.
 */
static void tags_out_of_place_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint32_t taken = TRL_TAG_BIT(4U) | TRL_TAG_BIT(5U);
    const uint8_t cases[][2] = {{5U, 4U}, {5U, 5U}, {6U, 0U}, {40U, 0U}};
    for (size_t i = 0U; i < TRL_TEST_COUNT(cases); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, cases[i][0]);
        if (cases[i][1] != 0U) {
            trl_put_u8(&f.buffer, cases[i][1]);
        }
        trl_reader r = reader_of(&f);
        trl_tail tail = {0U};
        uint8_t tag = 0U;
        bool read = true;
        while (read && trl_tail_next(&r, &tail, taken, &tag) && tag != 0U) {
            read = true;
        }
        TRL_CHECK(test, failed_connection(&f));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL The tail] attributes are a count, at most 32, then
 * each name, 1 to 64 bytes, and value, at most 1024; more fails.
 */
static void attributes_are_bounded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u32(&f.buffer, 1U);
    trl_put_bytes(&f.buffer, text_of("application"));
    trl_put_bytes(&f.buffer, text_of("report"));
    trl_reader r = reader_of(&f);
    trl_attribute *attributes = NULL;
    uint32_t count = 0U;
    TRL_CHECK(test, trl_read_attributes(&r, &attributes, &count) && count == 1U);
    TRL_CHECK(test, slice_is(attributes[0].name, "application") && slice_is(attributes[0].value, "report"));
    fixture_reset(&f);
    trl_put_u32(&f.buffer, 33U);
    for (unsigned int i = 0U; i < 33U; ++i) {
        trl_put_bytes(&f.buffer, text_of("n"));
        trl_put_bytes(&f.buffer, text_of("v"));
    }
    r = reader_of(&f);
    TRL_CHECK(test, !trl_read_attributes(&r, &attributes, &count) && failed_connection(&f));
    char value[1026];
    memset(value, 'v', 1025U);
    value[1025] = '\0';
    const char *names[] = {"", "n"};
    const char *values[] = {"v", value};
    for (size_t i = 0U; i < TRL_TEST_COUNT(names); ++i) {
        fixture_reset(&f);
        trl_put_u32(&f.buffer, 1U);
        trl_put_bytes(&f.buffer, text_of(names[i]));
        trl_put_bytes(&f.buffer, text_of(values[i]));
        r = reader_of(&f);
        TRL_CHECK(test, !trl_read_attributes(&r, &attributes, &count) && failed_connection(&f));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL The tail] attributes cut short fail the connection;
 * attributes to send that are given none, or not UTF-8, are refused.
 */
static void attributes_cut_or_malformed_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t cut_count[] = {1, 0};
    const uint8_t cut_value[] = {1, 0, 0, 0, 1, 0, 0, 0, 'n', 5, 0, 0, 0, 'v'};
    trl_attribute *attributes = NULL;
    uint32_t count = 0U;
    trl_reader r;
    trl_reader_init(&r, cut_count, sizeof cut_count, &f.arena, &f.error);
    TRL_CHECK(test, !trl_read_attributes(&r, &attributes, &count) && failed_connection(&f));
    fixture_reset(&f);
    trl_reader_init(&r, cut_value, sizeof cut_value, &f.arena, &f.error);
    TRL_CHECK(test, !trl_read_attributes(&r, &attributes, &count) && failed_connection(&f));
    fixture_reset(&f);
    TRL_CHECK(test, !trl_write_attributes(&f.buffer, NULL, 1U, &f.error));
    const uint8_t bad[] = {0xFF};
    const trl_attribute bad_name = {{bad, sizeof bad}, {NULL, 0U}};
    const trl_attribute bad_value = {{(const uint8_t *)"n", 1U}, {bad, sizeof bad}};
    TRL_CHECK(test, !trl_write_attributes(&f.buffer, &bad_name, 1U, &f.error));
    TRL_CHECK(test, !trl_write_attributes(&f.buffer, &bad_value, 1U, &f.error));
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_API, TRL_SQLSTATE_API));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(tags_come_in_order),
        TRL_TEST_CASE(tags_out_of_place_fail),
        TRL_TEST_CASE(attributes_are_bounded),
        TRL_TEST_CASE(attributes_cut_or_malformed_fail),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
