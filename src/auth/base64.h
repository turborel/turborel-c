/**
 * @file base64.h
 * @brief Base 64 of RFC 4648, padded, as SCRAM writes its binary fields.
 */
#ifndef TRL_AUTH_BASE64_H
#define TRL_AUTH_BASE64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The characters `length` bytes encode to, the null not counted. */
size_t trl_base64_length(size_t length);

/** @brief The bytes `length` characters of base 64 decode to at most, and one more. */
size_t trl_base64_capacity(size_t length);

/**
 * @brief Writes `length` bytes as base 64 into `out`, which holds
 * trl_base64_length(length) characters and a null.
 */
void trl_base64_encode(const uint8_t *bytes, size_t length, char *out);

/**
 * @brief Reads `length` characters of base 64 into `out`, which holds at
 * least `length` / 4 * 3 bytes; `written` gets how many.
 *
 * @return Whether it is base 64: whole groups of four, padding only at
 * the end, at most two, and no bits set past the last byte.
 */
bool trl_base64_decode(const char *text, size_t length, uint8_t *out, size_t *written);

#endif
