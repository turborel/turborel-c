/**
 * @file names_test.c
 * @brief SQL names as an import writes them: a table's name lexed and
 * written as delimited identifiers, and a heading's column.
 */
#include "bulk/names.h"
#include "codec/buffer.h"
#include "harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Room for a long identifier's text. */
#define LONG_NAME 140U

/* Whether the table name `text` is written as `expected`, or, if
   `expected` is null, refused. */
static bool table_is(const char *text, const char *expected) {
    trl_buffer out;
    trl_buffer_init(&out);
    bool named = trl_table_sql(text, &out);
    trl_put_u8(&out, 0U);
    bool same = expected == NULL ? !named : named && strcmp((const char *)out.data, expected) == 0;
    if (!same) {
        (void)fprintf(stderr, "%s: %s\n", text, named ? (const char *)out.data : "refused");
    }
    trl_buffer_free(&out);
    return same;
}

/* Whether the heading `heading` names the column `expected`. */
static bool heading_is(const char *heading, const char *expected) {
    trl_buffer out;
    trl_buffer_init(&out);
    trl_column_of_heading(heading, &out);
    trl_put_u8(&out, 0U);
    bool same = strcmp((const char *)out.data, expected) == 0;
    if (!same) {
        (void)fprintf(stderr, "%s: %s\n", heading, (const char *)out.data);
    }
    trl_buffer_free(&out);
    return same;
}

/**
 * Verifies: [CLIENT Importing] a table's name of one to three parts, each
 * a regular identifier in upper case or a delimited one as written, is
 * written as delimited identifiers [ISO/IEC 9075:1992 §5.4]; separators
 * and comments between its tokens passed over.
 */
static void table_names_are_delimited(trl_test *test) {
    TRL_CHECK(test, table_is("people", "\"PEOPLE\""));
    TRL_CHECK(test, table_is("ann.people", "\"ANN\".\"PEOPLE\""));
    TRL_CHECK(test, table_is("cat . ann . people", "\"CAT\".\"ANN\".\"PEOPLE\""));
    TRL_CHECK(test, table_is("\"Mixed \"\"q\"\"\"", "\"Mixed \"\"q\"\"\""));
    TRL_CHECK(test, table_is("  x_1 -- the table\n", "\"X_1\""));
    TRL_CHECK(test, table_is("\r\nx /* the table */", "\"X\""));
}

/**
 * Verifies: [CLIENT Importing] a text that is no table name is refused: a
 * key word, four parts, an empty or open delimited identifier, an
 * identifier too long, a token after the name, a comment not closed.
 */
static void what_is_no_table_name_is_refused(trl_test *test) {
    TRL_CHECK(test, table_is("select", NULL));
    TRL_CHECK(test, table_is("a.b.c.d", NULL));
    TRL_CHECK(test, table_is("\"\"", NULL));
    TRL_CHECK(test, table_is("\"open", NULL));
    TRL_CHECK(test, table_is("\"line\nbreak\"", NULL));
    TRL_CHECK(test, table_is("people x", NULL));
    TRL_CHECK(test, table_is("people.", NULL));
    TRL_CHECK(test, table_is(".people", NULL));
    TRL_CHECK(test, table_is("_x", NULL));
    TRL_CHECK(test, table_is("a\xc3\x97"
                             "b",
                             NULL));
    TRL_CHECK(test, table_is("x -- open", NULL));
    TRL_CHECK(test, table_is("x /* open", NULL));
    TRL_CHECK(test, table_is("", NULL));
    char name[LONG_NAME];
    memset(name, 'a', 129U);
    name[129] = '\0';
    TRL_CHECK(test, table_is(name, NULL));
    name[128] = '\0';
    char upper[LONG_NAME + 2U];
    upper[0] = '"';
    memset(upper + 1, 'A', 128U);
    upper[129] = '"';
    upper[130] = '\0';
    TRL_CHECK(test, table_is(name, upper));
}

/**
 * Verifies: [CLIENT Importing] letters past ASCII taken as Turborel takes
 * them, in upper case as far as RULES.md D8 says: Latin-1, Latin
 * Extended-A, Greek, final sigma, Cyrillic; a letter of four octets kept.
 */
static void letters_past_ascii(trl_test *test) {
    TRL_CHECK(test, table_is("caf\xc3\xa9", "\"CAF\xc3\x89\""));
    TRL_CHECK(test, table_is("\xc3\xbf", "\"\xc5\xb8\""));
    TRL_CHECK(test, table_is("\xc4\x81", "\"\xc4\x80\""));
    TRL_CHECK(test, table_is("\xce\xb1\xcf\x82", "\"\xce\x91\xce\xa3\""));
    TRL_CHECK(test, table_is("\xd0\xb6\xd1\x90", "\"\xd0\x96\xd0\x80\""));
    TRL_CHECK(test, table_is("\xf0\x9f\x98\x80", "\"\xf0\x9f\x98\x80\""));
    TRL_CHECK(test, table_is("\xe4\xb8\xad", "\"\xe4\xb8\xad\""));
    TRL_CHECK(test, table_is("\xe2\x80\x94", NULL));
}

/**
 * Verifies: [CLIENT Importing] a heading that is one identifier names it,
 * a regular one in upper case, a key word as it would be but for being
 * reserved; any other heading names its text as written.
 */
static void headings_name_columns(trl_test *test) {
    TRL_CHECK(test, heading_is("id", "ID"));
    TRL_CHECK(test, heading_is("select", "SELECT"));
    TRL_CHECK(test, heading_is("\"Full Name\"", "Full Name"));
    TRL_CHECK(test, heading_is("full name", "full name"));
    TRL_CHECK(test, heading_is("", ""));
    trl_buffer out;
    trl_buffer_init(&out);
    trl_put_delimited(&out, "a\"b", 3U);
    TRL_CHECK(test, out.length == 6U && memcmp(out.data, "\"a\"\"b\"", 6U) == 0);
    trl_buffer_free(&out);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(table_names_are_delimited),
        TRL_TEST_CASE(what_is_no_table_name_is_refused),
        TRL_TEST_CASE(letters_past_ascii),
        TRL_TEST_CASE(headings_name_columns),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
