/**
 * @file harness.c
 * @brief Runs a test executable's cases and reports what failed
 * (RULES.md D14).
 */
#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The arguments of an executable asked to run one case: itself and it. */
#define TRL_TEST_ONE_CASE_ARGC 2

void trl_test_check(trl_test *test, int passed, const char *expression, const char *file, int line) {
    if (passed) {
        return;
    }
    test->failures++;
    (void)fprintf(stderr, "%s:%d: %s: check failed: %s\n", file, line, test->name, expression);
}

static unsigned int run_case(const trl_test_case *test_case) {
    trl_test test = {test_case->name, 0U};
    test_case->run(&test);
    (void)printf("%s %s\n", test.failures == 0U ? "ok  " : "FAIL", test_case->name);
    return test.failures;
}

int trl_test_main(int argc, char **argv, const trl_test_case *cases, size_t count) {
    const char *only = argc == TRL_TEST_ONE_CASE_ARGC ? argv[1] : NULL;
    unsigned int failures = 0U;
    size_t ran = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (only == NULL || strcmp(only, cases[i].name) == 0) {
            failures += run_case(&cases[i]);
            ++ran;
        }
    }
    if (ran == 0U) {
        (void)fprintf(stderr, "no test case is named %s\n", only == NULL ? "(none)" : only);
        return EXIT_FAILURE;
    }
    return failures == 0U ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Opens the file at `path` as `mode` asks; null if it cannot be. */
static FILE *opened(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *file = NULL;
    if (fopen_s(&file, path, mode) != 0) {
        file = NULL;
    }
    return file;
#else
    return fopen(path, mode);
#endif
}

bool trl_test_read_file(const char *path, char **text) {
    *text = NULL;
    FILE *file = opened(path, "rb");
    if (file == NULL) {
        return false;
    }
    size_t capacity = BUFSIZ;
    size_t length = 0U;
    char *data = malloc(capacity + 1U);
    while (data != NULL) {
        length += fread(data + length, 1U, capacity - length, file);
        if (length < capacity) {
            break;
        }
        capacity *= 2U;
        char *grown = realloc(data, capacity + 1U);
        if (grown == NULL) {
            free(data);
        }
        data = grown;
    }
    bool read = data != NULL && ferror(file) == 0;
    (void)fclose(file);
    if (!read) {
        free(data);
        return false;
    }
    data[length] = '\0';
    *text = data;
    return true;
}

bool trl_test_write_file(const char *path, const char *text) {
    FILE *file = opened(path, "wb");
    if (file == NULL) {
        return false;
    }
    bool written = fputs(text, file) >= 0;
    return fclose(file) == 0 && written;
}
