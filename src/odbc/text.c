/**
 * @file text.c
 * @brief Text between the driver and an application (RULES.md D2): the
 * driver keeps UTF-8, which its ANSI entry points pass through as it is;
 * its Unicode entry points and SQL_C_WCHAR take and give SQLWCHAR, UTF-16
 * where SQLWCHAR has two bytes, as on Windows and with unixODBC, and
 * UTF-32 where it has four, as with iODBC.
 */
#include "memory.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* UTF-8's bytes: one of a character of one byte, the marks of a byte that
   continues a character, and of the leads of two, three and four. */
#define TRL_ONE_BYTE 0x80U
#define TRL_CONTINUES_MASK 0xC0U
#define TRL_CONTINUES 0x80U
#define TRL_LEAD_TWO 0xC0U
#define TRL_LEAD_THREE 0xE0U
#define TRL_LEAD_FOUR 0xF0U
#define TRL_SIX_BITS 0x3FU
/* The code point's bits each lead holds, and the bytes of its character. */
#define TRL_LEAD_TWO_BITS 0x1FU
#define TRL_LEAD_THREE_BITS 0x0FU
#define TRL_LEAD_FOUR_BITS 0x07U
#define TRL_TWO_LONG 2U
#define TRL_THREE_LONG 3U
#define TRL_FOUR_LONG 4U
#define TRL_BITS 6U
/* The first code points needing two, three and four bytes, the last, and
   the surrogates UTF-16 writes those past the first plane as. */
#define TRL_TWO_BYTES 0x80U
#define TRL_THREE_BYTES 0x800U
#define TRL_FOUR_BYTES 0x10000U
#define TRL_LAST_CODE_POINT 0x10FFFFU
#define TRL_HIGH_SURROGATE 0xD800U
#define TRL_LOW_SURROGATE 0xDC00U
#define TRL_SURROGATE_BITS 10U
#define TRL_SURROGATE_MASK 0x3FFU
#define TRL_SURROGATE_END 0xE000U
/* What a character that cannot be decoded is taken as. */
#define TRL_REPLACEMENT 0xFFFDU
/* The UTF-16 units of a code point past the first plane. */
#define TRL_PAIR 2U

/* Whether SQLWCHAR holds UTF-16. */
static bool utf16(void) {
    return sizeof(SQLWCHAR) == sizeof(uint16_t);
}

/* Each lead of a character of more than one byte: the bits of the code
   point it holds, and the bytes of the character. */
static const struct {
    uint32_t lead;
    uint32_t bits;
    size_t bytes;
} leads_of[] = {
    {TRL_LEAD_FOUR, TRL_LEAD_FOUR_BITS, TRL_FOUR_LONG},
    {TRL_LEAD_THREE, TRL_LEAD_THREE_BITS, TRL_THREE_LONG},
    {TRL_LEAD_TWO, TRL_LEAD_TWO_BITS, TRL_TWO_LONG},
};

/* Decodes the character at `*at` of `text`, passing over it. */
static uint32_t decoded(const uint8_t *text, size_t *at) {
    uint32_t c = text[*at];
    size_t count = 1U;
    for (size_t i = 0U; count == 1U && i < sizeof leads_of / sizeof leads_of[0]; ++i) {
        if (c >= leads_of[i].lead) {
            c &= leads_of[i].bits;
            count = leads_of[i].bytes;
        }
    }
    if (count == 1U && c >= TRL_ONE_BYTE) {
        ++*at;
        return TRL_REPLACEMENT;
    }
    ++*at;
    for (size_t i = 1U; i < count; ++i) {
        uint32_t next = text[*at];
        if ((next & TRL_CONTINUES_MASK) != TRL_CONTINUES) {
            return TRL_REPLACEMENT;
        }
        c = (c << TRL_BITS) | (next & TRL_SIX_BITS);
        ++*at;
    }
    return c;
}

/* The SQLWCHAR units `c` takes. */
static size_t units_of(uint32_t c) {
    return utf16() && c >= TRL_FOUR_BYTES ? TRL_PAIR : 1U;
}

/* Writes `c` as SQLWCHAR at `out`. */
static void wide_put(SQLWCHAR *out, uint32_t c) {
    if (utf16() && c >= TRL_FOUR_BYTES) {
        uint32_t v = c - TRL_FOUR_BYTES;
        out[0] = (SQLWCHAR)(TRL_HIGH_SURROGATE | (v >> TRL_SURROGATE_BITS));
        out[1] = (SQLWCHAR)(TRL_LOW_SURROGATE | (v & TRL_SURROGATE_MASK));
    } else {
        out[0] = (SQLWCHAR)c;
    }
}

size_t trl_odbc_widen(const char *text, SQLWCHAR *out, size_t capacity) {
    const uint8_t *bytes = (const uint8_t *)text;
    size_t at = 0U;
    size_t units = 0U;
    size_t written = 0U;
    bool cut = false;
    while (bytes[at] != 0U) {
        uint32_t c = decoded(bytes, &at);
        size_t needs = units_of(c);
        if (!cut && out != NULL && written + needs < capacity) {
            wide_put(out + written, c);
            written += needs;
        } else {
            cut = true;
        }
        units += needs;
    }
    if (out != NULL && capacity > 0U) {
        out[written] = 0;
    }
    return units;
}

/* Appends code point `c` as UTF-8. */
static void narrow_put(uint8_t *out, size_t *at, uint32_t c) {
    if (c < TRL_TWO_BYTES) {
        out[(*at)++] = (uint8_t)c;
        return;
    }
    size_t trailing = TRL_FOUR_LONG - 1U;
    if (c < TRL_THREE_BYTES) {
        trailing = TRL_TWO_LONG - 1U;
    } else if (c < TRL_FOUR_BYTES) {
        trailing = TRL_THREE_LONG - 1U;
    }
    static const uint8_t leads[] = {0U, TRL_LEAD_TWO, TRL_LEAD_THREE, TRL_LEAD_FOUR};
    out[(*at)++] = (uint8_t)(leads[trailing] | (c >> (trailing * TRL_BITS)));
    for (size_t i = trailing; i-- > 0U;) {
        out[(*at)++] = (uint8_t)(TRL_CONTINUES | ((c >> (i * TRL_BITS)) & TRL_SIX_BITS));
    }
}

/* The code point of the SQLWCHAR units at `*at`, passing over them. */
static uint32_t wide_decoded(const SQLWCHAR *text, size_t length, size_t *at) {
    uint32_t c = (uint32_t)text[(*at)++];
    bool high = utf16() && c >= TRL_HIGH_SURROGATE && c < TRL_LOW_SURROGATE;
    if (high && *at < length) {
        uint32_t low = (uint32_t)text[*at];
        if (low >= TRL_LOW_SURROGATE && low < TRL_SURROGATE_END) {
            ++*at;
            return TRL_FOUR_BYTES + (((c - TRL_HIGH_SURROGATE) << TRL_SURROGATE_BITS) | (low - TRL_LOW_SURROGATE));
        }
    }
    bool surrogate = c >= TRL_HIGH_SURROGATE && c < TRL_SURROGATE_END;
    return surrogate || c > TRL_LAST_CODE_POINT ? TRL_REPLACEMENT : c;
}

char *trl_odbc_wide_text(const SQLWCHAR *text, SQLINTEGER length) {
    if (text == NULL) {
        return NULL;
    }
    size_t units = 0U;
    if (length == SQL_NTS) {
        while (text[units] != 0) {
            ++units;
        }
    } else if (length >= 0) {
        units = (size_t)length;
    } else {
        return NULL;
    }
    /* A unit makes at most three bytes; a pair of two, four. */
    char *out = trl_malloc((units * TRL_THREE_LONG) + 1U);
    if (out == NULL) {
        return NULL;
    }
    size_t at = 0U;
    size_t written = 0U;
    while (at < units) {
        narrow_put((uint8_t *)out, &written, wide_decoded(text, units, &at));
    }
    out[written] = '\0';
    return out;
}

char *trl_odbc_text(const SQLCHAR *text, SQLINTEGER length) {
    if (text == NULL || (length < 0 && length != SQL_NTS)) {
        return NULL;
    }
    size_t bytes = length == SQL_NTS ? strlen((const char *)text) : (size_t)length;
    char *out = trl_malloc(bytes + 1U);
    if (out != NULL) {
        memcpy(out, text, bytes);
        out[bytes] = '\0';
    }
    return out;
}

/* Gives `length` to wherever `out` asks for it. */
static void length_put(const trl_odbc_out *out, size_t length) {
    if (out->small_length != NULL) {
        *out->small_length = (SQLSMALLINT)(length > INT16_MAX ? INT16_MAX : length);
    }
    if (out->length != NULL) {
        *out->length = (SQLINTEGER)(length > INT32_MAX ? INT32_MAX : length);
    }
    if (out->long_length != NULL) {
        *out->long_length = (SQLLEN)length;
    }
}

/* Writes `text` as SQLWCHAR; whether it was cut. */
static bool wide_out(const trl_odbc_out *out, const char *text) {
    size_t capacity = out->capacity < 0 ? 0U : (size_t)out->capacity;
    capacity = out->bytes ? capacity / sizeof(SQLWCHAR) : capacity;
    size_t units = trl_odbc_widen(text, out->buffer, out->buffer == NULL ? 0U : capacity);
    length_put(out, out->bytes ? units * sizeof(SQLWCHAR) : units);
    return out->buffer != NULL && units >= capacity;
}

/* Writes `text` as UTF-8, cut at a character's start; whether it was cut. */
static bool narrow_out(const trl_odbc_out *out, const char *text) {
    size_t length = strlen(text);
    length_put(out, length);
    if (out->buffer == NULL) {
        return false;
    }
    size_t capacity = out->capacity < 0 ? 0U : (size_t)out->capacity;
    if (capacity == 0U) {
        return true;
    }
    size_t taken = length < capacity ? length : capacity - 1U;
    while (taken > 0U && taken < length && ((uint8_t)text[taken] & TRL_CONTINUES_MASK) == TRL_CONTINUES) {
        --taken;
    }
    memcpy(out->buffer, text, taken);
    ((char *)out->buffer)[taken] = '\0';
    return taken < length;
}

SQLRETURN trl_odbc_put(trl_odbc_diag *diag, const trl_odbc_out *out, const char *text) {
    bool cut = out->wide ? wide_out(out, text) : narrow_out(out, text);
    if (!cut) {
        return SQL_SUCCESS;
    }
    if (diag != NULL) {
        (void)trl_odbc_warn(diag, "01004", "String data, right truncated");
    }
    return SQL_SUCCESS_WITH_INFO;
}
