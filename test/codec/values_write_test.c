/**
 * @file values_write_test.c
 * @brief Values a program gives that a server would refuse, refused before
 * they are sent [PROTOCOL Values], [DESIGN] D9.
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>

/* Whether writing `value` is refused with `sqlcode` and `sqlstate`. */
static bool refused_as(fixture *f, const trl_value *value, int32_t sqlcode, const char *sqlstate) {
    fixture_reset(f);
    bool written = trl_write_value(&f->buffer, value, &f->error);
    return !written && failed_with(f, sqlcode, sqlstate);
}

/* Whether writing `value` is refused as a call made wrongly. */
static bool refused(fixture *f, const trl_value *value) {
    return refused_as(f, value, TRL_SQLCODE_API, TRL_SQLSTATE_API);
}

/* Whether writing `value` is refused as a value the protocol cannot carry, of `sqlstate`. */
static bool refused_data(fixture *f, const trl_value *value, const char *sqlstate) {
    return refused_as(f, value, TRL_SQLCODE_DATA, sqlstate);
}

/**
 * Verifies: [PROTOCOL Values and fields] a string, an XML serialization
 * and a character large object to send are UTF-8, or refused.
 */
static void strings_to_send_are_utf8(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t bad[] = {0xC3};
    trl_value text = {TRL_TAG_CHARACTER, {bytes_of(bad, sizeof bad)}};
    trl_value xml = {TRL_TAG_XML, {bytes_of(bad, sizeof bad)}};
    trl_value lob = {TRL_TAG_LOB_WHOLE, {{NULL, 0U}}};
    lob.as.lob.kind = TRL_LOB_CHARACTER;
    lob.as.lob.octets = bytes_of(bad, sizeof bad);
    TRL_CHECK(test, refused_data(&f, &text, TRL_SQLSTATE_NOT_UTF8));
    TRL_CHECK(test, refused_data(&f, &xml, TRL_SQLSTATE_NOT_UTF8));
    TRL_CHECK(test, refused_data(&f, &lob, TRL_SQLSTATE_NOT_UTF8));
    lob.as.lob.kind = TRL_LOB_BINARY;
    fixture_reset(&f);
    TRL_CHECK(test, trl_write_value(&f.buffer, &lob, &f.error));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a UUID to send is 16 octets, a large
 * object's kind 0 or 1, an interval's fields 0 to 5, a DECFLOAT canonical,
 * a multiset of at most 65535 elements; else each is refused.
 */
static void malformed_values_are_refused(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t octets[15] = {0};
    trl_value uuid = {TRL_TAG_UUID, {bytes_of(octets, sizeof octets)}};
    TRL_CHECK(test, refused_data(&f, &uuid, TRL_SQLSTATE_DATA));
    trl_value locator = {TRL_TAG_LOB_LOCATOR, {{NULL, 0U}}};
    locator.as.locator.kind = 2U;
    TRL_CHECK(test, refused(&f, &locator));
    trl_value interval = {TRL_TAG_INTERVAL, {{NULL, 0U}}};
    interval.as.interval.qualifier.start = 6U;
    TRL_CHECK(test, refused_data(&f, &interval, TRL_SQLSTATE_INTERVAL_OVERFLOW));
    trl_value decfloat = {TRL_TAG_DECFLOAT, {{NULL, 0U}}};
    decfloat.as.decfloat.coefficient.low = 100U;
    TRL_CHECK(test, refused_data(&f, &decfloat, TRL_SQLSTATE_OUT_OF_RANGE));
    trl_value multiset = {TRL_TAG_MULTISET, {{NULL, 0U}}};
    multiset.as.list.count = 65536U;
    TRL_CHECK(test, refused_as(&f, &multiset, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED));
    trl_value unknown = {28U, {{NULL, 0U}}};
    TRL_CHECK(test, refused(&f, &unknown));
    TRL_CHECK(test, f.buffer.length <= 1U);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a value nested past 16 arrays and rows is
 * refused before it is sent, as a server would refuse it.
 */
static void nesting_to_send_is_bounded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value chain[18];
    chain[17].tag = TRL_TAG_NULL;
    for (unsigned int i = 0U; i < 17U; ++i) {
        chain[i].tag = TRL_TAG_ARRAY;
        chain[i].as.list.items = &chain[i + 1U];
        chain[i].as.list.count = 1U;
    }
    TRL_CHECK(test, refused_as(&f, &chain[0], TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED));
    fixture_reset(&f);
    TRL_CHECK(test, trl_write_value(&f.buffer, &chain[1], &f.error));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a JSON value to send is a document of its
 * stored form, or refused.
 */
static void json_to_send_is_whole(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t document[] = {1, 2};
    const uint8_t damaged[] = {1, 12};
    trl_value good = {TRL_TAG_JSON, {bytes_of(document, sizeof document)}};
    trl_value bad = {TRL_TAG_JSON, {bytes_of(damaged, sizeof damaged)}};
    TRL_CHECK(test, trl_write_value(&f.buffer, &good, &f.error));
    TRL_CHECK(test, refused_data(&f, &bad, TRL_SQLSTATE_INVALID_JSON_TEXT));
    fixture_free(&f);
}

/**
 * Verifies: [DESIGN] D9 a null value, or a row of values given none, is
 * refused, never read.
 */
static void null_pointers_are_refused(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, !trl_write_value(&f.buffer, NULL, &f.error));
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_API, TRL_SQLSTATE_API));
    fixture_reset(&f);
    TRL_CHECK(test, !trl_write_row(&f.buffer, NULL, 1U, &f.error));
    fixture_reset(&f);
    trl_value list = {TRL_TAG_ARRAY, {{NULL, 0U}}};
    list.as.list.count = 1U;
    TRL_CHECK(test, refused(&f, &list));
    fixture_reset(&f);
    TRL_CHECK(test, trl_write_row(&f.buffer, NULL, 0U, &f.error));
    const uint8_t empty[] = {0, 0, 0, 0};
    TRL_CHECK(test, buffer_is(&f.buffer, empty, sizeof empty));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(strings_to_send_are_utf8),   TRL_TEST_CASE(malformed_values_are_refused),
        TRL_TEST_CASE(nesting_to_send_is_bounded), TRL_TEST_CASE(json_to_send_is_whole),
        TRL_TEST_CASE(null_pointers_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
