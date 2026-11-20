/**
 * @file format.h
 * @brief Text written into a buffer of a bounded size, cut short to fit,
 * as a message is: snprintf, its result of no concern, which GCC would
 * otherwise warn of each time a message may be cut.
 */
#ifndef TRL_FORMAT_H
#define TRL_FORMAT_H

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
/** @brief Says a function takes a printf format, for the compiler to check. */
#define TRL_PRINTF(at, from) __attribute__((format(printf, at, from)))
#else
#define TRL_PRINTF(at, from)
#endif

/** @brief Writes `format` of its arguments into `out`, null-ended, cut short to `capacity`. */
void trl_format(char *out, size_t capacity, const char *format, ...) TRL_PRINTF(3, 4);

#endif
