/**
 * @file decimal_test.c
 * @brief Exact numbers, `trl_decimal`, and their text: the full range of an
 * exact number's units and scale, with nothing lost [PROTOCOL Values].
 */
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Whether `number` is written as `expected`. */
static bool written_as(trl_decimal number, const char *expected) {
    char text[TRL_DECIMAL_TEXT_BYTES];
    return trl_decimal_text(&number, text, sizeof text) == TRL_OK && strcmp(text, expected) == 0;
}

/* Whether `text` reads as `units` and `scale` and writes back as `back`. */
static bool reads_as(const char *text, trl_int128 units, uint32_t scale, const char *back) {
    trl_decimal number = {{0U, 0}, 0U};
    return trl_decimal_parse(text, &number) == TRL_OK && number.units.low == units.low &&
           number.units.high == units.high && number.scale == scale && written_as(number, back);
}

/**
 * Verifies: [PROTOCOL Values] an exact number is units divided by ten to
 * the scale, written as turborel writes it: digits, a minus sign, a
 * decimal point before the scale's digits, zeros before them.
 */
static void numbers_are_written_as_turborel_writes_them(trl_test *test) {
    TRL_CHECK(test, written_as((trl_decimal){{1U, 0}, 0U}, "1"));
    TRL_CHECK(test, written_as((trl_decimal){{UINT64_MAX - 49U, -1}, 3U}, "-0.050"));
    TRL_CHECK(test, written_as((trl_decimal){{1250U, 0}, 2U}, "12.50"));
    TRL_CHECK(test, written_as((trl_decimal){{0U, 0}, 18U}, "0.000000000000000000"));
    TRL_CHECK(test, written_as((trl_decimal){{5U, 0}, 40U}, "5E-40"));
    TRL_CHECK(test, written_as((trl_decimal){{5U, 0}, UINT32_MAX}, "5E-4294967295"));
}

/**
 * Verifies: [PROTOCOL Values] the full range of an i128's units, its least
 * and greatest, and of a u32's scale, reads and writes back unchanged.
 */
static void the_full_range_round_trips(trl_test *test) {
    TRL_CHECK(test, reads_as("170141183460469231731687303715884105727", (trl_int128){UINT64_MAX, INT64_MAX}, 0U,
                             "170141183460469231731687303715884105727"));
    TRL_CHECK(test, reads_as("-170141183460469231731687303715884105728", (trl_int128){0U, INT64_MIN}, 0U,
                             "-170141183460469231731687303715884105728"));
    TRL_CHECK(test, reads_as("-1.70141183460469231731687303715884105728E-4294967257", (trl_int128){0U, INT64_MIN},
                             UINT32_MAX, "-170141183460469231731687303715884105728E-4294967295"));
    TRL_CHECK(test, reads_as("-12.50", (trl_int128){UINT64_MAX - 1249U, -1}, 2U, "-12.50"));
    TRL_CHECK(test, reads_as("12E2", (trl_int128){1200U, 0}, 0U, "1200"));
    TRL_CHECK(test, reads_as("+0.5e1", (trl_int128){5U, 0}, 0U, "5"));
    TRL_CHECK(test, reads_as("0E99999", (trl_int128){0U, 0}, 0U, "0"));
}

/**
 * Verifies: [PROTOCOL Values] units past an i128, or a scale past a u32,
 * are out of range; text of no number is refused; a null pointer or a
 * buffer too short, an argument wrongly given.
 */
static void bad_text_and_arguments_fail(trl_test *test) {
    trl_decimal number = {{0U, 0}, 0U};
    TRL_CHECK(test, trl_decimal_parse("170141183460469231731687303715884105728", &number) == TRL_OUT_OF_RANGE);
    TRL_CHECK(test, trl_decimal_parse("1E-4294967296", &number) == TRL_OUT_OF_RANGE);
    TRL_CHECK(test, trl_decimal_parse("1E39", &number) == TRL_OUT_OF_RANGE);
    TRL_CHECK(test, trl_decimal_parse("340282366920938463463374607431768211456", &number) == TRL_OUT_OF_RANGE);
    TRL_CHECK(test, trl_decimal_parse("1E99999999999", &number) == TRL_OUT_OF_RANGE);
    const char *texts[] = {"", "-", ".", "1.2.3", "1e", "1e+", "12a", " 1", "1 "};
    for (size_t i = 0U; i < TRL_TEST_COUNT(texts); ++i) {
        TRL_CHECK(test, trl_decimal_parse(texts[i], &number) == TRL_INVALID_TEXT);
    }
    TRL_CHECK(test, trl_decimal_parse(NULL, &number) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_decimal_parse("1", NULL) == TRL_INVALID_ARGUMENT);
    char text[4];
    trl_decimal long_number = {{1250U, 0}, 2U};
    TRL_CHECK(test, trl_decimal_text(&long_number, text, sizeof text) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_decimal_text(NULL, text, sizeof text) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_decimal_text(&long_number, NULL, 0U) == TRL_INVALID_ARGUMENT);
    trl_decimal short_number = {{125U, 0}, 0U};
    TRL_CHECK(test, trl_decimal_text(&short_number, text, sizeof text) == TRL_OK && strcmp(text, "125") == 0);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(numbers_are_written_as_turborel_writes_them),
        TRL_TEST_CASE(the_full_range_round_trips),
        TRL_TEST_CASE(bad_text_and_arguments_fail),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
