/**
 * @file type_text_test.c
 * @brief Data types written as SQL writes them, as turborel's `DataType`
 * displays one read from the protocol: each tag's form.
 */
#include "codec/buffer.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Whether `type` is written as `expected`. */
static bool written(const trl_data_type *type, const char *expected) {
    char text[256];
    size_t length = 0U;
    bool same = trl_type_to_text(type, text, sizeof text, &length) == TRL_OK && strcmp(text, expected) == 0 &&
                length == strlen(expected);
    if (!same) {
        (void)fprintf(stderr, "written as \"%s\", not \"%s\"\n", text, expected);
    }
    return same;
}

/* A type of `tag` and no more. */
static trl_data_type of(uint8_t tag) {
    trl_data_type type;
    memset(&type, 0, sizeof type);
    type.tag = tag;
    return type;
}

/* A slice of `text`. */
static trl_slice slice_of(const char *text) {
    trl_slice slice = {(const uint8_t *)text, strlen(text)};
    return slice;
}

/* A name of `schema` and `name`. */
static trl_name name_of(const char *schema, const char *name) {
    trl_name named = {slice_of(schema), slice_of(name)};
    return named;
}

/**
 * Verifies: [PROTOCOL Data types] a numeric type is written by its tag,
 * NUMERIC(p,s), FLOAT(p) or DECFLOAT(p), as turborel's client writes one
 * not told its declared type; a string type with its length, OCTETS of a
 * character one counted in octets.
 */
static void numbers_and_strings_are_written(trl_test *test) {
    trl_data_type type = of(TRL_TAG_EXACT);
    type.as.exact.precision = 10U;
    TRL_CHECK(test, written(&type, "NUMERIC(10,0)"));
    type = of(TRL_TAG_APPROXIMATE);
    type.as.precision = 53U;
    TRL_CHECK(test, written(&type, "FLOAT(53)"));
    type = of(TRL_TAG_DECFLOAT);
    type.as.precision = 34U;
    TRL_CHECK(test, written(&type, "DECFLOAT(34)"));
    type = of(TRL_TAG_CHARACTER);
    type.as.character.length = 3U;
    TRL_CHECK(test, written(&type, "CHARACTER(3)"));
    type = of(TRL_TAG_VARYING);
    type.as.character.length = 20U;
    type.as.character.units = TRL_UNITS_OCTETS;
    TRL_CHECK(test, written(&type, "CHARACTER VARYING(20 OCTETS)"));
    type = of(TRL_TAG_BINARY);
    type.as.length = 16U;
    TRL_CHECK(test, written(&type, "BINARY(16)"));
    type = of(TRL_TAG_BINARY_VARYING);
    type.as.length = 8U;
    TRL_CHECK(test, written(&type, "BINARY VARYING(8)"));
    type = of(TRL_TAG_CLOB);
    type.as.lob.length = 1000U;
    TRL_CHECK(test, written(&type, "CHARACTER LARGE OBJECT(1000)"));
    type.as.lob.units = TRL_UNITS_OCTETS;
    TRL_CHECK(test, written(&type, "CHARACTER LARGE OBJECT(1000 OCTETS)"));
    type = of(TRL_TAG_BLOB);
    type.as.lob.length = 2048U;
    TRL_CHECK(test, written(&type, "BINARY LARGE OBJECT(2048)"));
}

/**
 * Verifies: [PROTOCOL Data types] a datetime type with its precision and
 * whether WITH TIME ZONE, an interval with its qualifier, and each type of
 * one form by its name.
 */
static void datetimes_and_the_rest_are_written(trl_test *test) {
    trl_data_type type = of(TRL_TAG_DATE);
    TRL_CHECK(test, written(&type, "DATE"));
    type = of(TRL_TAG_TIME);
    type.as.precision = 3U;
    TRL_CHECK(test, written(&type, "TIME(3)"));
    type.tag = TRL_TAG_TIME_ZONED;
    TRL_CHECK(test, written(&type, "TIME(3) WITH TIME ZONE"));
    type.tag = TRL_TAG_TIMESTAMP;
    TRL_CHECK(test, written(&type, "TIMESTAMP(3)"));
    type.tag = TRL_TAG_TIMESTAMP_ZONED;
    TRL_CHECK(test, written(&type, "TIMESTAMP(3) WITH TIME ZONE"));
    type = of(TRL_TAG_INTERVAL);
    type.as.interval.start = 2U;
    type.as.interval.end = 5U;
    type.as.interval.leading = 2U;
    type.as.interval.fraction = 6U;
    TRL_CHECK(test, written(&type, "INTERVAL DAY(2) TO SECOND(6)"));
    type = of(TRL_TAG_BOOLEAN);
    TRL_CHECK(test, written(&type, "BOOLEAN"));
    type = of(TRL_TAG_JSON);
    TRL_CHECK(test, written(&type, "JSON"));
    type = of(TRL_TAG_UUID);
    TRL_CHECK(test, written(&type, "UUID"));
    type = of(TRL_TAG_PLUGIN);
    TRL_CHECK(test, written(&type, "UNREGISTERED"));
    TRL_CHECK(test, trl_type_to_text(NULL, NULL, 0U, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_type_to_text(&type, NULL, 1U, NULL) == TRL_INVALID_ARGUMENT);
    char cut[4];
    size_t length = 0U;
    TRL_CHECK(test, trl_type_to_text(&type, cut, sizeof cut, &length) == TRL_OK && strcmp(cut, "UNR") == 0 &&
                        length == strlen("UNREGISTERED"));
}

/**
 * Verifies: [PROTOCOL Data types] an array, a multiset and a row type
 * written of their elements' and fields' types; a structured type by its
 * name, a reference with its scope, and an XML type with its modifiers.
 */
static void composites_and_xml_are_written(trl_test *test) {
    trl_data_type element = of(TRL_TAG_EXACT);
    element.as.exact.precision = 5U;
    trl_data_type type = of(TRL_TAG_ARRAY);
    type.as.array.max = 3U;
    type.as.array.element = &element;
    TRL_CHECK(test, written(&type, "NUMERIC(5,0) ARRAY[3]"));
    type = of(TRL_TAG_MULTISET);
    type.as.element = &element;
    TRL_CHECK(test, written(&type, "NUMERIC(5,0) MULTISET"));
    trl_row_field fields[] = {{slice_of("A"), &element}, {slice_of("B"), &element}};
    type = of(TRL_TAG_ROW);
    type.as.row.fields = fields;
    type.as.row.count = 2U;
    TRL_CHECK(test, written(&type, "ROW(A NUMERIC(5,0), B NUMERIC(5,0))"));
    type = of(TRL_TAG_STRUCTURED);
    type.as.structured.name = name_of("S", "POINT");
    TRL_CHECK(test, written(&type, "S.POINT"));
    type = of(TRL_TAG_REFERENCE);
    type.as.reference.referenced.name = name_of("S", "POINT");
    TRL_CHECK(test, written(&type, "REF(S.POINT)"));
    type.as.reference.scoped = true;
    type.as.reference.scope = name_of("S", "POINTS");
    TRL_CHECK(test, written(&type, "REF(S.POINT) SCOPE S.POINTS"));
    type = of(TRL_TAG_XML);
    type.as.xml.primary = TRL_XML_SEQUENCE;
    TRL_CHECK(test, written(&type, "XML(SEQUENCE)"));
    type.as.xml.primary = TRL_XML_DOCUMENT;
    type.as.xml.secondary = TRL_XML_ANY;
    TRL_CHECK(test, written(&type, "XML(DOCUMENT(ANY))"));
    type.as.xml.primary = TRL_XML_CONTENT;
    type.as.xml.secondary = TRL_XML_XMLSCHEMA;
    type.as.xml.schema = name_of("S", "ORDERS");
    TRL_CHECK(test, written(&type, "XML(CONTENT(XMLSCHEMA ID S.ORDERS))"));
    type.as.xml.has_namespace = true;
    TRL_CHECK(test, written(&type, "XML(CONTENT(XMLSCHEMA ID S.ORDERS NO NAMESPACE))"));
    type.as.xml.target_namespace = slice_of("urn:o'k");
    type.as.xml.has_element = true;
    type.as.xml.element = slice_of("a\"b");
    TRL_CHECK(test, written(&type, "XML(CONTENT(XMLSCHEMA ID S.ORDERS NAMESPACE 'urn:o''k' ELEMENT \"a\"\"b\"))"));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(numbers_and_strings_are_written),
        TRL_TEST_CASE(datetimes_and_the_rest_are_written),
        TRL_TEST_CASE(composites_and_xml_are_written),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
