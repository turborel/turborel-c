/**
 * @file format.c
 * @brief Text written into a bounded buffer, cut short to fit.
 */
#include "format.h"

#include <stdarg.h>
#include <stddef.h>
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
