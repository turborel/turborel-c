/**
 * @file decimal.c
 * @brief Exact numbers and their text, with nothing lost [PROTOCOL Values]
 * (RULES.md D2, R-NUM-3).
 *
 * The text is turborel's, as its `Decimal` writes one: the units' digits,
 * a minus sign before them if negative, and the last `scale` of them after
 * a decimal point, zeros before them where there are fewer; a scale past
 * 18, turborel's MAX_EXACT_PRECISION, as a power of ten instead.
 */
#include "codec/int128.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The greatest scale written with a decimal point, turborel's
   MAX_EXACT_PRECISION; a greater one is written as a power of ten. */
#define TRL_MAX_POINT_SCALE 18U
/* The decimal radix, and the digits of a u32's greatest value. */
#define TRL_TEN 10U
#define TRL_U32_DIGITS 10U
/* The greatest power of ten read: past it, no scale of a u32 and no units
   of 128 bits hold the number, whatever its digits. */
#define TRL_MAX_POWER 4294967400

/* Text being written into a caller's buffer. */
typedef struct text_out {
    char *out;
    size_t capacity;
    size_t at;
} text_out;

/* Appends `count` bytes of `text`, if they fit. */
static bool append(text_out *text, const char *bytes, size_t count) {
    if (count > text->capacity || text->at > text->capacity - count) {
        return false;
    }
    memcpy(text->out + text->at, bytes, count);
    text->at += count;
    return true;
}

/* Appends `count` zeros. */
static bool zeros(text_out *text, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        if (!append(text, "0", 1U)) {
            return false;
        }
    }
    return true;
}

/* Appends the decimal digits of `value`. */
static bool append_u32(text_out *text, uint32_t value) {
    char digits[TRL_U32_DIGITS];
    size_t count = 0U;
    do {
        digits[TRL_U32_DIGITS - 1U - count] = (char)('0' + (value % TRL_TEN));
        value /= TRL_TEN;
        ++count;
    } while (value != 0U);
    return append(text, digits + (TRL_U32_DIGITS - count), count);
}

/* Appends `digits` with a decimal point before the last `scale` of them. */
static bool append_point(text_out *text, const char *digits, size_t count, uint32_t scale) {
    if (scale >= count) {
        return append(text, "0.", sizeof "0." - 1U) && zeros(text, scale - count) && append(text, digits, count);
    }
    size_t whole = count - scale;
    return append(text, digits, whole) && append(text, ".", 1U) && append(text, digits + whole, scale);
}

trl_status trl_decimal_text(const trl_decimal *number, char *out, size_t capacity) {
    if (number == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    char digits[TRL_U128_DIGITS];
    size_t count = trl_uint128_to_digits(trl_int128_magnitude(number->units), digits);
    text_out text = {out, capacity, 0U};
    bool fits = !trl_int128_negative(number->units) || append(&text, "-", 1U);
    if (number->scale == 0U) {
        fits = fits && append(&text, digits, count);
    } else if (number->scale > TRL_MAX_POINT_SCALE) {
        fits = fits && append(&text, digits, count) && append(&text, "E-", sizeof "E-" - 1U) &&
               append_u32(&text, number->scale);
    } else {
        fits = fits && append_point(&text, digits, count, number->scale);
    }
    if (!fits || text.at >= capacity) {
        return TRL_INVALID_ARGUMENT;
    }
    out[text.at] = '\0';
    return TRL_OK;
}

/* A number's parts as read: its digits' value, how many follow the point,
   its power of ten, and its sign. */
typedef struct parsed {
    trl_uint128 magnitude;
    uint64_t fraction;
    int64_t power;
    bool negative;
} parsed;

/* Whether `c` is a decimal digit. */
static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Reads digits into `value` from `*text`, counting them; out of range if
   the value passes 128 bits. */
static trl_status read_digits(const char **text, trl_uint128 *value, uint64_t *count) {
    const char *at = *text;
    while (is_digit(*at)) {
        if (!trl_uint128_times_ten_plus(value, (unsigned int)(*at - '0'))) {
            return TRL_OUT_OF_RANGE;
        }
        ++at;
    }
    *count = (uint64_t)(at - *text);
    *text = at;
    return TRL_OK;
}

/* Reads an exponent, `E` or `e`, a sign and digits, into `*power`. */
static trl_status read_power(const char **text, int64_t *power) {
    const char *at = *text + 1;
    bool negative = *at == '-';
    if (*at == '-' || *at == '+') {
        ++at;
    }
    if (!is_digit(*at)) {
        return TRL_INVALID_TEXT;
    }
    int64_t value = 0;
    while (is_digit(*at)) {
        if (value > TRL_MAX_POWER) {
            return TRL_OUT_OF_RANGE;
        }
        value = (value * (int64_t)TRL_TEN) + (*at - '0');
        ++at;
    }
    *power = negative ? -value : value;
    *text = at;
    return TRL_OK;
}

/* Reads the text's sign, digits, point, more digits and power. */
static trl_status read_parts(const char *text, parsed *out) {
    out->negative = *text == '-';
    if (*text == '-' || *text == '+') {
        ++text;
    }
    uint64_t whole = 0U;
    trl_status status = read_digits(&text, &out->magnitude, &whole);
    if (status == TRL_OK && *text == '.') {
        ++text;
        status = read_digits(&text, &out->magnitude, &out->fraction);
    }
    if (status != TRL_OK) {
        return status;
    }
    if (whole + out->fraction == 0U) {
        return TRL_INVALID_TEXT;
    }
    if (*text == 'E' || *text == 'e') {
        status = read_power(&text, &out->power);
    }
    return status == TRL_OK && *text != '\0' ? TRL_INVALID_TEXT : status;
}

trl_status trl_decimal_parse(const char *text, trl_decimal *out) {
    if (text == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    parsed parts = {{0U, 0U}, 0U, 0, false};
    trl_status status = read_parts(text, &parts);
    if (status != TRL_OK) {
        return status;
    }
    int64_t scale = (int64_t)parts.fraction - parts.power;
    if (trl_uint128_zero(parts.magnitude) && scale < 0) {
        scale = 0;
    }
    for (; scale < 0; ++scale) {
        if (!trl_uint128_times_ten_plus(&parts.magnitude, 0U)) {
            return TRL_OUT_OF_RANGE;
        }
    }
    trl_int128 units = {0U, 0};
    if (scale > (int64_t)UINT32_MAX || !trl_int128_of(parts.magnitude, parts.negative, &units)) {
        return TRL_OUT_OF_RANGE;
    }
    out->units = units;
    out->scale = (uint32_t)scale;
    return TRL_OK;
}
