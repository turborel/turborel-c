/**
 * @file json_edges_test.c
 * @brief JSON text at its edges, read as turborel reads it
 * [PROTOCOL JSON values]: every kind of white space, numbers missing
 * digits or past their range, escapes of surrogates whole and broken,
 * literals cut short.
 */
#include "codec/json.h"
#include "fixture.h"
#include "harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Whether `text` reads as JSON. */
static bool reads(fixture *f, const char *text) {
    fixture_reset(f);
    return trl_json_from_text(&f->buffer, (const uint8_t *)text, strlen(text), &f->arena, &f->error);
}

/**
 * Verifies: [PROTOCOL JSON values] space, tab, line feed and carriage
 * return are white space; escapes of a surrogate pair, upper or lower
 * case, read as one character.
 */
static void what_reads(trl_test *test) {
    fixture f;
    fixture_init(&f);
    static const char *const texts[] = {
        " \t\n\r[1,\t2]\r\n",       "\"\\uD83D\\uDE00\"", "\"\\ud83d\\ude00\"", "\"\\u00e9\\u00C9\"", "-0.5e-3",
        "{\"k\":true,\"k\":false}",
    };
    for (size_t i = 0U; i < sizeof texts / sizeof texts[0]; ++i) {
        TRL_CHECK(test, reads(&f, texts[i]));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL JSON values] a number missing a digit or signed by
 * a plus, an escape of a lone or broken surrogate or of too few digits, a
 * literal cut short, a number too great for a double, or nothing at
 * all, is no JSON text.
 */
static void what_does_not_read(trl_test *test) {
    fixture f;
    fixture_init(&f);
    static const char *const texts[] = {
        "1e",
        "-",
        "+1",
        "1.",
        "\"\\udc00\"",
        "\"\\ud800\"",
        "\"\\ud800x\"",
        "\"\\ud800\\n\"",
        "\"\\ud800\\u0041\"",
        "\"\\ud800\\ud800\"",
        "\"\\u12\"",
        "\"\\u12g4\"",
        "nul",
        "tru",
        "fals",
        "",
        "1e400",
        "[1,]",
        "{\"a\"}",
        "1 2",
    };
    for (size_t i = 0U; i < sizeof texts / sizeof texts[0]; ++i) {
        TRL_CHECK(test, !reads(&f, texts[i]));
    }
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(what_reads),
        TRL_TEST_CASE(what_does_not_read),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
