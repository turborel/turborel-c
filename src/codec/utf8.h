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

#endif
