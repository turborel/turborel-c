/**
 * @file format.c
 * @brief Text written into a bounded buffer, cut short to fit.
 */
#include "format.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void trl_format(char *out, size_t capacity, const char *format, ...) {
    if (out == NULL || capacity == 0U) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    /* A message cut short still says what failed first. */
    (void)vsnprintf(out, capacity, format, arguments);
    va_end(arguments);
}

/* A second's milliseconds, and room for a fraction of one. */
#define TRL_MS_SECOND 1000U
#define TRL_FRACTION_BYTES 8U

void trl_format_duration(char *out, size_t capacity, uint32_t ms) {
    if (ms == 0U) {
        trl_format(out, capacity, "0ns");
    } else if (ms < TRL_MS_SECOND) {
        trl_format(out, capacity, "%lums", (unsigned long)ms);
    } else if (ms % TRL_MS_SECOND == 0U) {
        trl_format(out, capacity, "%lus", (unsigned long)(ms / TRL_MS_SECOND));
    } else {
        char fraction[TRL_FRACTION_BYTES];
        trl_format(fraction, sizeof fraction, "%03lu", (unsigned long)(ms % TRL_MS_SECOND));
        size_t length = strlen(fraction);
        while (fraction[length - 1U] == '0') {
            fraction[--length] = '\0';
        }
        trl_format(out, capacity, "%lu.%ss", (unsigned long)(ms / TRL_MS_SECOND), fraction);
    }
}
