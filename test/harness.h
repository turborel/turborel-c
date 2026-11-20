/**
 * @file harness.h
 * @brief The one harness every test executable is written on (RULES.md
 * D14, R-TEST-7).
 *
 * A test executable lists its cases in an array and hands it to
 * trl_test_main(). Each case is a function that checks with TRL_CHECK();
 * a check that fails is reported with its file and line, and the case
 * goes on, so one run shows every failure. CTest runs each executable,
 * and a case alone is run by naming it as the executable's argument.
 */
#ifndef TRL_TEST_HARNESS_H
#define TRL_TEST_HARNESS_H

#include <stddef.h>

/** @brief The case running, and the checks of it that failed. */
typedef struct trl_test {
    /** @brief The case's name, as its array entry gives it. */
    const char *name;
    /** @brief How many of its checks failed. */
    unsigned int failures;
} trl_test;

/** @brief A test case: a function that checks one behaviour. */
typedef void (*trl_test_run)(trl_test *test);

/** @brief A test case and its name. */
typedef struct trl_test_case {
    /** @brief The case's name, its function's. */
    const char *name;
    /** @brief The case. */
    trl_test_run run;
} trl_test_case;

/**
 * @brief Records a check of the running case; prints it if it failed.
 *
 * @param test The case running.
 * @param passed Nonzero if the check held.
 * @param expression The check's source text.
 * @param file The source file of the check.
 * @param line Its line.
 */
void trl_test_check(trl_test *test, int passed, const char *expression, const char *file, int line);

/**
 * @brief Runs the cases, or the one named by the only argument.
 *
 * @param argc The program's argument count.
 * @param argv The program's arguments.
 * @param cases The executable's cases.
 * @param count How many there are.
 * @return EXIT_SUCCESS if every check of every case run held, else
 * EXIT_FAILURE, as also when a case named is not among them.
 */
int trl_test_main(int argc, char **argv, const trl_test_case *cases, size_t count);

/** @brief Checks that `condition` holds, in the case `test`. */
#define TRL_CHECK(test, condition) trl_test_check((test), !!(condition), #condition, __FILE__, __LINE__)

/** @brief An entry of a cases array, named after its function. */
#define TRL_TEST_CASE(function) {#function, function}

/** @brief The count of entries of an array. */
#define TRL_TEST_COUNT(array) (sizeof(array) / sizeof((array)[0]))

#endif
