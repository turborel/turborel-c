/**
 * @file text_test.c
 * @brief Values written as turborel displays them (`value.rs` `Display`):
 * each kind's text, and a composite's literal of its elements' literals.
 */
#include "codec/buffer.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "data/params.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 2024-02-29 13:45:06.789 UTC, in microseconds from 0001-01-01. */
#define LEAP_DAY_INSTANT 63844811106789000LL
/* Microseconds of a minute. */
#define MICROS_MINUTE 60000000LL

/* Whether `value` displays as `expected`. */
static bool shows(const trl_value *value, const char *expected) {
    char text[256];
    size_t length = 0U;
    bool same = trl_value_to_text(value, text, sizeof text, &length) == TRL_OK && strcmp(text, expected) == 0 &&
                length == strlen(expected);
    if (!same) {
        (void)fprintf(stderr, "shown as \"%s\", not \"%s\"\n", text, expected);
    }
    return same;
}

/* A value of `tag` and no more. */
static trl_value of(uint8_t tag) {
    trl_value value;
    memset(&value, 0, sizeof value);
    value.tag = tag;
    return value;
}

/* A string value of `tag`. */
static trl_value text_of(uint8_t tag, const char *text) {
    trl_value value = of(tag);
    value.as.bytes.bytes = (const uint8_t *)text;
    value.as.bytes.length = strlen(text);
    return value;
}

/* An approximate value of `x`. */
static trl_value double_of(double x) {
    trl_value value = of(TRL_TAG_APPROXIMATE);
    memcpy(&value.as.approximate, &x, sizeof x);
    return value;
}

/**
 * Verifies: [CLIENT Copying and exporting] numbers and truth values as
 * turborel displays them: an exact number's digits, a double's shortest
 * digits with an exponent, a DECFLOAT plain or with an exponent.
 */
static void numbers_are_displayed(trl_test *test) {
    trl_value exact = of(TRL_TAG_EXACT);
    exact.as.exact.units.low = 1250U;
    exact.as.exact.scale = 2U;
    TRL_CHECK(test, shows(&exact, "12.50"));
    trl_value half = double_of(7.5);
    trl_value hundred = double_of(100.0);
    trl_value tenth = double_of(0.1);
    trl_value negative_zero = double_of(-0.0);
    trl_value third = double_of(1.0 / 3.0);
    TRL_CHECK(test, shows(&half, "7.5E0") && shows(&hundred, "1E2") && shows(&tenth, "1E-1"));
    TRL_CHECK(test, shows(&negative_zero, "-0E0") && shows(&third, "3.333333333333333E-1"));
    trl_value plain = of(TRL_TAG_DECFLOAT);
    plain.as.decfloat.coefficient.low = 12345U;
    plain.as.decfloat.exponent = -2;
    trl_value large = plain;
    large.as.decfloat.coefficient.low = 1U;
    large.as.decfloat.exponent = 40;
    trl_value small = plain;
    small.as.decfloat.coefficient.low = 123U;
    small.as.decfloat.exponent = -10;
    trl_value zero = of(TRL_TAG_DECFLOAT);
    TRL_CHECK(test, shows(&plain, "123.45") && shows(&large, "1E40") && shows(&small, "1.23E-8") && shows(&zero, "0"));
    trl_value truth = of(TRL_TAG_BOOLEAN);
    truth.as.truth = true;
    trl_value null = of(TRL_TAG_NULL);
    TRL_CHECK(test, shows(&truth, "TRUE") && shows(&null, "NULL"));
}

/**
 * Verifies: [CLIENT Copying and exporting] datetimes as their literals'
 * strings, in their zone's local time, their fraction to their precision;
 * intervals as theirs, their leading field's digits, the rest two each.
 */
static void datetimes_and_intervals_are_displayed(trl_test *test) {
    trl_value stamp = of(TRL_TAG_TIMESTAMP);
    stamp.as.datetime.utc = LEAP_DAY_INSTANT;
    stamp.as.datetime.precision = 3U;
    TRL_CHECK(test, shows(&stamp, "2024-02-29 13:45:06.789"));
    trl_value zoned = of(TRL_TAG_TIMESTAMP_ZONED);
    zoned.as.datetime = stamp.as.datetime;
    zoned.as.datetime.zoned = true;
    zoned.as.datetime.zone = -330;
    zoned.as.datetime.precision = 0U;
    TRL_CHECK(test, shows(&zoned, "2024-02-29 08:15:06-05:30"));
    trl_value date = of(TRL_TAG_DATE);
    date.as.datetime.utc = LEAP_DAY_INSTANT - (((13LL * 60LL) + 45LL) * MICROS_MINUTE) - 6789000LL;
    TRL_CHECK(test, shows(&date, "2024-02-29"));
    trl_value time = of(TRL_TAG_TIME);
    time.as.datetime.utc = (((13LL * 60LL) + 45LL) * MICROS_MINUTE) + 6000000LL;
    TRL_CHECK(test, shows(&time, "13:45:06"));
    trl_value first = of(TRL_TAG_DATE);
    TRL_CHECK(test, shows(&first, "0001-01-01"));
    trl_value seconds = of(TRL_TAG_INTERVAL);
    seconds.as.interval.qualifier.start = 2U;
    seconds.as.interval.qualifier.end = 5U;
    seconds.as.interval.qualifier.leading = 2U;
    seconds.as.interval.qualifier.fraction = 3U;
    seconds.as.interval.units.low = ((((((((3ULL * 24U) + 4U) * 60U) + 5U) * 60U) + 6U) * 1000000U) + 789000U);
    TRL_CHECK(test, shows(&seconds, "3 04:05:06.789"));
    trl_value months = of(TRL_TAG_INTERVAL);
    months.as.interval.qualifier.start = 0U;
    months.as.interval.qualifier.end = 1U;
    months.as.interval.qualifier.leading = 2U;
    months.as.interval.units.low = UINT64_MAX - 13U;
    months.as.interval.units.high = -1;
    TRL_CHECK(test, shows(&months, "-1-02"));
}

/**
 * Verifies: [CLIENT Copying and exporting] strings as themselves, binary
 * strings in hexadecimal, large objects as their text or what they are,
 * UUIDs and plug-in values as their text.
 */
static void strings_and_objects_are_displayed(trl_test *test) {
    trl_value word = text_of(TRL_TAG_VARYING, "it's");
    TRL_CHECK(test, shows(&word, "it's"));
    trl_value octets = text_of(TRL_TAG_BINARY_VARYING, "\x01\xff");
    TRL_CHECK(test, shows(&octets, "01FF"));
    trl_value clob = of(TRL_TAG_LOB_WHOLE);
    clob.as.lob.kind = TRL_LOB_CHARACTER;
    clob.as.lob.octets.bytes = (const uint8_t *)"long";
    clob.as.lob.octets.length = 4U;
    trl_value blob = clob;
    blob.as.lob.kind = TRL_LOB_BINARY;
    TRL_CHECK(test, shows(&clob, "long") && shows(&blob, "6C6F6E67"));
    trl_value located = of(TRL_TAG_LOB_LOCATOR);
    located.as.locator.characters = 70000U;
    TRL_CHECK(test, shows(&located, "<CHARACTER LARGE OBJECT of 70000 characters>"));
    located.as.locator.kind = TRL_LOB_BINARY;
    located.as.locator.octets = 9U;
    TRL_CHECK(test, shows(&located, "<BINARY LARGE OBJECT of 9 octets>"));
    static const uint8_t id[16] = {
        0x91, 0x91, 0x08, 0xf7, 0x52, 0xd1, 0x43, 0x20, 0x9b, 0xac, 0xf8, 0x47, 0xdb, 0x41, 0x48, 0xa8,
    };
    trl_value uuid = of(TRL_TAG_UUID);
    uuid.as.bytes.bytes = id;
    uuid.as.bytes.length = sizeof id;
    TRL_CHECK(test, shows(&uuid, "919108f7-52d1-4320-9bac-f847db4148a8"));
    trl_value plugin = of(TRL_TAG_PLUGIN);
    plugin.as.plugin.text.bytes = (const uint8_t *)"POINT(1 2)";
    plugin.as.plugin.text.length = 10U;
    TRL_CHECK(test, shows(&plugin, "POINT(1 2)"));
    char cut[4];
    size_t length = 0U;
    TRL_CHECK(test, trl_value_to_text(&word, cut, sizeof cut, &length) == TRL_OK && strcmp(cut, "it'") == 0);
    TRL_CHECK(test, length == 4U && trl_value_to_text(&word, NULL, 0U, NULL) == TRL_OK);
    TRL_CHECK(test, trl_value_to_text(NULL, cut, sizeof cut, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_to_text(&word, NULL, 1U, NULL) == TRL_INVALID_ARGUMENT);
}

/**
 * Verifies: [CLIENT Copying and exporting] an array, a row, a multiset, a
 * structured value and a reference as their literals, each element as its
 * own: a string quoted, a quote in it twice, a datetime, an interval, a
 * DECFLOAT, a JSON value, an XML value and a UUID each as SQL writes one.
 */
static void composites_are_written_as_literals(trl_test *test) {
    trl_value parts[9];
    parts[0] = text_of(TRL_TAG_CHARACTER, "it's");
    parts[1] = of(TRL_TAG_BOOLEAN);
    parts[2] = of(TRL_TAG_DATE);
    parts[3] = of(TRL_TAG_INTERVAL);
    parts[3].as.interval.qualifier.start = 2U;
    parts[3].as.interval.qualifier.end = 2U;
    parts[3].as.interval.qualifier.leading = 2U;
    parts[3].as.interval.units.low = UINT64_MAX - 86400000000U + 1U;
    parts[3].as.interval.units.high = -1;
    parts[4] = of(TRL_TAG_DECFLOAT);
    parts[4].as.decfloat.coefficient.low = 15U;
    parts[4].as.decfloat.exponent = -1;
    parts[5] = text_of(TRL_TAG_XML, "<a>'</a>");
    parts[6] = of(TRL_TAG_NULL);
    parts[7] = text_of(TRL_TAG_BINARY, "\x0a");
    parts[8] = of(TRL_TAG_INTERVAL);
    parts[8].as.interval.qualifier.start = 4U;
    parts[8].as.interval.qualifier.end = 5U;
    parts[8].as.interval.qualifier.leading = 2U;
    parts[8].as.interval.qualifier.fraction = 1U;
    parts[8].as.interval.units.low = 61500000U;
    trl_value row = of(TRL_TAG_ROW);
    row.as.list.items = parts;
    row.as.list.count = 9U;
    TRL_CHECK(test, shows(&row, "ROW('it''s', FALSE, DATE '0001-01-01', INTERVAL -'1' DAY(2), CAST('1.5' AS DECFLOAT), "
                                "XMLPARSE(CONTENT '<a>''</a>' PRESERVE WHITESPACE), NULL, X'0A', "
                                "INTERVAL '1:01.5' MINUTE(2) TO SECOND(1))"));
    trl_value array = of(TRL_TAG_ARRAY);
    array.as.list.items = parts;
    array.as.list.count = 2U;
    trl_value multiset = array;
    multiset.tag = TRL_TAG_MULTISET;
    multiset.as.list.count = 0U;
    TRL_CHECK(test, shows(&array, "ARRAY['it''s', FALSE]") && shows(&multiset, "MULTISET[]"));
}

/**
 * Verifies: [CLIENT Copying and exporting] a structured value written as its
 * type's name and its attributes' literals, a reference as REF of its
 * octets in hexadecimal, and an interval of seconds within an array.
 */
static void user_defined_values_are_written(trl_test *test) {
    trl_value parts[1];
    parts[0] = of(TRL_TAG_INTERVAL);
    parts[0].as.interval.qualifier.start = 5U;
    parts[0].as.interval.qualifier.end = 5U;
    parts[0].as.interval.qualifier.leading = 2U;
    parts[0].as.interval.qualifier.fraction = 1U;
    parts[0].as.interval.units.low = 61500000U;
    trl_value null = of(TRL_TAG_NULL);
    trl_value structured = of(TRL_TAG_STRUCTURED);
    structured.as.structured.type.schema.bytes = (const uint8_t *)"ANN";
    structured.as.structured.type.schema.length = 3U;
    structured.as.structured.type.name.bytes = (const uint8_t *)"POINT";
    structured.as.structured.type.name.length = 5U;
    structured.as.structured.attributes = &null;
    structured.as.structured.count = 1U;
    TRL_CHECK(test, shows(&structured, "ANN.POINT(NULL)"));
    trl_value reference = text_of(TRL_TAG_REFERENCE, "\x01\x02");
    TRL_CHECK(test, shows(&reference, "REF(0102)"));
    trl_value nested = of(TRL_TAG_ARRAY);
    nested.as.list.items = parts;
    nested.as.list.count = 1U;
    TRL_CHECK(test, shows(&nested, "ARRAY[INTERVAL '61.5' SECOND(2, 1)]"));
}

/**
 * Verifies: [CLIENT Copying and exporting] a JSON value, a UUID and large
 * objects sent whole as elements written as their literals: JSON('..'),
 * UUID '..', a quoted string, X'..'; a damaged JSON value shown as what
 * keeps it from having a text.
 */
static void more_elements_are_written_as_literals(trl_test *test) {
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    (void)trl_params_add_json(params, "[1,\"a\"]", 7U);
    static const uint8_t id[16] = {
        0x91, 0x91, 0x08, 0xf7, 0x52, 0xd1, 0x43, 0x20, 0x9b, 0xac, 0xf8, 0x47, 0xdb, 0x41, 0x48, 0xa8,
    };
    (void)trl_params_add_uuid(params, id);
    trl_value parts[4];
    memcpy(parts, trl_params_values(params), 2U * sizeof parts[0]);
    parts[2] = of(TRL_TAG_LOB_WHOLE);
    parts[2].as.lob.kind = TRL_LOB_CHARACTER;
    parts[2].as.lob.octets.bytes = (const uint8_t *)"o'k";
    parts[2].as.lob.octets.length = 3U;
    parts[3] = parts[2];
    parts[3].as.lob.kind = TRL_LOB_BINARY;
    trl_value array = of(TRL_TAG_ARRAY);
    array.as.list.items = parts;
    array.as.list.count = 4U;
    TRL_CHECK(test, shows(&array, "ARRAY[JSON('[1,\"a\"]'), UUID '919108f7-52d1-4320-9bac-f847db4148a8', 'o''k', "
                                  "X'6F276B']"));
    trl_value damaged = text_of(TRL_TAG_JSON, "\x09");
    char text[128];
    TRL_CHECK(test, trl_value_to_text(&damaged, text, sizeof text, NULL) == TRL_OK && text[0] == '<');
    trl_params_free(params);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(numbers_are_displayed),
        TRL_TEST_CASE(datetimes_and_intervals_are_displayed),
        TRL_TEST_CASE(strings_and_objects_are_displayed),
        TRL_TEST_CASE(composites_are_written_as_literals),
        TRL_TEST_CASE(user_defined_values_are_written),
        TRL_TEST_CASE(more_elements_are_written_as_literals),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
