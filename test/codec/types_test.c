/**
 * @file types_test.c
 * @brief Data types read from a reply, each tag's parameters, the nested
 * ones bounded, a malformed one refused [PROTOCOL Data types].
 */
#include "codec/buffer.h"
#include "codec/reader.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "fixture.h"
#include "harness.h"

#include <stddef.h>
#include <stdint.h>

/* Reads one type from the fixture's buffer, which it must fill whole. */
static bool read_type(fixture *f, trl_data_type *out) {
    trl_reader r;
    trl_reader_init(&r, f->buffer.data, f->buffer.length, &f->arena, &f->error);
    return trl_read_data_type(&r, out) && trl_reader_finish(&r);
}

/* Appends a schema and a name. */
static void put_name(trl_buffer *b, const char *schema, const char *name) {
    trl_put_bytes(b, text_of(schema));
    trl_put_bytes(b, text_of(name));
}

/**
 * Verifies: [PROTOCOL Data types] CHARACTER and CHARACTER VARYING carry a
 * length and units, 0 characters or 1 octets; other units fail.
 */
static void character_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t tags[] = {TRL_TAG_CHARACTER, TRL_TAG_VARYING};
    for (size_t i = 0U; i < TRL_TEST_COUNT(tags); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, tags[i]);
        trl_put_u32(&f.buffer, 40U);
        trl_put_u8(&f.buffer, TRL_UNITS_OCTETS);
        trl_data_type t;
        TRL_CHECK(test, read_type(&f, &t));
        TRL_CHECK(test, t.tag == tags[i] && t.as.character.length == 40U && t.as.character.units == TRL_UNITS_OCTETS);
    }
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_CHARACTER);
    trl_put_u32(&f.buffer, 1U);
    trl_put_u8(&f.buffer, 2U);
    trl_data_type t;
    TRL_CHECK(test, !read_type(&f, &t) && failed_value(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] the numbers, binary strings, datetimes
 * and DECFLOAT carry their precision, scale or length; BOOLEAN, JSON and
 * UUID (tag 27) nothing.
 */
static void sized_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_data_type t;
    trl_put_u8(&f.buffer, TRL_TAG_EXACT);
    trl_put_u32(&f.buffer, 18U);
    trl_put_u32(&f.buffer, 2U);
    TRL_CHECK(test, read_type(&f, &t) && t.as.exact.precision == 18U && t.as.exact.scale == 2U);
    const uint8_t precision[] = {
        TRL_TAG_APPROXIMATE,     TRL_TAG_DATE,     TRL_TAG_TIME, TRL_TAG_TIME_ZONED, TRL_TAG_TIMESTAMP,
        TRL_TAG_TIMESTAMP_ZONED, TRL_TAG_DECFLOAT,
    };
    for (size_t i = 0U; i < TRL_TEST_COUNT(precision); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, precision[i]);
        trl_put_u32(&f.buffer, 6U);
        TRL_CHECK(test, read_type(&f, &t) && t.tag == precision[i] && t.as.precision == 6U);
    }
    const uint8_t binary[] = {TRL_TAG_BINARY, TRL_TAG_BINARY_VARYING};
    for (size_t i = 0U; i < TRL_TEST_COUNT(binary); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, binary[i]);
        trl_put_u32(&f.buffer, 16U);
        TRL_CHECK(test, read_type(&f, &t) && t.as.length == 16U);
    }
    const uint8_t bare[] = {TRL_TAG_BOOLEAN, TRL_TAG_JSON, TRL_TAG_UUID};
    for (size_t i = 0U; i < TRL_TEST_COUNT(bare); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, bare[i]);
        TRL_CHECK(test, read_type(&f, &t) && t.tag == bare[i]);
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] CLOB and BLOB carry a length u64 and
 * units.
 */
static void large_object_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t tags[] = {TRL_TAG_CLOB, TRL_TAG_BLOB};
    for (size_t i = 0U; i < TRL_TEST_COUNT(tags); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, tags[i]);
        trl_put_u64(&f.buffer, 4294967296U);
        trl_put_u8(&f.buffer, TRL_UNITS_CHARACTERS);
        trl_data_type t;
        TRL_CHECK(test, read_type(&f, &t) && t.as.lob.length == 4294967296U && t.as.lob.units == 0U);
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] an interval carries its start and end
 * fields, 0 YEAR to 5 SECOND, and its precisions; a field past 5 fails.
 */
static void interval_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_data_type t;
    for (uint32_t start = TRL_FIELD_YEAR; start <= TRL_FIELD_SECOND; ++start) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, TRL_TAG_INTERVAL);
        trl_put_u32(&f.buffer, start);
        trl_put_u32(&f.buffer, TRL_FIELD_SECOND);
        trl_put_u32(&f.buffer, 2U);
        trl_put_u32(&f.buffer, 6U);
        TRL_CHECK(test, read_type(&f, &t) && t.as.interval.start == start && t.as.interval.fraction == 6U);
    }
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_INTERVAL);
    trl_put_u32(&f.buffer, TRL_FIELD_DAY);
    trl_put_u32(&f.buffer, TRL_FIELD_SECOND + 1U);
    trl_put_u32(&f.buffer, 2U);
    trl_put_u32(&f.buffer, 0U);
    TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] a plug-in's type carries its name and at
 * most two parameters, read by its name whether or not the client has it;
 * more parameters fail, and so does one of u32's most, which turborel's
 * parameters take as none.
 */
static void plugin_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, TRL_TAG_PLUGIN);
    trl_put_bytes(&f.buffer, text_of("GEOHASH"));
    trl_put_u32(&f.buffer, 2U);
    trl_put_u32(&f.buffer, 12U);
    trl_put_u32(&f.buffer, 4326U);
    trl_data_type t;
    TRL_CHECK(test, read_type(&f, &t) && slice_is(t.as.plugin.name, "GEOHASH"));
    TRL_CHECK(test, t.as.plugin.param_count == 2U && t.as.plugin.params[1] == 4326U);
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_PLUGIN);
    trl_put_bytes(&f.buffer, text_of("X"));
    trl_put_u32(&f.buffer, 3U);
    trl_put_u32(&f.buffer, 1U);
    trl_put_u32(&f.buffer, 2U);
    trl_put_u32(&f.buffer, 3U);
    TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_PLUGIN);
    trl_put_bytes(&f.buffer, text_of("X"));
    trl_put_u32(&f.buffer, 1U);
    trl_put_u32(&f.buffer, UINT32_MAX);
    TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] arrays, multisets and rows nest: an
 * array's cardinality 1 to 65535 and element type, a multiset's element
 * type, a row's named fields, one or more.
 */
static void constructed_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, TRL_TAG_ARRAY);
    trl_put_u32(&f.buffer, 65535U);
    trl_put_u8(&f.buffer, TRL_TAG_MULTISET);
    trl_put_u8(&f.buffer, TRL_TAG_ROW);
    trl_put_u32(&f.buffer, 2U);
    trl_put_bytes(&f.buffer, text_of("A"));
    trl_put_u8(&f.buffer, TRL_TAG_BOOLEAN);
    trl_put_bytes(&f.buffer, text_of("B"));
    trl_put_u8(&f.buffer, TRL_TAG_UUID);
    trl_data_type t;
    TRL_CHECK(test, read_type(&f, &t) && t.as.array.max == 65535U);
    const trl_data_type *multiset = t.as.array.element;
    TRL_CHECK(test, multiset->tag == TRL_TAG_MULTISET && multiset->as.element->tag == TRL_TAG_ROW);
    const trl_data_type *row = multiset->as.element;
    TRL_CHECK(test, row->as.row.count == 2U && slice_is(row->as.row.fields[1].name, "B"));
    TRL_CHECK(test, row->as.row.fields[1].type->tag == TRL_TAG_UUID);
    const uint32_t cardinalities[] = {0U, 65536U};
    for (size_t i = 0U; i < TRL_TEST_COUNT(cardinalities); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, TRL_TAG_ARRAY);
        trl_put_u32(&f.buffer, cardinalities[i]);
        trl_put_u8(&f.buffer, TRL_TAG_BOOLEAN);
        TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    }
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_ROW);
    trl_put_u32(&f.buffer, 0U);
    TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] types nest 16 deep, as turborel's
 * MAX_TYPE_NESTING, and no deeper.
 */
static void nesting_is_bounded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    for (unsigned int depth = 16U; depth <= 17U; ++depth) {
        fixture_reset(&f);
        for (unsigned int i = 0U; i < depth; ++i) {
            trl_put_u8(&f.buffer, TRL_TAG_MULTISET);
        }
        trl_put_u8(&f.buffer, TRL_TAG_BOOLEAN);
        trl_data_type t;
        bool read = read_type(&f, &t);
        TRL_CHECK(test, depth == 16U ? read : (!read && failed_connection(&f)));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] a structured type carries its schema and
 * name and its proper supertypes; a reference type the referenced type's
 * names and its scope, flagged 0 or 1.
 */
static void structured_and_reference_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, TRL_TAG_STRUCTURED);
    put_name(&f.buffer, "S", "ADDRESS");
    trl_put_u32(&f.buffer, 1U);
    put_name(&f.buffer, "S", "PLACE");
    trl_data_type t;
    TRL_CHECK(test, read_type(&f, &t) && slice_is(t.as.structured.name.name, "ADDRESS"));
    TRL_CHECK(test, t.as.structured.supertype_count == 1U && slice_is(t.as.structured.supertypes[0].name, "PLACE"));
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_REFERENCE);
    put_name(&f.buffer, "S", "PERSON");
    trl_put_u32(&f.buffer, 0U);
    trl_put_u8(&f.buffer, 1U);
    put_name(&f.buffer, "S", "PEOPLE");
    TRL_CHECK(test, read_type(&f, &t) && t.as.reference.scoped && slice_is(t.as.reference.scope.name, "PEOPLE"));
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_REFERENCE);
    put_name(&f.buffer, "S", "PERSON");
    trl_put_u32(&f.buffer, 0U);
    trl_put_u8(&f.buffer, 2U);
    TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] XML carries its primary and secondary
 * modifiers, and of XMLSCHEMA the schema's names and an optional namespace
 * and element; SEQUENCE is untyped, and other modifiers fail.
 */
static void xml_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, TRL_TAG_XML);
    trl_put_u8(&f.buffer, TRL_XML_DOCUMENT);
    trl_put_u8(&f.buffer, TRL_XML_XMLSCHEMA);
    put_name(&f.buffer, "S", "ORDERS");
    trl_put_u8(&f.buffer, 1U);
    trl_put_bytes(&f.buffer, text_of("urn:orders"));
    trl_put_u8(&f.buffer, 0U);
    trl_data_type t;
    TRL_CHECK(test, read_type(&f, &t) && t.as.xml.has_namespace && !t.as.xml.has_element);
    TRL_CHECK(test, slice_is(t.as.xml.target_namespace, "urn:orders") && slice_is(t.as.xml.schema.name, "ORDERS"));
    const uint8_t bad[][2] = {{TRL_XML_SEQUENCE, TRL_XML_ANY}, {3U, TRL_XML_UNTYPED}, {TRL_XML_CONTENT, 3U}};
    for (size_t i = 0U; i < TRL_TEST_COUNT(bad); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, TRL_TAG_XML);
        trl_put_u8(&f.buffer, bad[i][0]);
        trl_put_u8(&f.buffer, bad[i][1]);
        TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    }
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_XML);
    trl_put_u8(&f.buffer, TRL_XML_SEQUENCE);
    trl_put_u8(&f.buffer, TRL_XML_UNTYPED);
    TRL_CHECK(test, read_type(&f, &t) && t.as.xml.primary == TRL_XML_SEQUENCE);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Data types] a tag no type has (20, 21, 28, 99) fails
 * the connection.
 */
static void unknown_tags_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t tags[] = {0U, TRL_TAG_LOB_WHOLE, TRL_TAG_LOB_LOCATOR, 28U, 99U};
    for (size_t i = 0U; i < TRL_TEST_COUNT(tags); ++i) {
        fixture_reset(&f);
        trl_put_u8(&f.buffer, tags[i]);
        trl_data_type t;
        TRL_CHECK(test, !read_type(&f, &t) && failed_connection(&f));
    }
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(character_types),    TRL_TEST_CASE(sized_types),
        TRL_TEST_CASE(large_object_types), TRL_TEST_CASE(interval_types),
        TRL_TEST_CASE(plugin_types),       TRL_TEST_CASE(constructed_types),
        TRL_TEST_CASE(nesting_is_bounded), TRL_TEST_CASE(structured_and_reference_types),
        TRL_TEST_CASE(xml_types),          TRL_TEST_CASE(unknown_tags_fail),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
