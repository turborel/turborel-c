/**
 * @file values_test.c
 * @brief Values read by their kind and sent as parameters
 * [CLIENT Running statements] [CLIENT Prepared statements], and UUIDs'
 * octets and text [CLIENT UUIDs].
 */
#include "codec/buffer.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/values.h"
#include "data/params.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define UUID_TEXT "919108f7-52d1-4320-9bac-f847db4148a8"

/* A value whose body is `text`, of `tag`. */
static trl_value of_bytes(uint8_t tag, const char *text) {
    trl_value value;
    memset(&value, 0, sizeof value);
    value.tag = tag;
    value.as.bytes.bytes = (const uint8_t *)text;
    value.as.bytes.length = strlen(text);
    return value;
}

/**
 * Verifies: [CLIENT Running statements] each value read by its kind:
 * character, binary, exact, approximate, boolean; another kind refused.
 */
static void each_value_is_read_by_its_kind(trl_test *test) {
    trl_value text = of_bytes(TRL_TAG_VARYING, "Ada");
    size_t length = 0U;
    TRL_CHECK(test, strncmp(trl_value_text(&text, &length), "Ada", 3U) == 0 && length == 3U);
    TRL_CHECK(test, trl_value_bytes(&text, NULL) == NULL && trl_value_kind(&text) == TRL_KIND_CHARACTER_VARYING);
    trl_value binary = of_bytes(TRL_TAG_BINARY, "\x01\x02");
    TRL_CHECK(test,
              trl_value_bytes(&binary, &length)[1] == 2U && length == 2U && trl_value_text(&binary, NULL) == NULL);
    trl_value empty = of_bytes(TRL_TAG_CHARACTER, "");
    empty.as.bytes.bytes = NULL;
    TRL_CHECK(test, trl_value_text(&empty, &length) != NULL && length == 0U);
    trl_value exact;
    memset(&exact, 0, sizeof exact);
    exact.tag = TRL_TAG_EXACT;
    exact.as.exact.units.low = 150U;
    exact.as.exact.scale = 2U;
    trl_decimal decimal;
    TRL_CHECK(test, trl_value_decimal(&exact, &decimal) == TRL_OK && decimal.units.low == 150U);
    double approximate = 0.0;
    TRL_CHECK(test, trl_value_double(&exact, &approximate) == TRL_INVALID_ARGUMENT);
    trl_value approx;
    memset(&approx, 0, sizeof approx);
    approx.tag = TRL_TAG_APPROXIMATE;
    double half = 0.5;
    memcpy(&approx.as.approximate, &half, sizeof half);
    TRL_CHECK(test, trl_value_double(&approx, &approximate) == TRL_OK && approximate == half);
    trl_value truth;
    memset(&truth, 0, sizeof truth);
    truth.tag = TRL_TAG_BOOLEAN;
    truth.as.truth = true;
    bool read = false;
    TRL_CHECK(test, trl_value_boolean(&truth, &read) == TRL_OK && read);
}

/**
 * Verifies: [CLIENT Running statements] datetime, interval, DECFLOAT and a
 * plug-in's values read by their kind; null read as null.
 */
static void other_values_are_read_by_their_kind(trl_test *test) {
    size_t length = 0U;
    trl_value stamp;
    memset(&stamp, 0, sizeof stamp);
    stamp.tag = TRL_TAG_TIMESTAMP_ZONED;
    stamp.as.datetime.zone = 60;
    trl_datetime datetime;
    TRL_CHECK(test, trl_value_datetime(&stamp, &datetime) == TRL_OK && datetime.zone == 60);
    trl_value text = of_bytes(TRL_TAG_VARYING, "Ada");
    TRL_CHECK(test, trl_value_datetime(&text, &datetime) == TRL_INVALID_ARGUMENT);
    trl_value interval;
    memset(&interval, 0, sizeof interval);
    interval.tag = TRL_TAG_INTERVAL;
    trl_interval read_interval;
    TRL_CHECK(test, trl_value_interval(&interval, &read_interval) == TRL_OK);
    trl_value decfloat;
    memset(&decfloat, 0, sizeof decfloat);
    decfloat.tag = TRL_TAG_DECFLOAT;
    trl_decfloat read_decfloat;
    TRL_CHECK(test, trl_value_decfloat(&decfloat, &read_decfloat) == TRL_OK);
    trl_value plugin;
    memset(&plugin, 0, sizeof plugin);
    plugin.tag = TRL_TAG_PLUGIN;
    plugin.as.plugin.text.bytes = (const uint8_t *)"POINT(1 2)";
    plugin.as.plugin.text.length = 10U;
    TRL_CHECK(test, trl_value_text(&plugin, &length) != NULL && length == 10U);
    TRL_CHECK(test, trl_value_bytes(&plugin, &length) != NULL && length == 0U);
    TRL_CHECK(test, trl_value_is_null(NULL) && trl_value_kind(NULL) == TRL_KIND_NULL);
    TRL_CHECK(test, trl_value_text(NULL, NULL) == NULL && trl_value_bytes(NULL, NULL) == NULL);
}

/**
 * Verifies: [CLIENT Running statements] an array's, a row's, a multiset's
 * elements and a structured value's attributes, by their places; a large
 * object sent whole by its kind.
 */
static void values_of_values(trl_test *test) {
    trl_value items[2];
    items[0] = of_bytes(TRL_TAG_CHARACTER, "a");
    items[1] = of_bytes(TRL_TAG_CHARACTER, "b");
    trl_value array;
    memset(&array, 0, sizeof array);
    array.tag = TRL_TAG_ARRAY;
    array.as.list.items = items;
    array.as.list.count = 2U;
    TRL_CHECK(test, trl_value_count(&array) == 2U && trl_value_at(&array, 1U) == &items[1]);
    TRL_CHECK(test, trl_value_at(&array, 2U) == NULL && trl_value_count(&items[0]) == 0U);
    trl_value structured;
    memset(&structured, 0, sizeof structured);
    structured.tag = TRL_TAG_STRUCTURED;
    structured.as.structured.attributes = items;
    structured.as.structured.count = 1U;
    TRL_CHECK(test, trl_value_count(&structured) == 1U && trl_value_at(NULL, 0U) == NULL);
    trl_value clob;
    memset(&clob, 0, sizeof clob);
    clob.tag = TRL_TAG_LOB_WHOLE;
    clob.as.lob.kind = TRL_LOB_CHARACTER;
    clob.as.lob.octets.bytes = (const uint8_t *)"text";
    clob.as.lob.octets.length = 4U;
    size_t length = 0U;
    TRL_CHECK(test, trl_value_text(&clob, &length) != NULL && length == 4U && trl_value_bytes(&clob, NULL) == NULL);
    clob.as.lob.kind = TRL_LOB_BINARY;
    TRL_CHECK(test, trl_value_bytes(&clob, &length) != NULL && trl_value_text(&clob, NULL) == NULL);
}

/**
 * Verifies: [CLIENT UUIDs] a UUID's octets and its text, either case,
 * with hyphens or none; text of no UUID refused.
 */
static void a_uuid_is_octets_and_text(trl_test *test) {
    uint8_t octets[TRL_UUID_BYTES];
    char text[TRL_UUID_TEXT_BYTES];
    TRL_CHECK(test, trl_uuid_parse(UUID_TEXT, octets) == TRL_OK && octets[0] == 0x91U);
    TRL_CHECK(test, trl_uuid_text(octets, text) == TRL_OK && strcmp(text, UUID_TEXT) == 0);
    TRL_CHECK(test, trl_uuid_parse("919108F752D143209BACF847DB4148A8", octets) == TRL_OK && octets[15] == 0xa8U);
    TRL_CHECK(test, trl_uuid_parse("919108f7-52d1-4320-9bac-f847db4148a", octets) == TRL_INVALID_TEXT);
    TRL_CHECK(test, trl_uuid_parse("919108f7x52d1-4320-9bac-f847db4148a8", octets) == TRL_INVALID_TEXT);
    TRL_CHECK(test, trl_uuid_parse("g19108f752d143209bacf847db4148a8", octets) == TRL_INVALID_TEXT);
    TRL_CHECK(test, trl_uuid_parse(NULL, octets) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_uuid_text(NULL, text) == TRL_INVALID_ARGUMENT);
    trl_value uuid;
    memset(&uuid, 0, sizeof uuid);
    uuid.tag = TRL_TAG_UUID;
    uuid.as.bytes.bytes = octets;
    uuid.as.bytes.length = sizeof octets;
    uint8_t read[TRL_UUID_BYTES];
    TRL_CHECK(test, trl_value_uuid(&uuid, read) == TRL_OK && memcmp(read, octets, sizeof read) == 0);
    uuid.as.bytes.length = 3U;
    TRL_CHECK(test, trl_value_uuid(&uuid, read) == TRL_INVALID_ARGUMENT);
}

/**
 * Verifies: [CLIENT Prepared statements] parameters of each kind, a JSON
 * value made of its text, a value copied whole, as many as wanted, and the
 * list emptied for the next execution.
 */
static void parameters_of_each_kind(trl_test *test) {
    trl_params *params = NULL;
    TRL_CHECK(test, trl_params_new(&params) == TRL_OK);
    trl_decimal one = {{1U, 0}, 0U};
    trl_datetime instant = {false, 0U, 0, 0};
    trl_interval interval = {{2U, 2U, 2U, 0U}, {1U, 0}};
    trl_decfloat ten = {{10U, 0}, 0};
    uint8_t octets[TRL_UUID_BYTES] = {1};
    TRL_CHECK(test, trl_params_add_null(params) == TRL_OK && trl_params_add_text(params, "Ada", 3U) == TRL_OK);
    TRL_CHECK(test,
              trl_params_add_bytes(params, octets, 2U) == TRL_OK && trl_params_add_decimal(params, one) == TRL_OK);
    TRL_CHECK(test, trl_params_add_double(params, 0.5) == TRL_OK && trl_params_add_boolean(params, true) == TRL_OK);
    TRL_CHECK(test, trl_params_add_datetime(params, TRL_KIND_DATE, instant) == TRL_OK);
    TRL_CHECK(test, trl_params_add_datetime(params, TRL_KIND_BOOLEAN, instant) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test,
              trl_params_add_interval(params, interval) == TRL_OK && trl_params_add_decfloat(params, ten) == TRL_OK);
    TRL_CHECK(test, trl_params_add_uuid(params, octets) == TRL_OK);
    TRL_CHECK(test, trl_params_add_json(params, "{\"a\":[1,\"x\"]}", 13U) == TRL_OK);
    TRL_CHECK(test, trl_params_add_json(params, "{", 1U) == TRL_FAILED);
    TRL_CHECK(test, trl_params_add_xml(params, "<a/>", 4U) == TRL_OK);
    TRL_CHECK(test, trl_params_add_text(params, "\xff", 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_count(params) == 12U);
    const trl_value *values = trl_params_values(params);
    char json[32];
    size_t length = 0U;
    TRL_CHECK(test, trl_value_json_text(&values[10], json, sizeof json, &length) == TRL_OK);
    TRL_CHECK(test, strcmp(json, "{\"a\":[1,\"x\"]}") == 0 && length == 13U);
    TRL_CHECK(test, trl_value_json_text(&values[10], json, 4U, &length) == TRL_OK && strcmp(json, "{\"a") == 0);
    TRL_CHECK(test, trl_value_json_text(&values[1], json, sizeof json, NULL) == TRL_INVALID_ARGUMENT);
    trl_value items[2] = {values[1], values[3]};
    trl_value row;
    memset(&row, 0, sizeof row);
    row.tag = TRL_TAG_ROW;
    row.as.list.items = items;
    row.as.list.count = 2U;
    TRL_CHECK(test, trl_params_add_value(params, &row) == TRL_OK);
    const trl_value *copied = &trl_params_values(params)[12];
    TRL_CHECK(test,
              trl_value_count(copied) == 2U && trl_value_at(copied, 0U)->as.bytes.bytes != items[0].as.bytes.bytes);
    trl_params_clear(params);
    TRL_CHECK(test, trl_params_count(params) == 0U && trl_params_add_null(params) == TRL_OK);
    TRL_CHECK(test, trl_params_add_null(NULL) == TRL_INVALID_ARGUMENT && trl_params_count(NULL) == 0U);
    TRL_CHECK(test, trl_params_new(NULL) == TRL_INVALID_ARGUMENT);
    trl_params_free(params);
    trl_params_free(NULL);
    trl_params_clear(NULL);
}

/* Whether `text` is refused as a JSON parameter, its failure of `sqlcode` and `sqlstate`. */
static bool json_refused(trl_params *params, const char *text, size_t length, int32_t sqlcode, const char *sqlstate) {
    const trl_failure *failure = trl_params_failure(params);
    return trl_params_add_json(params, text, length) == TRL_FAILED && trl_failure_sqlcode(failure) == sqlcode &&
           strcmp(trl_failure_sqlstate(failure), sqlstate) == 0 && trl_failure_message(failure)[0] != '\0';
}

/**
 * Verifies: [PROTOCOL JSON values] a JSON parameter refused as turborel
 * refuses it, the list keeping why: text that is no JSON SQLCODE -301,
 * SQLSTATE 22032; a number no type holds 22003; arrays nested past 128,
 * and a value stored in more than 256 MiB, SQLCODE -900, SQLSTATE 0A000;
 * a value then taken clearing the failure.
 */
static void json_is_refused_as_turborel_refuses_it(trl_test *test) {
    trl_params *params = NULL;
    TRL_CHECK(test, trl_params_new(&params) == TRL_OK && trl_failure_sqlcode(trl_params_failure(params)) == 0);
    TRL_CHECK(test, json_refused(params, "{", 1U, -301, "22032") && json_refused(params, "tru", 3U, -301, "22032"));
    TRL_CHECK(test, json_refused(params, "1e400", 5U, -301, "22003"));
    char deep[130];
    memset(deep, '[', sizeof deep);
    TRL_CHECK(test, json_refused(params, deep, sizeof deep, -900, "0A000"));
    size_t length = (size_t)268435456U + 2U;
    char *large = malloc(length);
    TRL_CHECK(test, large != NULL);
    if (large != NULL) {
        memset(large, 'a', length);
        large[0] = '"';
        large[length - 1U] = '"';
        TRL_CHECK(test, json_refused(params, large, length, -900, "0A000"));
        free(large);
    }
    TRL_CHECK(test, trl_params_add_json(params, "[1]", 3U) == TRL_OK &&
                        trl_failure_sqlcode(trl_params_failure(params)) == 0 && trl_params_count(params) == 1U);
    TRL_CHECK(test, trl_params_failure(NULL) == NULL);
    trl_params_free(params);
}

/**
 * Verifies: [CLIENT Prepared statements] a type's kind, length, precision
 * and scale, as a column's or a parameter's.
 */
static void a_types_sizes(trl_test *test) {
    trl_data_type type;
    memset(&type, 0, sizeof type);
    type.tag = TRL_TAG_EXACT;
    type.as.exact.precision = 9U;
    type.as.exact.scale = 2U;
    TRL_CHECK(test, trl_type_kind(&type) == TRL_KIND_EXACT && trl_type_precision(&type) == 9U);
    TRL_CHECK(test, trl_type_scale(&type) == 2U && trl_type_length(&type) == 0U);
    type.tag = TRL_TAG_VARYING;
    type.as.character.length = 20U;
    TRL_CHECK(test, trl_type_length(&type) == 20U && trl_type_precision(&type) == 0U && trl_type_scale(&type) == 0U);
    type.tag = TRL_TAG_BLOB;
    type.as.lob.length = 1000U;
    TRL_CHECK(test, trl_type_length(&type) == 1000U);
    type.tag = TRL_TAG_BINARY;
    type.as.length = 4U;
    TRL_CHECK(test, trl_type_length(&type) == 4U);
    type.tag = TRL_TAG_TIMESTAMP;
    type.as.precision = 6U;
    TRL_CHECK(test, trl_type_precision(&type) == 6U);
    TRL_CHECK(test, trl_type_kind(NULL) == TRL_KIND_NULL && trl_type_length(NULL) == 0U);
    TRL_CHECK(test, trl_type_precision(NULL) == 0U && trl_type_scale(NULL) == 0U);
}

/**
 * Verifies: [DESIGN] D9 a null pointer passed in, to a value's accessor or
 * a parameter list's, refused with TRL_INVALID_ARGUMENT.
 */
static void null_arguments_are_refused(trl_test *test) {
    trl_value value = of_bytes(TRL_TAG_VARYING, "x");
    trl_decimal decimal;
    memset(&decimal, 0, sizeof decimal);
    trl_datetime datetime;
    memset(&datetime, 0, sizeof datetime);
    trl_interval interval;
    memset(&interval, 0, sizeof interval);
    trl_decfloat decfloat;
    memset(&decfloat, 0, sizeof decfloat);
    TRL_CHECK(test, trl_value_decimal(NULL, &decimal) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_decimal(&value, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_double(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_boolean(&value, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_datetime(NULL, &datetime) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_datetime(&value, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_interval(&value, &interval) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_decfloat(&value, &decfloat) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_json_text(NULL, NULL, 0U, NULL) == TRL_INVALID_ARGUMENT);
    uint8_t octets[TRL_UUID_BYTES] = {0};
    TRL_CHECK(test, trl_params_add_text(NULL, "a", 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_bytes(NULL, octets, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_decimal(NULL, decimal) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_double(NULL, 1.0) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_boolean(NULL, true) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_datetime(NULL, TRL_KIND_DATE, datetime) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_interval(NULL, interval) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_decfloat(NULL, decfloat) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_uuid(NULL, octets) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_json(NULL, "1", 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_xml(NULL, "a", 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_value(NULL, &value) == TRL_INVALID_ARGUMENT);
    trl_params *params = NULL;
    TRL_CHECK(test, trl_params_new(&params) == TRL_OK);
    TRL_CHECK(test, trl_params_add_value(params, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_text(params, NULL, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_bytes(params, NULL, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_bytes(params, NULL, 0U) == TRL_OK);
    TRL_CHECK(test, trl_params_add_uuid(params, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_json(params, NULL, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_params_add_xml(params, NULL, 1U) == TRL_INVALID_ARGUMENT);
    trl_params_free(params);
}

/**
 * Verifies: [CLIENT Running statements] a large object's bytes and text by
 * its kind; a JSON value's bytes not of the tree refused; each type's
 * length and precision.
 */
static void large_objects_and_types(trl_test *test) {
    trl_value clob;
    memset(&clob, 0, sizeof clob);
    clob.tag = TRL_TAG_LOB_WHOLE;
    clob.as.lob.kind = TRL_LOB_CHARACTER;
    clob.as.lob.octets.bytes = (const uint8_t *)"text";
    clob.as.lob.octets.length = 4U;
    size_t length = 0U;
    TRL_CHECK(test, trl_value_text(&clob, &length) != NULL && length == 4U && trl_value_bytes(&clob, NULL) == NULL);
    clob.as.lob.kind = TRL_LOB_BINARY;
    TRL_CHECK(test, trl_value_bytes(&clob, &length) != NULL && trl_value_text(&clob, NULL) == NULL);
    trl_value json = of_bytes(TRL_TAG_JSON, "\x7f\x7f");
    char text[8];
    TRL_CHECK(test, trl_value_json_text(&json, text, sizeof text, NULL) == TRL_INVALID_TEXT);
    trl_data_type type;
    memset(&type, 0, sizeof type);
    static const uint8_t precise[] = {
        TRL_TAG_APPROXIMATE, TRL_TAG_DATE, TRL_TAG_TIME, TRL_TAG_TIME_ZONED, TRL_TAG_TIMESTAMP_ZONED, TRL_TAG_DECFLOAT,
    };
    for (size_t i = 0U; i < sizeof precise; ++i) {
        type.tag = precise[i];
        type.as.precision = 3U;
        TRL_CHECK(test, trl_type_precision(&type) == 3U && trl_type_length(&type) == 0U);
    }
    static const uint8_t long_ones[] = {TRL_TAG_CHARACTER, TRL_TAG_BINARY_VARYING, TRL_TAG_CLOB};
    type.tag = long_ones[0];
    type.as.character.length = 5U;
    TRL_CHECK(test, trl_type_length(&type) == 5U);
    type.tag = long_ones[1];
    type.as.length = 6U;
    TRL_CHECK(test, trl_type_length(&type) == 6U);
    type.tag = long_ones[2];
    type.as.lob.length = 7U;
    TRL_CHECK(test, trl_type_length(&type) == 7U);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(each_value_is_read_by_its_kind),
        TRL_TEST_CASE(other_values_are_read_by_their_kind),
        TRL_TEST_CASE(values_of_values),
        TRL_TEST_CASE(a_uuid_is_octets_and_text),
        TRL_TEST_CASE(parameters_of_each_kind),
        TRL_TEST_CASE(json_is_refused_as_turborel_refuses_it),
        TRL_TEST_CASE(a_types_sizes),
        TRL_TEST_CASE(null_arguments_are_refused),
        TRL_TEST_CASE(large_objects_and_types),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
