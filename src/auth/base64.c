/**
 * @file base64.c
 * @brief Base 64, as turborel's SCRAM reads and writes it: padded, and
 * refusing anything else.
 */
#include "auth/base64.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bits each character carries, and the characters of a group. */
#define TRL_BASE64_BITS 6U
#define TRL_BASE64_GROUP 4U
#define TRL_BASE64_BYTES 3U
#define TRL_BASE64_MASK 0x3fU
#define TRL_BYTE_BITS 8U
#define TRL_BYTE_MASK 0xffU
#define TRL_BASE64_PAD '='
/* The most padding characters a group ends with. */
#define TRL_BASE64_MAX_PADS 2U
/* The bits of the last group each padding character leaves unused. */
#define TRL_BASE64_SPARE_BITS 2U
/* What a character not of the alphabet maps to. */
#define TRL_BASE64_NONE 0xffU

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t trl_base64_capacity(size_t length) {
    return (length / TRL_BASE64_GROUP * TRL_BASE64_BYTES) + 1U;
}

size_t trl_base64_length(size_t length) {
    return (length + TRL_BASE64_BYTES - 1U) / TRL_BASE64_BYTES * TRL_BASE64_GROUP;
}

void trl_base64_encode(const uint8_t *bytes, size_t length, char *out) {
    size_t at = 0U;
    for (size_t i = 0U; i < length; i += TRL_BASE64_BYTES) {
        size_t taken = length - i < TRL_BASE64_BYTES ? length - i : TRL_BASE64_BYTES;
        uint32_t value = 0U;
        for (size_t j = 0U; j < TRL_BASE64_BYTES; ++j) {
            value = (value << TRL_BYTE_BITS) | (j < taken ? bytes[i + j] : 0U);
        }
        for (size_t j = 0U; j < TRL_BASE64_GROUP; ++j) {
            size_t shift = (TRL_BASE64_GROUP - 1U - j) * TRL_BASE64_BITS;
            out[at++] = j <= taken ? alphabet[(value >> shift) & TRL_BASE64_MASK] : TRL_BASE64_PAD;
        }
    }
    out[at] = '\0';
}

/* The value of one character, or TRL_BASE64_NONE. */
static uint32_t value_of(char c) {
    for (uint32_t i = 0U; i < sizeof alphabet - 1U; ++i) {
        if (alphabet[i] == c) {
            return i;
        }
    }
    return TRL_BASE64_NONE;
}

/* Reads one group of four into `out`, the last if `last`; `written` gets
   the bytes it holds. */
static bool decode_group(const char *group, bool last, uint8_t *out, size_t *written) {
    size_t pads = 0U;
    while (pads < TRL_BASE64_GROUP && group[TRL_BASE64_GROUP - 1U - pads] == TRL_BASE64_PAD) {
        ++pads;
    }
    if (pads > TRL_BASE64_MAX_PADS || (pads > 0U && !last)) {
        return false;
    }
    uint32_t value = 0U;
    for (size_t i = 0U; i < TRL_BASE64_GROUP - pads; ++i) {
        uint32_t digit = value_of(group[i]);
        if (digit == TRL_BASE64_NONE) {
            return false;
        }
        value = (value << TRL_BASE64_BITS) | digit;
    }
    uint32_t spare = (uint32_t)pads * TRL_BASE64_SPARE_BITS;
    if ((value & ((1U << spare) - 1U)) != 0U) {
        return false;
    }
    value <<= TRL_BASE64_BITS * pads;
    *written = TRL_BASE64_BYTES - pads;
    for (size_t i = 0U; i < *written; ++i) {
        out[i] = (uint8_t)((value >> ((TRL_BASE64_BYTES - 1U - i) * TRL_BYTE_BITS)) & TRL_BYTE_MASK);
    }
    return true;
}

bool trl_base64_decode(const char *text, size_t length, uint8_t *out, size_t *written) {
    *written = 0U;
    if (length % TRL_BASE64_GROUP != 0U) {
        return false;
    }
    for (size_t i = 0U; i < length; i += TRL_BASE64_GROUP) {
        size_t taken = 0U;
        if (!decode_group(text + i, i + TRL_BASE64_GROUP == length, out + *written, &taken)) {
            return false;
        }
        *written += taken;
    }
    return true;
}
