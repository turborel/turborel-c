/**
 * @file format.h
 * @brief Text written into a buffer of a bounded size, cut short to fit,
 * as a message is: snprintf, its result of no concern, which GCC would
 * otherwise warn of each time a message may be cut.
 */
#ifndef TRL_FORMAT_H
#define TRL_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
/** @brief Says a function takes a printf format, for the compiler to check. */
#define TRL_PRINTF(at, from) __attribute__((format(printf, at, from)))
#else
#define TRL_PRINTF(at, from)
#endif

/** @brief Writes `format` of its arguments into `out`, null-ended, cut short to `capacity`. */
void trl_format(char *out, size_t capacity, const char *format, ...) TRL_PRINTF(3, 4);

/**
 * @brief Writes a time of `ms` milliseconds as turborel-client's messages
 * show one, as Rust shows a Duration: 0ns, 250ms, 30s, 1.5s.
 */
void trl_format_duration(char *out, size_t capacity, uint32_t ms);

#endif
