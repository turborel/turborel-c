/**
 * @file assign_test.c
 * @brief A source's value as an import sends it for its column: a string
 * for a character string column assigned to it as INSERT assigns it,
 * padded, its spaces past the length cut, or refused if too long; any
 * other value as it is.
 */
#include "bulk/assign.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/values.h"
#include "data/params.h"
#include "failure.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A character string value of `text`. */
static trl_value text_of(const char *text) {
    trl_value value;
    memset(&value, 0, sizeof value);
    value.tag = TRL_TAG_VARYING;
    value.as.bytes.bytes = (const uint8_t *)text;
    value.as.bytes.length = strlen(text);
    return value;
}

/* A character string type of `tag`, `length` and `units`. */
static trl_data_type type_of(uint8_t tag, uint64_t length, uint8_t units) {
    trl_data_type type;
    memset(&type, 0, sizeof type);
    type.tag = tag;
    if (tag == TRL_TAG_CLOB || tag == TRL_TAG_BLOB) {
        type.as.lob.length = length;
        type.as.lob.units = units;
    } else {
        type.as.character.length = (uint32_t)length;
        type.as.character.units = units;
    }
    return type;
}

/* Whether `text` assigned to `type` is sent as `sent`, of tag `tag`. */
static bool sent_as(const char *text, trl_data_type type, uint8_t tag, const char *sent) {
    trl_params *out = NULL;
    (void)trl_params_new(&out);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_value value = text_of(text);
    bool same = trl_import_value(NULL, &value, &type, out, &failure) == TRL_OK && trl_params_count(out) == 1U;
    const trl_value *got = trl_params_values(out);
    if (same) {
        trl_slice bytes = got->tag == TRL_TAG_LOB_WHOLE ? got->as.lob.octets : got->as.bytes;
        same = got->tag == tag && bytes.length == strlen(sent) && memcmp(bytes.bytes, sent, bytes.length) == 0;
    }
    trl_failure_clear(&failure);
    trl_params_free(out);
    return same;
}

/* Whether `text` assigned to `type` is refused, SQLSTATE 22001, saying
   `message`, its data type `name`. */
static bool refused_as(const char *text, trl_data_type type, const char *message, const char *name) {
    trl_params *out = NULL;
    (void)trl_params_new(&out);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_value value = text_of(text);
    bool same = trl_import_value(NULL, &value, &type, out, &failure) == TRL_FAILED && trl_params_count(out) == 0U &&
                strcmp(trl_failure_sqlstate(&failure), "22001") == 0 &&
                strcmp(trl_failure_message(&failure), message) == 0 &&
                strcmp(trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_DATA_TYPE), name) == 0;
    if (!same) {
        (void)fprintf(stderr, "%s: %s\n", text, trl_failure_message(&failure));
    }
    trl_failure_clear(&failure);
    trl_params_free(out);
    return same;
}

/**
 * Verifies: [CLIENT Importing] a string for CHARACTER VARYING is sent as
 * it is, its spaces past the length cut; one longer, not of spaces beyond
 * the length, is refused [ISO/IEC 9075:1992 §13.8 GR4b iii, §9.2 GR3e ii];
 * a length in octets counts octets.
 */
static void varying_strings(trl_test *test) {
    trl_data_type five = type_of(TRL_TAG_VARYING, 5U, TRL_UNITS_CHARACTERS);
    TRL_CHECK(test, sent_as("abc", five, TRL_TAG_CHARACTER, "abc"));
    TRL_CHECK(test, sent_as("abc    ", five, TRL_TAG_CHARACTER, "abc  "));
    TRL_CHECK(test, sent_as("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", five, TRL_TAG_CHARACTER,
                            "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"));
    TRL_CHECK(test, refused_as("abcdefg", five,
                               "a value of 7 characters, not spaces beyond 5, does not fit CHARACTER VARYING(5)",
                               "CHARACTER VARYING(5)"));
    trl_data_type octets = type_of(TRL_TAG_VARYING, 4U, TRL_UNITS_OCTETS);
    TRL_CHECK(test, sent_as("\xc3\xa9\xc3\xa9", octets, TRL_TAG_CHARACTER, "\xc3\xa9\xc3\xa9"));
    TRL_CHECK(test, refused_as("\xc3\xa9\xc3\xa9\xc3\xa9", octets,
                               "a value of 6 octets, not spaces beyond 4, does not fit CHARACTER VARYING(4 OCTETS)",
                               "CHARACTER VARYING(4 OCTETS)"));
}

/**
 * Verifies: [CLIENT Importing] a string for CHARACTER is padded with
 * spaces to its length; one longer, not of spaces beyond it, is refused
 * [ISO/IEC 9075:1992 §9.2 GR3b ii].
 */
static void fixed_strings(trl_test *test) {
    trl_data_type five = type_of(TRL_TAG_CHARACTER, 5U, TRL_UNITS_CHARACTERS);
    TRL_CHECK(test, sent_as("ab", five, TRL_TAG_CHARACTER, "ab   "));
    TRL_CHECK(test, sent_as("", five, TRL_TAG_CHARACTER, "     "));
    TRL_CHECK(test, sent_as("abcde  ", five, TRL_TAG_CHARACTER, "abcde"));
    TRL_CHECK(test,
              refused_as("abcdef", five, "a value of 6 characters, not spaces beyond 5, does not fit CHARACTER(5)",
                         "CHARACTER(5)"));
}

/**
 * Verifies: [CLIENT Importing] a string for a character large object of
 * at most what a server sends whole is sent whole; one longer than its
 * column refused [ISO/IEC 9075-2:2023 §9.2 GR3e ii]; a value of any other
 * kind, or for any other type, sent as it is.
 */
static void objects_and_other_values(trl_test *test) {
    trl_data_type clob = type_of(TRL_TAG_CLOB, 10U, TRL_UNITS_CHARACTERS);
    TRL_CHECK(test, sent_as("short", clob, TRL_TAG_LOB_WHOLE, "short"));
    TRL_CHECK(test, sent_as("ten chars!   ", clob, TRL_TAG_LOB_WHOLE, "ten chars!"));
    TRL_CHECK(test, refused_as("eleven chars", clob,
                               "a large object longer than CHARACTER LARGE OBJECT(10), not padding beyond it, does "
                               "not fit it",
                               "CHARACTER LARGE OBJECT(10)"));
    trl_data_type octets = type_of(TRL_TAG_CLOB, 2U, TRL_UNITS_OCTETS);
    TRL_CHECK(test, refused_as("\xc3\xa9\xc3\xa9", octets,
                               "a large object longer than CHARACTER LARGE OBJECT(2 OCTETS), not padding beyond it, "
                               "does not fit it",
                               "CHARACTER LARGE OBJECT(2 OCTETS)"));
    trl_data_type exact = type_of(TRL_TAG_EXACT, 0U, 0U);
    TRL_CHECK(test, sent_as("12", exact, TRL_TAG_VARYING, "12"));
    trl_data_type blob = type_of(TRL_TAG_BLOB, 10U, 0U);
    TRL_CHECK(test, sent_as("ab", blob, TRL_TAG_VARYING, "ab"));
    trl_params *out = NULL;
    (void)trl_params_new(&out);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_value truth;
    memset(&truth, 0, sizeof truth);
    truth.tag = TRL_TAG_BOOLEAN;
    TRL_CHECK(test, trl_import_value(NULL, &truth, &clob, out, &failure) == TRL_OK && trl_params_count(out) == 1U);
    trl_failure_clear(&failure);
    trl_params_free(out);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(varying_strings),
        TRL_TEST_CASE(fixed_strings),
        TRL_TEST_CASE(objects_and_other_values),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
