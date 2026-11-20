/**
 * @file version_test.c
 * @brief The library's version, through the public header alone.
 */
#include "harness.h"
#include "turborel/turborel.h"

#include <stdio.h>
#include <string.h>

/** Room for a version written from its parts, as "MAJOR.MINOR.PATCH". */
#define VERSION_TEXT_BYTES 32U

/**
 * Verifies: [DESIGN] D5 the library linked gives the version the header
 * declares, the turborel release it is made for, 0.0.0 in the alpha.
 */
static void library_and_header_agree(trl_test *test) {
    TRL_CHECK(test, strcmp(trl_version(), TRL_VERSION_STRING) == 0);
    TRL_CHECK(test, strcmp(trl_version(), "0.0.0") == 0);
}

/**
 * Verifies: [DESIGN] D5 the version's text is its three parts, joined by
 * full stops.
 */
static void text_is_its_parts(trl_test *test) {
    char text[VERSION_TEXT_BYTES];
    int written = snprintf(text, sizeof text, "%d.%d.%d", TRL_VERSION_MAJOR, TRL_VERSION_MINOR, TRL_VERSION_PATCH);
    TRL_CHECK(test, written > 0 && (size_t)written < sizeof text);
    TRL_CHECK(test, strcmp(text, TRL_VERSION_STRING) == 0);
}

/**
 * Verifies: [PROTOCOL The Native Protocol] the library speaks version 1,
 * the only version.
 */
static void speaks_protocol_version_1(trl_test *test) {
    /* Held in a variable, so no compiler warns of a constant condition. */
    const int spoken = TRL_PROTOCOL_VERSION;
    TRL_CHECK(test, spoken == 1);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(library_and_header_agree),
        TRL_TEST_CASE(text_is_its_parts),
        TRL_TEST_CASE(speaks_protocol_version_1),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
