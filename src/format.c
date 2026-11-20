/**
 * @file format.c
 * @brief Text written into a bounded buffer, cut short to fit.
 */
#include "format.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

/* A second's milliseconds, the digits of a fraction of one, and ten. */
#define TRL_MS_SECOND 1000U
#define TRL_MS_DIGITS 3
#define TRL_TEN 10U

void trl_format_duration(char *out, size_t capacity, uint32_t ms) {
    if (ms == 0U) {
        trl_format(out, capacity, "0ns");
    } else if (ms < TRL_MS_SECOND) {
        trl_format(out, capacity, "%lums", (unsigned long)ms);
    } else if (ms % TRL_MS_SECOND == 0U) {
        trl_format(out, capacity, "%lus", (unsigned long)(ms / TRL_MS_SECOND));
    } else {
        /* The fraction's digits, its trailing zeros dropped, counted
           rather than cut from its text, so no index is left to prove. */
        uint32_t fraction = ms % TRL_MS_SECOND;
        int digits = TRL_MS_DIGITS;
        while (fraction % TRL_TEN == 0U) {
            fraction /= TRL_TEN;
            --digits;
        }
        trl_format(out, capacity, "%lu.%0*lus", (unsigned long)(ms / TRL_MS_SECOND), digits, (unsigned long)fraction);
    }
}
