/**
 * @file utf8.c
 * @brief UTF-8 checked by the table of well-formed byte sequences of
 * RFC 3629 §4, which is what Rust's `str::from_utf8` takes, so the driver
 * refuses what turborel-client refuses.
 */
#include "codec/utf8.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The last byte that stands alone, ASCII. */
#define TRL_ASCII_LAST 0x7FU
/* The most octets of a white space character, and ASCII's of them. */
#define TRL_SPACE_MOST 3U
#define TRL_SPACE 0x20U
#define TRL_TAB 0x09U
#define TRL_CARRIAGE_RETURN 0x0DU
/* The bounds of a continuation byte. */
#define TRL_CONTINUATION_FIRST 0x80U
#define TRL_CONTINUATION_LAST 0xBFU
/* The lead bytes of sequences of two, three and four bytes. */
#define TRL_LEAD2_FIRST 0xC2U
#define TRL_LEAD2_LAST 0xDFU
#define TRL_LEAD3_FIRST 0xE0U
#define TRL_LEAD3_LAST 0xEFU
#define TRL_LEAD4_FIRST 0xF0U
#define TRL_LEAD4_LAST 0xF4U
/* The leads whose second byte is narrower: E0 (no overlong), ED (no
   surrogate), F0 (no overlong) and F4 (nothing past U+10FFFF). */
#define TRL_LEAD_E0 0xE0U
#define TRL_LEAD_ED 0xEDU
#define TRL_LEAD_F0 0xF0U
#define TRL_LEAD_F4 0xF4U
#define TRL_E0_SECOND_FIRST 0xA0U
#define TRL_ED_SECOND_LAST 0x9FU
#define TRL_F0_SECOND_FIRST 0x90U
#define TRL_F4_SECOND_LAST 0x8FU
/* The bytes of the longest sequences. */
#define TRL_TWO 2U
#define TRL_THREE 3U
#define TRL_FOUR 4U

/* Whether `byte` lies in `first` to `last`. */
static bool within(uint8_t byte, unsigned int first, unsigned int last) {
    return byte >= first && byte <= last;
}

/* The bounds of the second byte after the lead `lead`. */
static void second_bounds(uint8_t lead, unsigned int *first, unsigned int *last) {
    *first = TRL_CONTINUATION_FIRST;
    *last = TRL_CONTINUATION_LAST;
    if (lead == TRL_LEAD_E0) {
        *first = TRL_E0_SECOND_FIRST;
    } else if (lead == TRL_LEAD_ED) {
        *last = TRL_ED_SECOND_LAST;
    } else if (lead == TRL_LEAD_F0) {
        *first = TRL_F0_SECOND_FIRST;
    } else if (lead == TRL_LEAD_F4) {
        *last = TRL_F4_SECOND_LAST;
    }
}

/* The bytes of the sequence led by `lead`; 0 if it leads none. */
static size_t sequence_length(uint8_t lead) {
    if (lead <= TRL_ASCII_LAST) {
        return 1U;
    }
    if (within(lead, TRL_LEAD2_FIRST, TRL_LEAD2_LAST)) {
        return TRL_TWO;
    }
    if (within(lead, TRL_LEAD3_FIRST, TRL_LEAD3_LAST)) {
        return TRL_THREE;
    }
    if (within(lead, TRL_LEAD4_FIRST, TRL_LEAD4_LAST)) {
        return TRL_FOUR;
    }
    return 0U;
}

/* Whether the `count` bytes from `at` are one well-formed sequence. */
static bool sequence_valid(const uint8_t *at, size_t count) {
    unsigned int first = 0U;
    unsigned int last = 0U;
    second_bounds(at[0], &first, &last);
    if (!within(at[1], first, last)) {
        return false;
    }
    for (size_t i = TRL_TWO; i < count; ++i) {
        if (!within(at[i], TRL_CONTINUATION_FIRST, TRL_CONTINUATION_LAST)) {
            return false;
        }
    }
    return true;
}

bool trl_utf8_valid(const uint8_t *bytes, size_t length) {
    size_t at = 0U;
    while (at < length) {
        size_t count = sequence_length(bytes[at]);
        if (count == 0U || count > length - at) {
            return false;
        }
        if (count > 1U && !sequence_valid(bytes + at, count)) {
            return false;
        }
        at += count;
    }
    return true;
}

/* A white space character of more than one octet, as UTF-8 writes it:
   its lead octet, and the range of its last; a second octet of two-octet
   ones is that range. */
typedef struct space {
    uint8_t octets[TRL_SPACE_MOST];
    uint8_t last_to;
    uint8_t length;
} space;

/* The white space characters past ASCII: U+0085, U+00A0, U+1680, U+2000
   to U+200A, U+2028 and U+2029, U+202F, U+205F, U+3000. */
static const space spaces[] = {
    {{0xC2U, 0x85U, 0x00U}, 0x85U, 2U}, {{0xC2U, 0xA0U, 0x00U}, 0xA0U, 2U}, {{0xE1U, 0x9AU, 0x80U}, 0x80U, 3U},
    {{0xE2U, 0x80U, 0x80U}, 0x8AU, 3U}, {{0xE2U, 0x80U, 0xA8U}, 0xA9U, 3U}, {{0xE2U, 0x80U, 0xAFU}, 0xAFU, 3U},
    {{0xE2U, 0x81U, 0x9FU}, 0x9FU, 3U}, {{0xE3U, 0x80U, 0x80U}, 0x80U, 3U},
};

/* Whether the `length` octets at `bytes` are the space `s`. */
static bool is_space(const uint8_t *bytes, size_t length, const space *s) {
    if (length < s->length) {
        return false;
    }
    size_t last = (size_t)s->length - 1U;
    return memcmp(bytes, s->octets, last) == 0 && bytes[last] >= s->octets[last] && bytes[last] <= s->last_to;
}

/* Whether `c` is white space of ASCII: tab to carriage return, and space. */
static bool ascii_space(uint8_t c) {
    return c == TRL_SPACE || (c >= TRL_TAB && c <= TRL_CARRIAGE_RETURN);
}

size_t trl_utf8_space_at(const uint8_t *bytes, size_t length) {
    if (length == 0U) {
        return 0U;
    }
    if (ascii_space(bytes[0])) {
        return 1U;
    }
    for (size_t i = 0U; i < sizeof spaces / sizeof spaces[0]; ++i) {
        if (is_space(bytes, length, &spaces[i])) {
            return spaces[i].length;
        }
    }
    return 0U;
}

size_t trl_utf8_space_before(const uint8_t *bytes, size_t length) {
    if (length == 0U) {
        return 0U;
    }
    if (ascii_space(bytes[length - 1U])) {
        return 1U;
    }
    for (size_t i = 0U; i < sizeof spaces / sizeof spaces[0]; ++i) {
        size_t n = spaces[i].length;
        if (length >= n && is_space(bytes + length - n, n, &spaces[i])) {
            return n;
        }
    }
    return 0U;
}
