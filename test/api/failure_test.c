/**
 * @file failure_test.c
 * @brief Failures (RULES.md D9): what the server sent kept whole, the
 * driver's own of the rule DESIGN and their severity, the display form
 * turborel-client writes, and which end a connection
 * [PROTOCOL Errors and notices].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "failure.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEXT_BYTES 256U
#define POSITION 12U
#define SHORT_TEXT 10U

/* A view of a null-ended string. */
static trl_slice text(const char *string) {
    trl_slice slice = {(const uint8_t *)string, strlen(string)};
    return slice;
}

/**
 * Verifies: [PROTOCOL Errors and notices] a failure the server sent is
 * kept whole: its SQLCODE, SQLSTATE, message, rule, position, whether a
 * transaction is active, and each diagnostic told; and is displayed as
 * turborel-client displays it.
 */
static void a_servers_failure_is_kept_whole(trl_test *test) {
    trl_wire_failure wire;
    memset(&wire, 0, sizeof wire);
    wire.sqlcode = -803;
    wire.sqlstate = text("23505");
    wire.message = text("a duplicate key");
    wire.cited = true;
    wire.rule = text("ISO/IEC 9075-2:2023 \xc2\xa7"
                     "11.7");
    wire.placed = true;
    wire.position = POSITION;
    wire.active = true;
    wire.diagnostics.told[TRL_DIAGNOSTIC_DETAIL] = true;
    wire.diagnostics.text[TRL_DIAGNOSTIC_DETAIL] = text("ID 1");
    wire.diagnostics.told[TRL_DIAGNOSTIC_CONSTRAINT] = true;
    wire.diagnostics.text[TRL_DIAGNOSTIC_CONSTRAINT] = text("");
    trl_failure failure;
    trl_failure_init(&failure);
    trl_failure_from_wire(&failure, &wire);
    TRL_CHECK(test, trl_failure_sqlcode(&failure) == -803 && strcmp(trl_failure_sqlstate(&failure), "23505") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(&failure), "a duplicate key") == 0);
    uint64_t position = 0U;
    TRL_CHECK(test, trl_failure_position(&failure, &position) && position == POSITION);
    TRL_CHECK(test, trl_failure_position(&failure, NULL) && trl_failure_active(&failure));
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_DETAIL), "ID 1") == 0);
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_CONSTRAINT), "") == 0);
    TRL_CHECK(test, trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_HINT) == NULL);
    /* One past the last, as a program may pass by mistake. */
    trl_diagnostic past = (trl_diagnostic)TRL_DIAGNOSTICS; // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    TRL_CHECK(test, trl_failure_diagnostic(&failure, past) == NULL);
    char line[TEXT_BYTES];
    size_t written = trl_failure_text(&failure, line, sizeof line);
    static const char expected[] = "SQLCODE -803 (SQLSTATE 23505): a duplicate key [ISO/IEC 9075-2:2023 \xc2\xa7"
                                   "11.7] (at character 12)";
    TRL_CHECK(test, strcmp(line, expected) == 0 && written == sizeof expected - 1U);
    char short_line[SHORT_TEXT];
    TRL_CHECK(test, trl_failure_text(&failure, short_line, sizeof short_line) == written);
    TRL_CHECK(test, strcmp(short_line, "SQLCODE -") == 0 && trl_failure_text(&failure, NULL, 0U) == written);
    TRL_CHECK(test, !trl_failure_ends_connection(&failure));
    trl_failure copy;
    trl_failure_init(&copy);
    trl_failure_copy(&copy, &failure);
    trl_failure_copy(&copy, &copy);
    TRL_CHECK(test, strcmp(trl_failure_rule(&copy), trl_failure_rule(&failure)) == 0 && trl_failure_active(&copy));
    trl_failure_clear(&copy);
    trl_failure_clear(&failure);
}

/**
 * Verifies: [DESIGN] D9 the driver's own failures carry turborel-client's
 * SQLCODE and SQLSTATE, the rule DESIGN, and the severity FATAL if they
 * end the connection, else ERROR; a codec's failure of memory is the
 * driver's (SQLCODE -918, SQLSTATE TB918).
 */
static void the_drivers_failures_cite_design(trl_test *test) {
    trl_failure failure;
    trl_failure_init(&failure);
    TRL_CHECK(test, trl_failure_sqlcode(&failure) == 0 && strcmp(trl_failure_message(&failure), "") == 0);
    trl_failure_broken(&failure, "the connection closed");
    TRL_CHECK(test, trl_failure_sqlcode(&failure) == -919 && strcmp(trl_failure_sqlstate(&failure), "08006") == 0);
    TRL_CHECK(test, strcmp(trl_failure_rule(&failure), "DESIGN") == 0 && trl_failure_ends_connection(&failure));
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_SEVERITY), "FATAL") == 0);
    TRL_CHECK(test, !trl_failure_position(&failure, NULL) && !trl_failure_active(&failure));
    trl_failure_set(&failure, -910, "TB910", "misused");
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_SEVERITY), "ERROR") == 0);
    TRL_CHECK(test, !trl_failure_ends_connection(&failure));
    trl_failure_set(&failure, -919, "08003", "no connection");
    TRL_CHECK(test, !trl_failure_ends_connection(&failure));
    trl_failure_set(&failure, -919, "28000", "refused");
    TRL_CHECK(test, trl_failure_ends_connection(&failure));
    trl_failure_set(&failure, -551, "28000", "not granted");
    TRL_CHECK(test, !trl_failure_ends_connection(&failure));
    trl_failure_no_memory(&failure);
    TRL_CHECK(test, trl_failure_sqlcode(&failure) == -918 && strcmp(trl_failure_sqlstate(&failure), "TB918") == 0);
    trl_codec_error error = {0, "", ""};
    trl_codec_no_memory(&error);
    trl_failure_from_codec(&failure, &error);
    TRL_CHECK(test, trl_failure_sqlcode(&failure) == -918);
    trl_codec_error malformed = {0, "", ""};
    trl_codec_malformed(&malformed, "reply", 3U, "too short");
    trl_failure_from_codec(&failure, &malformed);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(&failure), "08006") == 0);
    char line[TEXT_BYTES];
    (void)trl_failure_text(&failure, line, sizeof line);
    TRL_CHECK(test, strstr(line, "[DESIGN]") != NULL && strstr(line, "(at character") == NULL);
    trl_wire_failure bare;
    memset(&bare, 0, sizeof bare);
    bare.sqlcode = -1;
    bare.message = text("plain");
    trl_failure_from_wire(&failure, &bare);
    (void)trl_failure_text(&failure, line, sizeof line);
    TRL_CHECK(test, strcmp(line, "SQLCODE -1 (SQLSTATE ): plain") == 0);
    trl_failure_clear(&failure);
}

/**
 * Verifies: [DESIGN] D9 a failure the server sent without a message, or a
 * rule cited empty, is kept as empty text; a null failure gives nothing.
 */
static void empty_and_null_failures(trl_test *test) {
    trl_wire_failure wire;
    memset(&wire, 0, sizeof wire);
    wire.sqlcode = -1;
    wire.cited = true;
    wire.diagnostics.told[0] = true;
    trl_failure failure;
    trl_failure_init(&failure);
    trl_failure_from_wire(&failure, &wire);
    TRL_CHECK(test, strcmp(trl_failure_message(&failure), "") == 0 && strcmp(trl_failure_rule(&failure), "") == 0);
    TRL_CHECK(test, strcmp(trl_failure_diagnostic(&failure, TRL_DIAGNOSTIC_SEVERITY), "") == 0);
    trl_failure_clear(&failure);
    TRL_CHECK(test, trl_failure_sqlcode(NULL) == 0 && strcmp(trl_failure_sqlstate(NULL), "") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(NULL), "") == 0 && trl_failure_rule(NULL) == NULL);
    TRL_CHECK(test, !trl_failure_position(NULL, NULL) && !trl_failure_active(NULL));
    TRL_CHECK(test, trl_failure_diagnostic(NULL, TRL_DIAGNOSTIC_DETAIL) == NULL);
    TRL_CHECK(test, !trl_failure_ends_connection(NULL) && trl_failure_text(NULL, NULL, 0U) == 0U);
    char line[SHORT_TEXT];
    TRL_CHECK(test, trl_failure_text(&failure, NULL, sizeof line) == 0U);
}

/**
 * Verifies: [DESIGN] D9 a program's own failure told into one lent it:
 * its SQLCODE, SQLSTATE and message kept, citing no rule; a null
 * pointer, an SQLCODE not below 0, an SQLSTATE of another form or a
 * message not UTF-8 refused, the failure as it was.
 */
static void a_programs_failure_is_told(trl_test *test) {
    trl_failure lent;
    trl_failure_init(&lent);
    TRL_CHECK(test, trl_failure_tell(&lent, -919, "08006", "closed") == TRL_OK);
    TRL_CHECK(test, trl_failure_sqlcode(&lent) == -919 && strcmp(trl_failure_sqlstate(&lent), "08006") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(&lent), "closed") == 0 && trl_failure_rule(&lent) == NULL);
    TRL_CHECK(test, trl_failure_tell(&lent, -30082, "28000", "refused") == TRL_OK &&
                        strcmp(trl_failure_message(&lent), "refused") == 0);
    static const char *const states[] = {"0800", "080066", "08o06", NULL};
    bool every = true;
    for (size_t i = 0U; i < sizeof states / sizeof states[0]; ++i) {
        every = every && trl_failure_tell(&lent, -1, states[i], "x") == TRL_INVALID_ARGUMENT;
    }
    TRL_CHECK(test, every && trl_failure_tell(&lent, 0, "08006", "x") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_failure_tell(&lent, -1, "08006", "\xff") == TRL_INVALID_ARGUMENT &&
                        trl_failure_tell(&lent, -1, "08006", NULL) == TRL_INVALID_ARGUMENT &&
                        trl_failure_tell(NULL, -1, "08006", "x") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_failure_sqlcode(&lent) == -30082);
    trl_failure_clear(&lent);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_servers_failure_is_kept_whole),
        TRL_TEST_CASE(the_drivers_failures_cite_design),
        TRL_TEST_CASE(empty_and_null_failures),
        TRL_TEST_CASE(a_programs_failure_is_told),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
