/**
 * @file text.c
 * @brief A value written as turborel displays it (`value.rs` `Display`):
 * a string as itself, a number as turborel writes it, a datetime and an
 * interval as their literals' strings, a binary string in hexadecimal, a
 * large object's text or what it is, and an array, row, multiset,
 * structured value or reference as its literal, each element as its own.
 */
#include "data/text.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/int128.h"
#include "codec/json.h"
#include "codec/numerals.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "turborel/turborel.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Microseconds of a second, a minute, an hour and a day. */
#define TRL_MICROS_SECOND 1000000LL
#define TRL_MICROS_MINUTE (60LL * TRL_MICROS_SECOND)
#define TRL_MICROS_HOUR (60LL * TRL_MICROS_MINUTE)
#define TRL_MICROS_DAY (24LL * TRL_MICROS_HOUR)
#define TRL_MINUTES_HOUR 60
/* The days from 0000-03-01 to 0001-01-01, and of a 400-year era; the
   divisors of the civil calendar (`datetime.rs` `civil_from_days`). */
#define TRL_DAYS_TO_YEAR_ONE 306LL
#define TRL_DAYS_ERA 146097LL
#define TRL_YEARS_ERA 400LL
#define TRL_DAYS_FOUR_YEARS 1460LL
#define TRL_DAYS_CENTURY 36524LL
#define TRL_DAYS_ERA_LAST 146096LL
#define TRL_DAYS_YEAR 365LL
#define TRL_LEAP 4LL
#define TRL_CENTURY 100LL
#define TRL_MONTH_DAYS_FIVE 153LL
#define TRL_MONTH_FIVE 5LL
#define TRL_MARCH 3LL
#define TRL_TO_JANUARY 9LL
#define TRL_FEBRUARY 2LL
#define TRL_MONTHS_TO_MARCH 10LL
/* The most fraction digits, of microseconds. */
#define TRL_FRACTION_DIGITS 6U
/* The digits of a year, and of each other field of a date or time. */
#define TRL_YEAR_DIGITS 4U
#define TRL_FIELD_DIGITS 2U
/* An interval's fields, by rank, and the months of a year. */
#define TRL_FIELD_YEAR 0U
#define TRL_FIELD_MONTH 1U
#define TRL_FIELD_DAY 2U
#define TRL_FIELD_HOUR 3U
#define TRL_FIELD_MINUTE 4U
#define TRL_FIELD_SECOND 5U
#define TRL_MONTHS_YEAR 12U
/* Room for a number written by snprintf. */
#define TRL_NUMBER_BYTES 48U
#define TRL_TEN 10U
/* The hexadecimal digits, and a nibble's bits. */
#define TRL_NIBBLE_BITS 4U
#define TRL_NIBBLE_MASK 0x0FU
/* A UUID's octets, and the digits before each hyphen of its text. */
#define TRL_UUID_OCTETS 16U

static void put_literal(trl_buffer *out, const trl_value *value);

/* Appends a null-ended string. */
static void put_text(trl_buffer *out, const char *text) {
    trl_put_raw(out, (const uint8_t *)text, strlen(text));
}

/* Appends `number` in decimal, zeros before it to `width` digits. */
static void put_digits(trl_buffer *out, long long number, unsigned int width) {
    char digits[TRL_NUMBER_BYTES];
    unsigned long long size = number < 0 ? 0ULL - (unsigned long long)number : (unsigned long long)number;
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + (int)(size % TRL_TEN));
        size /= TRL_TEN;
    } while (size > 0U && count < sizeof digits);
    if (number < 0) {
        trl_put_u8(out, (uint8_t)'-');
    }
    for (size_t pad = count; pad < width; ++pad) {
        trl_put_u8(out, (uint8_t)'0');
    }
    while (count > 0U) {
        trl_put_u8(out, (uint8_t)digits[--count]);
    }
}

/* Appends `before`, then `number` as put_digits() writes it. */
static void put_number(trl_buffer *out, char before, long long number, unsigned int width) {
    if (before != '\0') {
        trl_put_u8(out, (uint8_t)before);
    }
    put_digits(out, number, width);
}

/* Floor division and remainder, as turborel's `floor_div` and `floor_rem`. */
static long long floor_div(long long a, long long b) {
    long long q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static long long floor_rem(long long a, long long b) {
    return a - (floor_div(a, b) * b);
}

/* Appends `bytes` in upper-case hexadecimal. */
static void put_hex(trl_buffer *out, const uint8_t *bytes, size_t length) {
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0U; i < length; ++i) {
        trl_put_u8(out, (uint8_t)digits[bytes[i] >> TRL_NIBBLE_BITS]);
        trl_put_u8(out, (uint8_t)digits[bytes[i] & TRL_NIBBLE_MASK]);
    }
}

/* Appends a date of `days` from 0001-01-01, as YYYY-MM-DD. */
static void put_date(trl_buffer *out, long long days) {
    long long z = days + TRL_DAYS_TO_YEAR_ONE;
    long long era = floor_div(z, TRL_DAYS_ERA);
    long long doe = floor_rem(z, TRL_DAYS_ERA);
    long long yoe = floor_div(doe - floor_div(doe, TRL_DAYS_FOUR_YEARS) + floor_div(doe, TRL_DAYS_CENTURY) -
                                  floor_div(doe, TRL_DAYS_ERA_LAST),
                              TRL_DAYS_YEAR);
    long long doy = doe - ((yoe * TRL_DAYS_YEAR) + floor_div(yoe, TRL_LEAP) - floor_div(yoe, TRL_CENTURY));
    long long mp = floor_div((doy * TRL_MONTH_FIVE) + TRL_FEBRUARY, TRL_MONTH_DAYS_FIVE);
    long long day = doy - floor_div((mp * TRL_MONTH_DAYS_FIVE) + TRL_FEBRUARY, TRL_MONTH_FIVE) + 1;
    long long month = mp < TRL_MONTHS_TO_MARCH ? mp + TRL_MARCH : mp - TRL_TO_JANUARY;
    long long year = (era * TRL_YEARS_ERA) + yoe + (month <= TRL_FEBRUARY ? 1 : 0);
    put_number(out, '\0', year, TRL_YEAR_DIGITS);
    put_number(out, '-', month, TRL_FIELD_DIGITS);
    put_number(out, '-', day, TRL_FIELD_DIGITS);
}

/* Appends `micros` of a second's fraction to `precision` digits. */
static void put_fraction(trl_buffer *out, long long micros, unsigned int precision) {
    trl_buffer six;
    trl_buffer_init(&six);
    put_digits(&six, micros, TRL_FRACTION_DIGITS);
    trl_put_u8(out, (uint8_t)'.');
    size_t kept = precision < TRL_FRACTION_DIGITS ? precision : TRL_FRACTION_DIGITS;
    trl_put_raw(out, six.data, six.length < kept ? six.length : kept);
    trl_buffer_free(&six);
}

/* Appends a zone's displacement, as +HH:MM. */
static void put_zone(trl_buffer *out, int zone) {
    int size = zone < 0 ? -zone : zone;
    trl_put_u8(out, (uint8_t)(zone < 0 ? '-' : '+'));
    put_number(out, '\0', size / TRL_MINUTES_HOUR, TRL_FIELD_DIGITS);
    put_number(out, ':', size % TRL_MINUTES_HOUR, TRL_FIELD_DIGITS);
}

/* Appends a datetime as turborel shows it: its date, its time, its
   fraction to its precision, its zone; in its zone's local time. */
static void put_datetime(trl_buffer *out, const trl_value *value) {
    const trl_datetime *d = &value->as.datetime;
    long long local = d->utc + ((long long)d->zone * TRL_MICROS_MINUTE);
    bool time_only = value->tag == TRL_TAG_TIME || value->tag == TRL_TAG_TIME_ZONED;
    if (time_only) {
        local = floor_rem(local, TRL_MICROS_DAY);
    }
    if (!time_only) {
        put_date(out, floor_div(local, TRL_MICROS_DAY));
    }
    if (value->tag == TRL_TAG_DATE) {
        return;
    }
    if (!time_only) {
        trl_put_u8(out, (uint8_t)' ');
    }
    long long time = floor_rem(local, TRL_MICROS_DAY);
    long long seconds = floor_div(time, TRL_MICROS_SECOND);
    put_number(out, '\0', seconds / (TRL_MICROS_HOUR / TRL_MICROS_SECOND), TRL_FIELD_DIGITS);
    put_number(out, ':', (seconds / (TRL_MICROS_MINUTE / TRL_MICROS_SECOND)) % TRL_MINUTES_HOUR, TRL_FIELD_DIGITS);
    put_number(out, ':', seconds % TRL_MINUTES_HOUR, TRL_FIELD_DIGITS);
    if (d->precision > 0U) {
        put_fraction(out, floor_rem(time, TRL_MICROS_SECOND), d->precision);
    }
    if (d->zoned) {
        put_zone(out, d->zone);
    }
}

/* The units of an interval's field `rank`: months of YEAR and MONTH, else
   microseconds. */
static uint64_t unit_of(unsigned int rank) {
    static const uint64_t units[] = {
        TRL_MONTHS_YEAR,
        1U,
        (uint64_t)TRL_MICROS_DAY,
        (uint64_t)TRL_MICROS_HOUR,
        (uint64_t)TRL_MICROS_MINUTE,
        (uint64_t)TRL_MICROS_SECOND,
    };
    return units[rank];
}

/* The quotient of `value` by `divisor`. */
static trl_uint128 quotient_of(trl_uint128 value, uint64_t divisor) {
    trl_uint128 q = value;
    (void)trl_uint128_divide(&q, divisor);
    return q;
}

/* The remainder of `value` by `divisor`. */
static trl_uint128 remainder_of(trl_uint128 value, uint64_t divisor) {
    trl_uint128 q = value;
    trl_uint128 r = {trl_uint128_divide(&q, divisor), 0U};
    return r;
}

/* The separator before an interval's field `rank`, not the leading one. */
static char separator_of(unsigned int rank) {
    if (rank == TRL_FIELD_MONTH) {
        return '-';
    }
    return rank == TRL_FIELD_HOUR ? ' ' : ':';
}

/* Appends the field of `rank` of an interval of `size` units, as
   put_interval() writes each. */
static void put_interval_field(trl_buffer *out, const trl_interval_qualifier *q, unsigned int rank, trl_uint128 size) {
    trl_uint128 above = rank == q->start ? size : remainder_of(size, unit_of(rank - 1U));
    trl_uint128 n = rank == TRL_FIELD_SECOND ? above : quotient_of(above, unit_of(rank));
    trl_uint128 whole = rank == TRL_FIELD_SECOND ? quotient_of(n, (uint64_t)TRL_MICROS_SECOND) : n;
    char digits[TRL_U128_DIGITS];
    size_t count = trl_uint128_to_digits(whole, digits);
    if (rank != q->start) {
        trl_put_u8(out, (uint8_t)separator_of(rank));
        if (count < TRL_FIELD_DIGITS) {
            trl_put_u8(out, (uint8_t)'0');
        }
    }
    trl_put_raw(out, (const uint8_t *)digits, count);
    if (rank == TRL_FIELD_SECOND && q->fraction > 0U) {
        put_fraction(out, (long long)remainder_of(n, (uint64_t)TRL_MICROS_SECOND).low, q->fraction);
    }
}

/* Appends an interval as its literal's string: its sign if negative, its
   leading field's digits, each other field two after its separator, the
   second's fraction to its precision (`interval.rs`). */
static void put_interval(trl_buffer *out, const trl_interval *interval) {
    const trl_interval_qualifier *q = &interval->qualifier;
    bool negative = trl_int128_negative(interval->units);
    trl_uint128 size = trl_int128_magnitude(interval->units);
    if (negative) {
        trl_put_u8(out, (uint8_t)'-');
    }
    for (unsigned int rank = q->start; rank <= q->end && rank <= TRL_FIELD_SECOND; ++rank) {
        put_interval_field(out, q, rank, size);
    }
}

/* Appends a UUID's text: 8-4-4-4-12 lower-case hexadecimal digits. */
static void put_uuid(trl_buffer *out, const trl_value *value) {
    char text[TRL_UUID_TEXT_BYTES];
    uint8_t octets[TRL_UUID_OCTETS];
    if (trl_value_uuid(value, octets) == TRL_OK && trl_uuid_text(octets, text) == TRL_OK) {
        put_text(out, text);
    }
}

/* Appends a large object: its text or octets if whole, else what it is. */
static void put_lob(trl_buffer *out, const trl_value *value) {
    if (value->tag == TRL_TAG_LOB_LOCATOR) {
        bool character = value->as.locator.kind == TRL_LOB_CHARACTER;
        put_text(out, character ? "<CHARACTER LARGE OBJECT of " : "<BINARY LARGE OBJECT of ");
        put_number(out, '\0', (long long)(character ? value->as.locator.characters : value->as.locator.octets), 1U);
        put_text(out, character ? " characters>" : " octets>");
        return;
    }
    if (value->as.lob.kind == TRL_LOB_CHARACTER) {
        trl_put_raw(out, value->as.lob.octets.bytes, value->as.lob.octets.length);
    } else {
        put_hex(out, value->as.lob.octets.bytes, value->as.lob.octets.length);
    }
}

/* Appends a JSON value's text, or what keeps it from having one. */
static void put_json(trl_buffer *out, const trl_value *value) {
    trl_codec_error error = {0, "", ""};
    size_t before = out->length;
    if (!trl_json_to_text(out, value->as.bytes.bytes, value->as.bytes.length, &error)) {
        out->length = before;
        trl_put_u8(out, (uint8_t)'<');
        put_text(out, error.message);
        trl_put_u8(out, (uint8_t)'>');
    }
}

/* Appends each of `count` values' literals, separated by ", ". */
static void put_literals(trl_buffer *out, const trl_value *values, uint32_t count) {
    for (uint32_t i = 0U; i < count; ++i) {
        if (i > 0U) {
            put_text(out, ", ");
        }
        put_literal(out, &values[i]);
    }
}

/* Appends `text` as a quoted string, a quote in it written twice. */
static void put_quoted(trl_buffer *out, const uint8_t *text, size_t length) {
    trl_put_u8(out, (uint8_t)'\'');
    for (size_t i = 0U; i < length; ++i) {
        trl_put_u8(out, text[i]);
        if (text[i] == '\'') {
            trl_put_u8(out, (uint8_t)'\'');
        }
    }
    trl_put_u8(out, (uint8_t)'\'');
}

/* Appends what `value` displays as, quoted as a string literal. */
static void put_quoted_text(trl_buffer *out, const trl_value *value) {
    trl_buffer text;
    trl_buffer_init(&text);
    trl_value_put_text(&text, value);
    put_quoted(out, text.data, text.length);
    trl_buffer_free(&text);
}

/* Appends a composite value's literal: an array's, a row's, a multiset's,
   a structured value's or a reference's. */
static void put_composite(trl_buffer *out, const trl_value *value) {
    switch (value->tag) {
    case TRL_TAG_ARRAY:
    case TRL_TAG_MULTISET:
        put_text(out, value->tag == TRL_TAG_ARRAY ? "ARRAY[" : "MULTISET[");
        put_literals(out, value->as.list.items, value->as.list.count);
        trl_put_u8(out, (uint8_t)']');
        break;
    case TRL_TAG_ROW:
        put_text(out, "ROW(");
        put_literals(out, value->as.list.items, value->as.list.count);
        trl_put_u8(out, (uint8_t)')');
        break;
    case TRL_TAG_STRUCTURED:
        trl_put_raw(out, value->as.structured.type.schema.bytes, value->as.structured.type.schema.length);
        trl_put_u8(out, (uint8_t)'.');
        trl_put_raw(out, value->as.structured.type.name.bytes, value->as.structured.type.name.length);
        trl_put_u8(out, (uint8_t)'(');
        put_literals(out, value->as.structured.attributes, value->as.structured.count);
        trl_put_u8(out, (uint8_t)')');
        break;
    default:
        put_text(out, "REF(");
        put_hex(out, value->as.bytes.bytes, value->as.bytes.length);
        trl_put_u8(out, (uint8_t)')');
        break;
    }
}

/* Appends the literal of a datetime or an interval. */
static void put_moment_literal(trl_buffer *out, const trl_value *value) {
    if (value->tag != TRL_TAG_INTERVAL) {
        static const char *const words[] = {"DATE ", "TIME ", "TIME ", "TIMESTAMP ", "TIMESTAMP "};
        put_text(out, words[value->tag - TRL_TAG_DATE]);
        put_quoted_text(out, value);
        return;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    put_interval(&text, &value->as.interval);
    bool negative = text.length > 0U && text.data[0] == '-';
    put_text(out, negative ? "INTERVAL -" : "INTERVAL ");
    put_quoted(out, text.data + (negative ? 1U : 0U), text.length - (negative ? 1U : 0U));
    trl_buffer_free(&text);
    trl_put_u8(out, (uint8_t)' ');
    trl_value_put_qualifier(out, &value->as.interval.qualifier);
}

/* Appends a string's literal: quoted, or of octets X'..'. */
static void put_string_literal(trl_buffer *out, const trl_value *value) {
    if (value->tag == TRL_TAG_CHARACTER || value->tag == TRL_TAG_VARYING) {
        put_quoted(out, value->as.bytes.bytes, value->as.bytes.length);
    } else if (value->tag == TRL_TAG_LOB_WHOLE && value->as.lob.kind == TRL_LOB_CHARACTER) {
        put_quoted(out, value->as.lob.octets.bytes, value->as.lob.octets.length);
    } else if (value->tag == TRL_TAG_LOB_WHOLE) {
        put_text(out, "X'");
        put_lob(out, value);
        trl_put_u8(out, (uint8_t)'\'');
    } else {
        put_text(out, "X'");
        put_hex(out, value->as.bytes.bytes, value->as.bytes.length);
        trl_put_u8(out, (uint8_t)'\'');
    }
}

/* Appends a value's literal, as its kind writes one (each kind's
   `literal` in `value/types`). */
static void put_literal(trl_buffer *out, const trl_value *value) {
    switch (value->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
    case TRL_TAG_LOB_WHOLE:
        put_string_literal(out, value);
        break;
    case TRL_TAG_DECFLOAT:
        put_text(out, "CAST(");
        put_quoted_text(out, value);
        put_text(out, " AS DECFLOAT)");
        break;
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIMESTAMP_ZONED:
    case TRL_TAG_INTERVAL:
        put_moment_literal(out, value);
        break;
    case TRL_TAG_JSON:
        put_text(out, "JSON(");
        put_quoted_text(out, value);
        trl_put_u8(out, (uint8_t)')');
        break;
    case TRL_TAG_XML:
        put_text(out, "XMLPARSE(CONTENT ");
        put_quoted(out, value->as.bytes.bytes, value->as.bytes.length);
        put_text(out, " PRESERVE WHITESPACE)");
        break;
    case TRL_TAG_UUID:
        put_text(out, "UUID ");
        put_quoted_text(out, value);
        break;
    default:
        trl_value_put_text(out, value);
        break;
    }
}

void trl_value_put_qualifier(trl_buffer *out, const trl_interval_qualifier *q) {
    static const char *const names[] = {"YEAR", "MONTH", "DAY", "HOUR", "MINUTE", "SECOND"};
    put_text(out, names[q->start <= TRL_FIELD_SECOND ? q->start : TRL_FIELD_SECOND]);
    if (q->start == q->end && q->end == TRL_FIELD_SECOND) {
        put_number(out, '(', (long long)q->leading, 1U);
        put_text(out, ", ");
        put_number(out, '\0', (long long)q->fraction, 1U);
        trl_put_u8(out, (uint8_t)')');
        return;
    }
    put_number(out, '(', (long long)q->leading, 1U);
    trl_put_u8(out, (uint8_t)')');
    if (q->start == q->end) {
        return;
    }
    put_text(out, " TO ");
    put_text(out, names[q->end <= TRL_FIELD_SECOND ? q->end : TRL_FIELD_SECOND]);
    if (q->end == TRL_FIELD_SECOND) {
        put_number(out, '(', (long long)q->fraction, 1U);
        trl_put_u8(out, (uint8_t)')');
    }
}

/* Appends a scalar value of a number's or a truth value's kind. */
static void put_scalar(trl_buffer *out, const trl_value *value) {
    char text[TRL_DECIMAL_TEXT_BYTES];
    switch (value->tag) {
    case TRL_TAG_EXACT:
        if (trl_decimal_text(&value->as.exact, text, sizeof text) == TRL_OK) {
            put_text(out, text);
        }
        break;
    case TRL_TAG_APPROXIMATE:
        trl_put_approximate_text(out, value->as.approximate);
        break;
    case TRL_TAG_DECFLOAT:
        trl_put_decfloat_text(out, value->as.decfloat);
        break;
    default:
        put_text(out, value->as.truth ? "TRUE" : "FALSE");
        break;
    }
}

void trl_value_put_text(trl_buffer *out, const trl_value *value) {
    switch (value->tag) {
    case TRL_TAG_NULL:
        put_text(out, "NULL");
        break;
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_XML:
        trl_put_raw(out, value->as.bytes.bytes, value->as.bytes.length);
        break;
    case TRL_TAG_PLUGIN:
        trl_put_raw(out, value->as.plugin.text.bytes, value->as.plugin.text.length);
        break;
    case TRL_TAG_EXACT:
    case TRL_TAG_APPROXIMATE:
    case TRL_TAG_DECFLOAT:
    case TRL_TAG_BOOLEAN:
        put_scalar(out, value);
        break;
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
        put_hex(out, value->as.bytes.bytes, value->as.bytes.length);
        break;
    case TRL_TAG_LOB_WHOLE:
    case TRL_TAG_LOB_LOCATOR:
        put_lob(out, value);
        break;
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIMESTAMP_ZONED:
        put_datetime(out, value);
        break;
    case TRL_TAG_INTERVAL:
        put_interval(out, &value->as.interval);
        break;
    case TRL_TAG_JSON:
        put_json(out, value);
        break;
    case TRL_TAG_UUID:
        put_uuid(out, value);
        break;
    default:
        put_composite(out, value);
        break;
    }
}

trl_status trl_value_to_text(const trl_value *value, char *out, size_t capacity, size_t *length) {
    if (value == NULL || (out == NULL && capacity > 0U)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    trl_value_put_text(&text, value);
    trl_status status = trl_text_give(&text, out, capacity, length);
    trl_buffer_free(&text);
    return status;
}

trl_status trl_text_give(const trl_buffer *text, char *out, size_t capacity, size_t *length) {
    if (text->state != TRL_BUFFER_OK) {
        return TRL_NO_MEMORY;
    }
    if (length != NULL) {
        *length = text->length;
    }
    if (capacity > 0U) {
        size_t kept = text->length < capacity - 1U ? text->length : capacity - 1U;
        if (kept > 0U) {
            memcpy(out, text->data, kept);
        }
        out[kept] = '\0';
    }
    return TRL_OK;
}
