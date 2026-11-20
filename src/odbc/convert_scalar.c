/**
 * @file convert_scalar.c
 * @brief A value given as a C type that is not a string (RULES.md D2), as
 * ODBC's reference's Appendix D converts it: an exact or approximate
 * number, a truth value or a number's text as an integer, a float or
 * SQL_C_NUMERIC, its fraction dropped with 01S07, out of range 22003; a
 * truth value as SQL_C_BIT; a datetime, or its text, as ODBC's date, time
 * and timestamp structures, in its own zone; a UUID as SQLGUID. Each goes
 * through the value's text where C has no type to hold it whole.
 */
#include "format.h"
#include "odbc/convert.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Room for a number's text; SQL_C_NUMERIC's magnitude's bytes and its
   most digits; the radix, and a byte's bits and mask. */
#define TRL_NUMBER_BYTES 96U
#define TRL_NUMERIC_BYTES 16U
#define TRL_NUMERIC_MOST 38
#define TRL_DECIMAL_BASE 10U
#define TRL_BYTE_BITS 8U
#define TRL_BYTE_MASK 0xFFU
/* What a truth value is less than. */
#define TRL_BIT_PAST 2.0

/* A number as read: exact, as its text, or approximate. */
typedef struct number {
    bool exact;
    char text[TRL_NUMBER_BYTES];
    double approximate;
} number;

/* Whether `text` is a number whole: an exact one's text, or an approximate one's. */
static bool parsed(const char *text, number *out) {
    trl_decimal decimal;
    if (trl_decimal_parse(text, &decimal) == TRL_OK) {
        out->exact = true;
        return trl_decimal_text(&decimal, out->text, sizeof out->text) == TRL_OK;
    }
    char *end = NULL;
    errno = 0;
    double d = strtod(text, &end);
    if (end == text || *end != '\0') {
        return false;
    }
    out->exact = false;
    out->approximate = d;
    return true;
}

/* The value as a number; SQL_ERROR, 22018, of one that is none. */
static SQLRETURN number_of(trl_odbc_diag *diag, const trl_odbc_piece *piece, number *out) {
    memset(out, 0, sizeof *out);
    trl_kind kind = trl_value_kind(piece->value);
    trl_decimal decimal;
    if (kind == TRL_KIND_EXACT && trl_value_decimal(piece->value, &decimal) == TRL_OK) {
        out->exact = true;
        return trl_decimal_text(&decimal, out->text, sizeof out->text) == TRL_OK ? SQL_SUCCESS
                                                                                 : trl_odbc_no_memory(diag);
    }
    if (kind == TRL_KIND_APPROXIMATE) {
        (void)trl_value_double(piece->value, &out->approximate);
        return SQL_SUCCESS;
    }
    if (kind == TRL_KIND_BOOLEAN) {
        bool truth = false;
        (void)trl_value_boolean(piece->value, &truth);
        out->exact = true;
        out->text[0] = truth ? '1' : '0';
        return SQL_SUCCESS;
    }
    bool numeral = kind == TRL_KIND_CHARACTER || kind == TRL_KIND_DECFLOAT;
    if (numeral && piece->text != NULL && parsed(piece->text, out)) {
        return SQL_SUCCESS;
    }
    return numeral ? trl_odbc_fail(diag, "22018", "Invalid character value for cast specification")
                   : trl_odbc_fail(diag, "07006", "Restricted data type attribute violation");
}

/* An exact number's integer part, as text, and whether a fraction was
   dropped; into `whole`. */
static bool whole_of(const char *text, char *whole, size_t capacity) {
    const char *point = strchr(text, '.');
    size_t length = point != NULL ? (size_t)(point - text) : strlen(text);
    length = length < capacity - 1U ? length : capacity - 1U;
    memcpy(whole, text, length);
    whole[length] = '\0';
    bool dropped = false;
    for (const char *at = point != NULL ? point + 1 : ""; *at != '\0'; ++at) {
        dropped = dropped || *at != '0';
    }
    return dropped;
}

/* The bounds of an integer C type, and its bytes; whether `c_type` is one. */
typedef struct bounds {
    SQLSMALLINT c_type;
    long long low;
    unsigned long long high;
    size_t bytes;
} bounds;

static const bounds integers[] = {
    {SQL_C_STINYINT, SCHAR_MIN, SCHAR_MAX, sizeof(signed char)},
    {SQL_C_TINYINT, SCHAR_MIN, SCHAR_MAX, sizeof(signed char)},
    {SQL_C_UTINYINT, 0, UCHAR_MAX, sizeof(unsigned char)},
    {SQL_C_SSHORT, SHRT_MIN, SHRT_MAX, sizeof(short)},
    {SQL_C_SHORT, SHRT_MIN, SHRT_MAX, sizeof(short)},
    {SQL_C_USHORT, 0, USHRT_MAX, sizeof(unsigned short)},
    {SQL_C_SLONG, INT32_MIN, INT32_MAX, sizeof(int32_t)},
    {SQL_C_LONG, INT32_MIN, INT32_MAX, sizeof(int32_t)},
    {SQL_C_ULONG, 0, UINT32_MAX, sizeof(uint32_t)},
    {SQL_C_SBIGINT, LLONG_MIN, LLONG_MAX, sizeof(int64_t)},
    {SQL_C_UBIGINT, 0, ULLONG_MAX, sizeof(uint64_t)},
};

/* Writes `v`, in range, as the integer C type of `b`. */
static void integer_put(const bounds *b, SQLPOINTER out, long long v, unsigned long long u) {
    bool is_unsigned = b->low == 0;
    if (b->bytes == sizeof(signed char)) {
        *(signed char *)out = is_unsigned ? (signed char)(unsigned char)u : (signed char)v;
    } else if (b->bytes == sizeof(short)) {
        *(short *)out = is_unsigned ? (short)(unsigned short)u : (short)v;
    } else if (b->bytes == sizeof(int32_t)) {
        *(int32_t *)out = is_unsigned ? (int32_t)(uint32_t)u : (int32_t)v;
    } else {
        *(int64_t *)out = is_unsigned ? (int64_t)u : (int64_t)v;
    }
}

/* The integer part of the number `n` as text into `whole`; false if an
   approximate one is past `b`'s range; `*dropped` whether a fraction was. */
static bool whole_text(const number *n, const bounds *b, char *whole, bool *dropped) {
    if (n->exact) {
        *dropped = whole_of(n->text, whole, TRL_NUMBER_BYTES);
        return true;
    }
    double truncated = trunc(n->approximate);
    *dropped = truncated != n->approximate;
    trl_format(whole, TRL_NUMBER_BYTES, "%.0f", truncated);
    return truncated >= (double)b->low && truncated <= (double)b->high && isfinite(truncated);
}

/* Reads `whole` as an integer of `b`'s type into `v` or `u`; whether it is in its range. */
static bool in_range(const char *whole, const bounds *b, long long *v, unsigned long long *u) {
    bool negative = whole[0] == '-' && strspn(whole + 1, "0") != strlen(whole + 1);
    errno = 0;
    if (b->low == 0) {
        *u = negative ? 0U : strtoull(whole, NULL, (int)TRL_DECIMAL_BASE);
        return !negative && errno != ERANGE && *u <= b->high;
    }
    *v = strtoll(whole, NULL, (int)TRL_DECIMAL_BASE);
    return errno != ERANGE && *v >= b->low && *v <= (long long)b->high;
}

/* An integer in range of `b` from the number `n`; 22003 out of it. */
static SQLRETURN integer_give(trl_odbc_diag *diag, const number *n, const bounds *b, const trl_odbc_target *target) {
    char whole[TRL_NUMBER_BYTES];
    bool dropped = false;
    long long v = 0;
    unsigned long long u = 0U;
    if (!whole_text(n, b, whole, &dropped) || !in_range(whole, b, &v, &u)) {
        return trl_odbc_fail(diag, "22003", "Numeric value out of range");
    }
    if (target->buffer != NULL) {
        integer_put(b, target->buffer, v, u);
    }
    if (target->length != NULL) {
        *target->length = (SQLLEN)b->bytes;
    }
    return dropped ? trl_odbc_warn(diag, "01S07", "Fractional truncation") : SQL_SUCCESS;
}

/* A float or a double from the number `n`. */
static SQLRETURN float_give(trl_odbc_diag *diag, const number *n, const trl_odbc_target *target) {
    double d = n->exact ? strtod(n->text, NULL) : n->approximate;
    bool single = target->c_type == SQL_C_FLOAT;
    if (single && isfinite(d) && fabs(d) > (double)FLT_MAX) {
        return trl_odbc_fail(diag, "22003", "Numeric value out of range");
    }
    if (target->buffer != NULL) {
        if (single) {
            *(float *)target->buffer = (float)d;
        } else {
            *(double *)target->buffer = d;
        }
    }
    if (target->length != NULL) {
        *target->length = single ? (SQLLEN)sizeof(float) : (SQLLEN)sizeof(double);
    }
    return SQL_SUCCESS;
}

/* A truth value, 0 or 1, from the number `n`: one between, its fraction dropped. */
static SQLRETURN bit_give(trl_odbc_diag *diag, const number *n, const trl_odbc_target *target) {
    double d = n->exact ? strtod(n->text, NULL) : n->approximate;
    if (d < 0.0 || d >= TRL_BIT_PAST) {
        return trl_odbc_fail(diag, "22003", "Numeric value out of range");
    }
    if (target->buffer != NULL) {
        *(unsigned char *)target->buffer = (unsigned char)(d >= 1.0 ? 1U : 0U);
    }
    if (target->length != NULL) {
        *target->length = 1;
    }
    return d != 0.0 && d != 1.0 ? trl_odbc_warn(diag, "01S07", "Fractional truncation") : SQL_SUCCESS;
}

/* Adds a decimal digit to SQL_C_NUMERIC's little-endian magnitude; false if it overflows. */
static bool magnitude_add(SQLCHAR *val, unsigned int digit) {
    unsigned int carry = digit;
    for (size_t i = 0U; i < TRL_NUMERIC_BYTES; ++i) {
        unsigned int next = (val[i] * TRL_DECIMAL_BASE) + carry;
        val[i] = (SQLCHAR)(next & TRL_BYTE_MASK);
        carry = next >> TRL_BYTE_BITS;
    }
    return carry == 0U;
}

/* The digits of `text`, a number, as SQL_C_NUMERIC's magnitude at `scale`
   into `val`, a fraction past it dropped; false if they overflow it. */
static bool magnitude_of(const char *text, SQLSMALLINT scale, SQLCHAR *val, bool *dropped) {
    const char *point = strchr(text, '.');
    SQLSMALLINT fraction = 0;
    bool fits = true;
    for (const char *at = text + (text[0] == '-' ? 1 : 0); *at != '\0' && fits; ++at) {
        bool after = point != NULL && at > point;
        if (at == point || (after && fraction >= scale)) {
            *dropped = *dropped || (at != point && *at != '0');
            continue;
        }
        fraction = after ? (SQLSMALLINT)(fraction + 1) : fraction;
        fits = magnitude_add(val, (unsigned int)(*at - '0'));
    }
    for (; fraction < scale && fits; ++fraction) {
        fits = magnitude_add(val, 0U);
    }
    return fits;
}

/* SQL_C_NUMERIC of the number `n`, at the target's scale. */
static SQLRETURN numeric_give(trl_odbc_diag *diag, const number *n, const trl_odbc_target *target) {
    char text[TRL_NUMBER_BYTES];
    if (n->exact) {
        trl_format(text, sizeof text, "%s", n->text);
    } else {
        trl_format(text, sizeof text, "%.*f", target->scale, n->approximate);
    }
    SQL_NUMERIC_STRUCT out;
    memset(&out, 0, sizeof out);
    out.sign = (SQLCHAR)(text[0] == '-' ? 0U : 1U);
    out.scale = (SQLSCHAR)target->scale;
    out.precision = (SQLCHAR)(target->precision > 0 ? target->precision : TRL_NUMERIC_MOST);
    bool dropped = false;
    if (!magnitude_of(text, target->scale, out.val, &dropped)) {
        return trl_odbc_fail(diag, "22003", "Numeric value out of range");
    }
    if (target->buffer != NULL) {
        memcpy(target->buffer, &out, sizeof out);
    }
    if (target->length != NULL) {
        *target->length = (SQLLEN)sizeof out;
    }
    return dropped ? trl_odbc_warn(diag, "01S07", "Fractional truncation") : SQL_SUCCESS;
}

SQLRETURN trl_odbc_give_scalar(trl_odbc_diag *diag, const trl_odbc_piece *piece, const trl_odbc_target *target) {
    SQLSMALLINT c_type = target->c_type;
    bool datetime = c_type == SQL_C_TYPE_DATE || c_type == SQL_C_TYPE_TIME || c_type == SQL_C_TYPE_TIMESTAMP ||
                    c_type == SQL_C_DATE || c_type == SQL_C_TIME || c_type == SQL_C_TIMESTAMP;
    if (datetime) {
        return trl_odbc_give_datetime(diag, piece, target);
    }
    if (c_type == SQL_C_GUID) {
        return trl_odbc_give_guid(diag, piece, target);
    }
    number n;
    SQLRETURN read = number_of(diag, piece, &n);
    if (read != SQL_SUCCESS) {
        return read;
    }
    for (size_t i = 0U; i < sizeof integers / sizeof integers[0]; ++i) {
        if (integers[i].c_type == c_type) {
            return integer_give(diag, &n, &integers[i], target);
        }
    }
    switch (c_type) {
    case SQL_C_FLOAT:
    case SQL_C_DOUBLE:
        return float_give(diag, &n, target);
    case SQL_C_BIT:
        return bit_give(diag, &n, target);
    case SQL_C_NUMERIC:
        return numeric_give(diag, &n, target);
    default:
        return trl_odbc_fail(diag, "07006", "Restricted data type attribute violation");
    }
}
