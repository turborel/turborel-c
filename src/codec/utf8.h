/**
 * @file utf8.h
 * @brief Whether bytes are UTF-8, as every string of the protocol must be
 * [PROTOCOL Values and fields].
 */
#ifndef TRL_CODEC_UTF8_H
#define TRL_CODEC_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Whether `bytes` are well-formed UTF-8 (RFC 3629): no overlong
 * form, no surrogate, nothing past U+10FFFF, no sequence cut short.
 *
 * @param bytes The bytes; may be null if `length` is 0.
 * @param length How many.
 * @return Nonzero if they are.
 */
bool trl_utf8_valid(const uint8_t *bytes, size_t length);

/**
 * @brief The octets of the white space character `bytes` begins with, as
 * Unicode's White_Space property has it and Rust's `char::is_whitespace`
 * takes it: tab, line feed, line tabulation, form feed, carriage return,
 * space, U+0085, U+00A0, U+1680, U+2000 to U+200A, U+2028, U+2029,
 * U+202F, U+205F and U+3000.
 *
 * @param bytes The bytes; may be null if `length` is 0.
 * @param length How many.
 * @return Its octets, 1 to 3; 0 if `bytes` begins with none.
 */
size_t trl_utf8_space_at(const uint8_t *bytes, size_t length);

/**
 * @brief The octets of the white space character `bytes` ends with, as
 * trl_utf8_space_at() takes one.
 *
 * @param bytes The bytes; may be null if `length` is 0.
 * @param length How many.
 * @return Its octets, 1 to 3; 0 if `bytes` ends with none.
 */
size_t trl_utf8_space_before(const uint8_t *bytes, size_t length);

#endif
