/**
 * @file values_test.c
 * @brief Values read from a reply and written back, each tag at its edges
 * [PROTOCOL Values].
 */
#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/int128.h"
#include "codec/reader.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "fixture.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdint.h>
#include <string.h>

/* Reads one value from the fixture's buffer, which it must fill whole. */
static bool read_one(fixture *f, trl_value *out) {
    trl_reader r;
    trl_reader_init(&r, f->buffer.data, f->buffer.length, &f->arena, &f->error);
    return trl_read_value(&r, out) && trl_reader_finish(&r);
}

/* Writes `value`, reads it back, writes that again, and checks the two
   writings are the same bytes. */
static bool round_trips(fixture *f, const trl_value *value, trl_value *back) {
    fixture_reset(f);
    if (!trl_write_value(&f->buffer, value, &f->error)) {
        return false;
    }
    trl_buffer first = f->buffer;
    trl_buffer_init(&f->buffer);
    trl_put_raw(&f->buffer, first.data, first.length);
    bool read = read_one(f, back);
    trl_buffer again;
    trl_buffer_init(&again);
    bool same = read && trl_write_value(&again, back, &f->error) && again.length == first.length &&
                memcmp(again.data, first.data, first.length) == 0;
    trl_buffer_free(&again);
    trl_buffer_free(&first);
    return same;
}

/**
 * Verifies: [PROTOCOL Values] null is its tag alone; a character string a
 * string, which a CHARACTER VARYING column's values are written as too.
 */
static void null_and_strings(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value null = {TRL_TAG_NULL, {{NULL, 0U}}};
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, round_trips(&f, &null, &back) && back.tag == TRL_TAG_NULL && f.buffer.length == 1U);
    trl_value text = {TRL_TAG_CHARACTER, {text_of("Grace")}};
    TRL_CHECK(test, round_trips(&f, &text, &back) && slice_is(back.as.bytes, "Grace"));
    const uint8_t expected[] = {1, 5, 0, 0, 0, 'G', 'r', 'a', 'c', 'e'};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an exact number is units i128, then scale
 * u32, at the limits of both.
 */
static void exact_numbers(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const trl_decimal numbers[] = {
        {{1U, 0}, 0U},
        {{UINT64_MAX, INT64_MAX}, 0U},
        {{0U, INT64_MIN}, UINT32_MAX},
        {{UINT64_MAX - 124U, -1}, 2U},
    };
    for (size_t i = 0U; i < TRL_TEST_COUNT(numbers); ++i) {
        trl_value value = {TRL_TAG_EXACT, {{NULL, 0U}}};
        value.as.exact = numbers[i];
        trl_value back;
        memset(&back, 0, sizeof back);
        TRL_CHECK(test, round_trips(&f, &value, &back));
        TRL_CHECK(test,
                  trl_int128_equal(back.as.exact.units, numbers[i].units) && back.as.exact.scale == numbers[i].scale);
        TRL_CHECK(test, f.buffer.length == 1U + 16U + 4U);
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an approximate number is its double's bits,
 * kept as they are: NaN, -0.0 and the infinities too.
 */
static void approximate_numbers(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint64_t bits[] = {0x8000000000000000U, 0x7FF8000000000001U, 0x7FF0000000000000U, 0x3FF0000000000000U};
    for (size_t i = 0U; i < TRL_TEST_COUNT(bits); ++i) {
        trl_value value = {TRL_TAG_APPROXIMATE, {{NULL, 0U}}};
        value.as.approximate = bits[i];
        trl_value back;
        memset(&back, 0, sizeof back);
        TRL_CHECK(test, round_trips(&f, &value, &back) && back.as.approximate == bits[i]);
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a plug-in's value is its type's name, its
 * text and its bytes, kept and sent back unchanged.
 */
static void plugin_values(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t raw[] = {0x01, 0xFF, 0x00};
    trl_value value = {TRL_TAG_PLUGIN, {{NULL, 0U}}};
    value.as.plugin.type_name = text_of("GEOHASH");
    value.as.plugin.text = text_of("u4pruydqqvj");
    value.as.plugin.bytes = bytes_of(raw, sizeof raw);
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, round_trips(&f, &value, &back));
    TRL_CHECK(test, slice_is(back.as.plugin.type_name, "GEOHASH") && slice_is(back.as.plugin.text, "u4pruydqqvj"));
    TRL_CHECK(test, back.as.plugin.bytes.length == 3U && back.as.plugin.bytes.bytes[1] == 0xFFU);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] binary strings and references are bytes; XML
 * its serialization, a string; a UUID a count of 16, then the octets.
 */
static void bytes_values(trl_test *test) {
    fixture f;
    fixture_init(&f);
    uint8_t octets[16];
    for (unsigned int i = 0U; i < 16U; ++i) {
        octets[i] = (uint8_t)(i * 17U);
    }
    const uint8_t tags[] = {TRL_TAG_BINARY, TRL_TAG_REFERENCE, TRL_TAG_UUID};
    for (size_t i = 0U; i < TRL_TEST_COUNT(tags); ++i) {
        trl_value value = {tags[i], {bytes_of(octets, sizeof octets)}};
        trl_value back;
        memset(&back, 0, sizeof back);
        TRL_CHECK(test, round_trips(&f, &value, &back) && back.as.bytes.length == 16U);
        TRL_CHECK(test, memcmp(back.as.bytes.bytes, octets, 16U) == 0);
    }
    const uint8_t uuid[] = {27, 16, 0, 0, 0};
    TRL_CHECK(test, memcmp(f.buffer.data, uuid, sizeof uuid) == 0);
    trl_value xml = {TRL_TAG_XML, {text_of("<order no=\"18\"><item>jam</item></order>")}};
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test,
              round_trips(&f, &xml, &back) && slice_is(back.as.bytes, "<order no=\"18\"><item>jam</item></order>"));
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_UUID);
    trl_put_bytes(&f.buffer, bytes_of(octets, 15U));
    TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] dates, times and timestamps carry zoned,
 * precision, the displacement in minutes as an i16 in the low half of a
 * u32, -12:59 to +14:00, and the instant in UTC microseconds; a zoned byte
 * of 1 is a type WITH TIME ZONE, and a displacement past 16 bits fails.
 */
static void datetimes(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const int16_t zones[] = {-779, 0, 840};
    const uint8_t tags[] = {TRL_TAG_DATE, TRL_TAG_TIME, TRL_TAG_TIMESTAMP};
    for (size_t i = 0U; i < TRL_TEST_COUNT(zones); ++i) {
        trl_value value = {tags[i], {{NULL, 0U}}};
        value.as.datetime.zoned = true;
        value.as.datetime.precision = 6U;
        value.as.datetime.zone = zones[i];
        value.as.datetime.utc = -1234567890123LL;
        trl_value back;
        memset(&back, 0, sizeof back);
        TRL_CHECK(test, round_trips(&f, &value, &back) && back.tag == tags[i] && back.as.datetime.zoned);
        TRL_CHECK(test, back.as.datetime.zone == zones[i] && back.as.datetime.utc == -1234567890123LL);
    }
    fixture_reset(&f);
    trl_value minus;
    memset(&minus, 0, sizeof minus);
    trl_value value = {TRL_TAG_TIMESTAMP, {{NULL, 0U}}};
    value.as.datetime.zone = -779;
    TRL_CHECK(test, trl_write_value(&f.buffer, &value, &f.error));
    const uint8_t displacement[] = {0xF5, 0xFC, 0x00, 0x00};
    TRL_CHECK(test, memcmp(f.buffer.data + 3, displacement, sizeof displacement) == 0);
    TRL_CHECK(test, read_one(&f, &minus) && !minus.as.datetime.zoned);
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_TIMESTAMP);
    trl_put_u8(&f.buffer, 1U);
    trl_put_u8(&f.buffer, 0U);
    trl_put_u32(&f.buffer, 0x10000U);
    trl_put_u64(&f.buffer, 0U);
    TRL_CHECK(test, !read_one(&f, &minus) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] an interval is its qualifier, as the type
 * writes it, then its units i128; every qualifier of start and end field.
 */
static void intervals(trl_test *test) {
    fixture f;
    fixture_init(&f);
    for (uint32_t start = TRL_FIELD_YEAR; start <= TRL_FIELD_SECOND; ++start) {
        for (uint32_t end = start; end <= TRL_FIELD_SECOND; ++end) {
            trl_value value = {TRL_TAG_INTERVAL, {{NULL, 0U}}};
            value.as.interval.qualifier.start = start;
            value.as.interval.qualifier.end = end;
            value.as.interval.qualifier.leading = 9U;
            value.as.interval.qualifier.fraction = 6U;
            value.as.interval.units.low = UINT64_MAX;
            value.as.interval.units.high = -1;
            trl_value back;
            memset(&back, 0, sizeof back);
            TRL_CHECK(test, round_trips(&f, &value, &back) && back.as.interval.qualifier.end == end);
            TRL_CHECK(test, trl_int128_equal(back.as.interval.units, value.as.interval.units));
        }
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a truth value is 0 or 1; another byte fails.
 */
static void truth_values(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value value = {TRL_TAG_BOOLEAN, {{NULL, 0U}}};
    value.as.truth = true;
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, round_trips(&f, &value, &back) && back.as.truth);
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_BOOLEAN);
    trl_put_u8(&f.buffer, 2U);
    TRL_CHECK(test, !read_one(&f, &back) && failed_value(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a DECFLOAT is coefficient i128, then exponent
 * i32 as u32, canonical as turborel makes it: 34 digits at most, no
 * trailing zero, zero of exponent 0, its adjusted exponent -6143 to 6144.
 */
static void decfloats(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value value = {TRL_TAG_DECFLOAT, {{NULL, 0U}}};
    value.as.decfloat.coefficient.low = 15U;
    value.as.decfloat.exponent = -1;
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, round_trips(&f, &value, &back) && back.as.decfloat.exponent == -1);
    /* Its edges: the greatest and least adjusted exponents. */
    value.as.decfloat.coefficient.low = 1U;
    value.as.decfloat.exponent = 6144;
    TRL_CHECK(test, trl_decfloat_canonical(value.as.decfloat));
    value.as.decfloat.exponent = -6143;
    TRL_CHECK(test, trl_decfloat_canonical(value.as.decfloat));
    value.as.decfloat.exponent = 6145;
    TRL_CHECK(test, !trl_decfloat_canonical(value.as.decfloat));
    value.as.decfloat.exponent = -6144;
    TRL_CHECK(test, !trl_decfloat_canonical(value.as.decfloat));
    value.as.decfloat.coefficient.low = 10U;
    value.as.decfloat.exponent = 0;
    TRL_CHECK(test, !trl_decfloat_canonical(value.as.decfloat));
    value.as.decfloat.coefficient.low = 0U;
    value.as.decfloat.exponent = 3;
    TRL_CHECK(test, !trl_decfloat_canonical(value.as.decfloat));
    /* 10^34 - 1 has 34 digits; 10^34 + 1 has 35. */
    trl_decimal digits34 = {{0U, 0}, 0U};
    trl_decimal digits35 = {{0U, 0}, 0U};
    TRL_CHECK(test, trl_decimal_parse("9999999999999999999999999999999999", &digits34) == TRL_OK);
    TRL_CHECK(test, trl_decimal_parse("10000000000000000000000000000000001", &digits35) == TRL_OK);
    value.as.decfloat.exponent = 0;
    value.as.decfloat.coefficient = digits34.units;
    TRL_CHECK(test, trl_decfloat_canonical(value.as.decfloat));
    value.as.decfloat.coefficient = digits35.units;
    TRL_CHECK(test, !trl_decfloat_canonical(value.as.decfloat));
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_DECFLOAT);
    trl_put_i128(&f.buffer, digits35.units);
    trl_put_u32(&f.buffer, 0U);
    TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    fixture_free(&f);
}

/* The DECFLOAT turborel makes of the digits `text` and `exponent`. */
static bool made(const char *text, int32_t exponent, trl_decfloat *out) {
    trl_decimal digits = {{0U, 0}, 0U};
    trl_decfloat value = {{0U, 0}, exponent};
    if (trl_decimal_parse(text, &digits) != TRL_OK) {
        return false;
    }
    value.coefficient = digits.units;
    return trl_decfloat_made(value, out);
}

/* Whether `made` has the digits `text` and the exponent `exponent`. */
static bool is(const trl_decfloat *made_value, const char *text, int32_t exponent) {
    trl_decimal digits = {{0U, 0}, 0U};
    return trl_decimal_parse(text, &digits) == TRL_OK && trl_int128_equal(made_value->coefficient, digits.units) &&
           made_value->exponent == exponent;
}

/**
 * Verifies: [PROTOCOL Values] a DECFLOAT of more than 34 digits is made by
 * rounding half to even, as turborel makes one: a dropped digit above 5
 * rounds up, 5 rounds to even unless digits after it are not all zero, and
 * a carry to 35 digits is taken into the exponent; an exponent past 32
 * bits makes none.
 */
static void decfloats_round_half_even(trl_test *test) {
    trl_decfloat out = {{0U, 0}, 0};
    TRL_CHECK(test, made("12345678901234567890123456789012346", 0, &out) &&
                        is(&out, "1234567890123456789012345678901235", 1));
    TRL_CHECK(test, made("12345678901234567890123456789012345", 0, &out) &&
                        is(&out, "1234567890123456789012345678901234", 1));
    TRL_CHECK(test, made("12345678901234567890123456789012335", 0, &out) &&
                        is(&out, "1234567890123456789012345678901234", 1));
    TRL_CHECK(test, made("123456789012345678901234567890123451", 0, &out) &&
                        is(&out, "1234567890123456789012345678901235", 2));
    TRL_CHECK(test, made("99999999999999999999999999999999999", 0, &out) && is(&out, "1", 35));
    TRL_CHECK(test, made("-12345678901234567890123456789012346", 0, &out) &&
                        is(&out, "-1234567890123456789012345678901235", 1));
    TRL_CHECK(test, !made("12345678901234567890123456789012345", INT32_MAX, &out));
    TRL_CHECK(test, made("1000", INT32_MIN + 6143, &out) == false);
}

/**
 * Verifies: [PROTOCOL Values] arrays, rows and multisets are lists of
 * values, nested, a structured value its type's names then its
 * attributes.
 */
static void constructed_values(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value leaves[2] = {{TRL_TAG_NULL, {{NULL, 0U}}}, {TRL_TAG_CHARACTER, {text_of("x")}}};
    trl_value row = {TRL_TAG_ROW, {{NULL, 0U}}};
    row.as.list.items = leaves;
    row.as.list.count = 2U;
    trl_value multiset = {TRL_TAG_MULTISET, {{NULL, 0U}}};
    multiset.as.list.items = &row;
    multiset.as.list.count = 1U;
    trl_value structured = {TRL_TAG_STRUCTURED, {{NULL, 0U}}};
    structured.as.structured.type.schema = text_of("S");
    structured.as.structured.type.name = text_of("ADDRESS");
    structured.as.structured.attributes = &multiset;
    structured.as.structured.count = 1U;
    trl_value array = {TRL_TAG_ARRAY, {{NULL, 0U}}};
    array.as.list.items = &structured;
    array.as.list.count = 1U;
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, round_trips(&f, &array, &back) && back.as.list.count == 1U);
    if (back.as.list.items == NULL) {
        fixture_free(&f);
        return;
    }
    const trl_value *s = &back.as.list.items[0];
    TRL_CHECK(test, s->tag == TRL_TAG_STRUCTURED && slice_is(s->as.structured.type.name, "ADDRESS"));
    const trl_value *r = &s->as.structured.attributes[0].as.list.items[0];
    TRL_CHECK(test, r->tag == TRL_TAG_ROW && r->as.list.count == 2U && slice_is(r->as.list.items[1].as.bytes, "x"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] values nest 16 arrays deep, as turborel's
 * MAX_TYPE_NESTING, and a value nested 17 deep is refused.
 */
static void nested_values_are_bounded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value back;
    memset(&back, 0, sizeof back);
    for (unsigned int depth = 16U; depth <= 17U; ++depth) {
        fixture_reset(&f);
        for (unsigned int i = 0U; i < depth; ++i) {
            trl_put_u8(&f.buffer, TRL_TAG_ARRAY);
            trl_put_u32(&f.buffer, 1U);
        }
        trl_put_u8(&f.buffer, TRL_TAG_NULL);
        bool read = read_one(&f, &back);
        TRL_CHECK(test, depth == 16U ? read : (!read && failed_connection(&f)));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a multiset of more elements than turborel's
 * greatest cardinality, 65535, is refused.
 */
static void a_multiset_is_bounded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, TRL_TAG_MULTISET);
    trl_put_u32(&f.buffer, 65536U);
    for (unsigned int i = 0U; i < 65536U; ++i) {
        trl_put_u8(&f.buffer, TRL_TAG_NULL);
    }
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a large object whole is its kind, 0
 * character or 1 binary, then its octets, a character one's UTF-8; by
 * locator, its kind, locator, octets and characters; another kind fails.
 */
static void large_objects(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value whole = {TRL_TAG_LOB_WHOLE, {{NULL, 0U}}};
    whole.as.lob.kind = TRL_LOB_CHARACTER;
    whole.as.lob.octets = text_of("tea");
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, round_trips(&f, &whole, &back) && back.as.lob.kind == 0U && slice_is(back.as.lob.octets, "tea"));
    trl_value locator = {TRL_TAG_LOB_LOCATOR, {{NULL, 0U}}};
    locator.as.locator.kind = TRL_LOB_BINARY;
    locator.as.locator.locator = 7U;
    locator.as.locator.octets = 4194304U;
    locator.as.locator.characters = 0U;
    TRL_CHECK(test, round_trips(&f, &locator, &back) && back.as.locator.locator == 7U);
    TRL_CHECK(test, back.as.locator.octets == 4194304U && f.buffer.length == 22U);
    const uint8_t no_kind[] = {TRL_TAG_LOB_WHOLE, 2, 0, 0, 0, 0};
    const uint8_t not_utf8[] = {TRL_TAG_LOB_WHOLE, 0, 1, 0, 0, 0, 0xFF};
    const uint8_t binary[] = {TRL_TAG_LOB_WHOLE, 1, 1, 0, 0, 0, 0xFF};
    fixture_reset(&f);
    trl_put_raw(&f.buffer, no_kind, sizeof no_kind);
    TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    fixture_reset(&f);
    trl_put_raw(&f.buffer, not_utf8, sizeof not_utf8);
    TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    fixture_reset(&f);
    trl_put_raw(&f.buffer, binary, sizeof binary);
    TRL_CHECK(test, read_one(&f, &back));
    fixture_free(&f);
}

/* Writes `value` and reads it back into `back`. */
static bool written_and_read(fixture *f, const trl_value *value, trl_value *back) {
    fixture_reset(f);
    if (!trl_write_value(&f->buffer, value, &f->error)) {
        return false;
    }
    trl_buffer written = f->buffer;
    trl_buffer_init(&f->buffer);
    trl_put_raw(&f->buffer, written.data, written.length);
    trl_buffer_free(&written);
    return read_one(f, back);
}

/**
 * Verifies: [PROTOCOL Values] value tags 5, 7, 10 and 12, the types' own,
 * read as 1, 6, 9 and 11, as turborel-protocol's reader takes them: a
 * string, bytes, a time and a timestamp, their bodies the same, the zone
 * told inside.
 */
static void type_tags_as_values(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value back;
    memset(&back, 0, sizeof back);
    trl_value varying = {TRL_TAG_VARYING, {text_of("v")}};
    trl_value bytes = {TRL_TAG_BINARY_VARYING, {text_of("b")}};
    trl_value zoned = {TRL_TAG_TIMESTAMP_ZONED, {{NULL, 0U}}};
    zoned.as.datetime.zoned = true;
    zoned.as.datetime.zone = 60;
    trl_value time = {TRL_TAG_TIME_ZONED, {{NULL, 0U}}};
    time.as.datetime.zoned = true;
    TRL_CHECK(test,
              written_and_read(&f, &varying, &back) && back.tag == TRL_TAG_CHARACTER && slice_is(back.as.bytes, "v"));
    TRL_CHECK(test, written_and_read(&f, &bytes, &back) && back.tag == TRL_TAG_BINARY && slice_is(back.as.bytes, "b"));
    TRL_CHECK(test, written_and_read(&f, &zoned, &back) && back.tag == TRL_TAG_TIMESTAMP && back.as.datetime.zoned &&
                        back.as.datetime.zone == 60);
    TRL_CHECK(test, written_and_read(&f, &time, &back) && back.tag == TRL_TAG_TIME && back.as.datetime.zoned);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a value under tag 18 or 19, a large
 * object's stored form, fails its request alone (SQLSTATE TB916, RULES.md
 * D8); a tag no value has fails the connection; one to send under 18 or
 * 19 is refused.
 */
static void type_tags_malformed_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value back;
    memset(&back, 0, sizeof back);
    const uint8_t kept[] = {TRL_TAG_CLOB, 0, 1, 0, 0, 0, 'a'};
    const uint8_t not_utf8[] = {TRL_TAG_BLOB, 0, 1, 0, 0, 0, 0xFF};
    const uint8_t unknown[] = {28U, 99U};
    fixture_reset(&f);
    trl_put_raw(&f.buffer, kept, sizeof kept);
    TRL_CHECK(test, !read_one(&f, &back) && failed_value(&f));
    fixture_reset(&f);
    trl_put_raw(&f.buffer, not_utf8, sizeof not_utf8);
    TRL_CHECK(test, !read_one(&f, &back) && failed_value(&f));
    for (size_t i = 0U; i < sizeof unknown; ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, unknown[i]);
        TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    }
    trl_value clob = {TRL_TAG_CLOB, {{NULL, 0U}}};
    clob.as.lob.octets = text_of("tea");
    fixture_reset(&f);
    TRL_CHECK(test, !trl_write_value(&f.buffer, &clob, &f.error) && f.error.sqlcode == -910);
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(null_and_strings),
        TRL_TEST_CASE(exact_numbers),
        TRL_TEST_CASE(approximate_numbers),
        TRL_TEST_CASE(plugin_values),
        TRL_TEST_CASE(bytes_values),
        TRL_TEST_CASE(datetimes),
        TRL_TEST_CASE(intervals),
        TRL_TEST_CASE(truth_values),
        TRL_TEST_CASE(decfloats),
        TRL_TEST_CASE(decfloats_round_half_even),
        TRL_TEST_CASE(constructed_values),
        TRL_TEST_CASE(nested_values_are_bounded),
        TRL_TEST_CASE(a_multiset_is_bounded),
        TRL_TEST_CASE(large_objects),
        TRL_TEST_CASE(type_tags_as_values),
        TRL_TEST_CASE(type_tags_malformed_fail),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
