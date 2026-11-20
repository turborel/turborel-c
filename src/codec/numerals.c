/**
 * @file numerals.c
 * @brief Approximate numbers and DECFLOATs as JSON text, as turborel
 * writes them (`value/cast.rs`, `value/decfloat.rs`).
 *
 * The shortest numeral of a double is found by trying 1 to 17 significant
 * digits, each correctly rounded by `snprintf`, and taking the first that
 * reads back, in the "C" locale, as the same double: the nearest of the
 * shortest, as turborel's writes it. Only digits and the exponent are
 * taken from what the C library writes, so the locale's decimal point
 * never shows.
 */
#include "codec/numerals.h"

#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/int128.h"
#include "codec/strtod.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The most significant digits a double ever needs to read back. */
#define TRL_DOUBLE_DIGITS 17
/* Room for a double written in `%e` form, its sign and exponent too. */
#define TRL_NUMERAL_BYTES 40U
/* The decimal radix. */
#define TRL_TEN 10
/* The sign bit of a double's bits; the rest is zero of either zero. */
#define TRL_DOUBLE_SIGN 0x8000000000000000U

/* The bits of `x`, by which doubles are compared (R-NUM-4). */
static uint64_t bits_of(double x) {
    uint64_t bits = 0U;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}

/* A double's numeral: its significant digits and its exponent. */
typedef struct numeral {
    char digits[TRL_NUMERAL_BYTES];
    size_t count;
    int exponent;
} numeral;

/* The digits and the exponent `%.*e` wrote into `text`, whatever decimal
   point the locale gave it. */
static void split_numeral(const char *text, numeral *out) {
    const char *at = text;
    out->count = 0U;
    while (*at != '\0' && *at != 'e' && *at != 'E') {
        if (*at >= '0' && *at <= '9') {
            out->digits[out->count++] = *at;
        }
        ++at;
    }
    out->exponent = (int)strtol(*at == '\0' ? at : at + 1, NULL, TRL_TEN);
}

/* The numeral of `x` to `digits` significant digits, correctly rounded. */
static void numeral_of(double x, int digits, numeral *out) {
    char text[TRL_NUMERAL_BYTES];
    (void)snprintf(text, sizeof text, "%.*e", digits - 1, x);
    split_numeral(text, out);
}

/* Whether `n`, of the sign of `bits`, reads back as the double of `bits`,
   read in the "C" locale. */
static bool reads_back(const numeral *n, uint64_t bits) {
    char text[TRL_NUMERAL_BYTES];
    int written = snprintf(text, sizeof text, "%s%c.%.*se%d", (bits & TRL_DOUBLE_SIGN) != 0U ? "-" : "", n->digits[0],
                           (int)(n->count - 1U), n->digits + 1, n->exponent);
    double back = 0.0;
    return written > 0 && trl_strtod_c(text, &back) && bits_of(back) == bits;
}

bool trl_put_approximate_numeral(trl_buffer *buffer, uint64_t bits) {
    double x = 0.0;
    memcpy(&x, &bits, sizeof x);
    if (!isfinite(x)) {
        return false;
    }
    if ((bits & ~TRL_DOUBLE_SIGN) == 0U) {
        trl_put_raw(buffer, (const uint8_t *)"0E0", sizeof "0E0" - 1U);
        return true;
    }
    numeral n;
    for (int digits = 1; digits <= TRL_DOUBLE_DIGITS; ++digits) {
        numeral_of(x, digits, &n);
        if (reads_back(&n, bits)) {
            break;
        }
    }
    if ((bits & TRL_DOUBLE_SIGN) != 0U) {
        trl_put_u8(buffer, (uint8_t)'-');
    }
    trl_put_raw(buffer, (const uint8_t *)n.digits, 1U);
    trl_put_u8(buffer, (uint8_t)'.');
    if (n.count > 1U) {
        trl_put_raw(buffer, (const uint8_t *)n.digits + 1, n.count - 1U);
    } else {
        trl_put_u8(buffer, (uint8_t)'0');
    }
    char power[TRL_NUMERAL_BYTES];
    int written = snprintf(power, sizeof power, "E%d", n.exponent);
    trl_put_raw(buffer, (const uint8_t *)power, written > 0 ? (size_t)written : 0U);
    return true;
}

/* Appends `count` zeros. */
static void put_zeros(trl_buffer *buffer, uint64_t count) {
    for (uint64_t i = 0U; i < count; ++i) {
        trl_put_u8(buffer, (uint8_t)'0');
    }
}

void trl_put_decfloat_numeral(trl_buffer *buffer, trl_decfloat value) {
    char digits[TRL_U128_DIGITS];
    size_t count = trl_uint128_to_digits(trl_int128_magnitude(value.coefficient), digits);
    if (trl_int128_negative(value.coefficient)) {
        trl_put_u8(buffer, (uint8_t)'-');
    }
    if (value.exponent >= 0) {
        trl_put_raw(buffer, (const uint8_t *)digits, count);
        put_zeros(buffer, (uint64_t)value.exponent);
        return;
    }
    uint64_t places = (uint64_t)(-(int64_t)value.exponent);
    if (places >= count) {
        trl_put_raw(buffer, (const uint8_t *)"0.", sizeof "0." - 1U);
        put_zeros(buffer, places - count);
        trl_put_raw(buffer, (const uint8_t *)digits, count);
        return;
    }
    size_t whole = count - (size_t)places;
    trl_put_raw(buffer, (const uint8_t *)digits, whole);
    trl_put_u8(buffer, (uint8_t)'.');
    trl_put_raw(buffer, (const uint8_t *)digits + whole, (size_t)places);
}
