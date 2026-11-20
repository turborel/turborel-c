/**
 * @file convert_datetime.c
 * @brief A datetime given as ODBC's date, time or timestamp structure,
 * and a UUID as SQLGUID (RULES.md D2), as ODBC's reference's Appendix D
 * converts them: a datetime's fields read from its text, in its own
 * zone, or from a character string's; a date of a timestamp its time
 * dropped (01S07), a time of a date refused (07006).
 */
#include "odbc/convert.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The digits of nanoseconds, ODBC's fraction of a second, and the radix. */
#define TRL_NANO_DIGITS 9U
#define TRL_DECIMAL_BASE 10
/* The octets of a UUID's parts as SQLGUID holds them, and a byte's bits. */
#define TRL_GUID_DATA1 4U
#define TRL_GUID_DATA2 2U
#define TRL_GUID_DATA4 8U
#define TRL_BYTE_BITS 8U

/* A date's or a time's three parts, and where each is. */
enum datetime_part {
    TRL_FIRST_PART = 0,
    TRL_SECOND_PART = 1,
    TRL_THIRD_PART = 2,
    TRL_DATETIME_PARTS = 3,
};

/* A datetime's fields, as its text gives them. */
typedef struct fields {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    unsigned long nanos;
    bool date;
    bool time;
} fields;

/* Reads a fraction of a second's digits as nanoseconds. */
static unsigned long nanos_of(const char *digits) {
    unsigned long nanos = 0U;
    size_t count = 0U;
    for (; digits[count] >= '0' && digits[count] <= '9' && count < TRL_NANO_DIGITS; ++count) {
        nanos = (nanos * (unsigned long)TRL_DECIMAL_BASE) + (unsigned long)(digits[count] - '0');
    }
    for (; count < TRL_NANO_DIGITS; ++count) {
        nanos *= (unsigned long)TRL_DECIMAL_BASE;
    }
    return nanos;
}

/* Reads a number's digits at `*at`, a sign before them if `with_sign`; whether there was one. */
static bool number_read(const char **at, int *out, bool with_sign) {
    bool negative = with_sign && **at == '-';
    *at += negative ? 1 : 0;
    if (**at < '0' || **at > '9') {
        return false;
    }
    int value = 0;
    for (; **at >= '0' && **at <= '9'; ++*at) {
        value = (value * TRL_DECIMAL_BASE) + (**at - '0');
    }
    *out = negative ? -value : value;
    return true;
}

/* Reads `between`, then a number, at `*at`; whether they were there. */
static bool next_read(const char **at, char between, int *out) {
    if (**at != between) {
        return false;
    }
    ++*at;
    return number_read(at, out, false);
}

/* Reads three numbers at `*at`, between them `between`; whether they were there. */
static bool three_read(const char **at, char between, int *parts, bool with_sign) {
    const char *from = *at;
    bool read = number_read(at, &parts[TRL_FIRST_PART], with_sign) && next_read(at, between, &parts[TRL_SECOND_PART]) &&
                next_read(at, between, &parts[TRL_THIRD_PART]);
    if (!read) {
        *at = from;
    }
    return read;
}

/* Reads a date, a time or a timestamp's text; false if it is none. */
static bool fields_of(const char *text, fields *out) {
    memset(out, 0, sizeof *out);
    const char *at = text;
    while (*at == ' ') {
        ++at;
    }
    int parts[TRL_DATETIME_PARTS] = {0, 0, 0};
    if (three_read(&at, '-', parts, true)) {
        out->date = true;
        out->year = parts[TRL_FIRST_PART];
        out->month = parts[TRL_SECOND_PART];
        out->day = parts[TRL_THIRD_PART];
        while (*at == ' ' || *at == 'T') {
            ++at;
        }
    }
    if (three_read(&at, ':', parts, false)) {
        out->time = true;
        out->hour = parts[TRL_FIRST_PART];
        out->minute = parts[TRL_SECOND_PART];
        out->second = parts[TRL_THIRD_PART];
        out->nanos = *at == '.' ? nanos_of(at + 1) : 0U;
    }
    return out->date || out->time;
}

/* Gives `bytes` of `structure` to the target. */
static void structure_put(const trl_odbc_target *target, const void *structure, size_t bytes) {
    if (target->buffer != NULL) {
        memcpy(target->buffer, structure, bytes);
    }
    if (target->length != NULL) {
        *target->length = (SQLLEN)bytes;
    }
}

/* Gives the fields as the structure the target's C type is. */
static void fields_put(const fields *f, bool date, bool time, const trl_odbc_target *target) {
    if (date) {
        DATE_STRUCT d = {(SQLSMALLINT)f->year, (SQLUSMALLINT)f->month, (SQLUSMALLINT)f->day};
        structure_put(target, &d, sizeof d);
    } else if (time) {
        TIME_STRUCT t = {(SQLUSMALLINT)f->hour, (SQLUSMALLINT)f->minute, (SQLUSMALLINT)f->second};
        structure_put(target, &t, sizeof t);
    } else {
        TIMESTAMP_STRUCT ts = {
            (SQLSMALLINT)f->year,    (SQLUSMALLINT)f->month,  (SQLUSMALLINT)f->day,  (SQLUSMALLINT)f->hour,
            (SQLUSMALLINT)f->minute, (SQLUSMALLINT)f->second, (SQLUINTEGER)f->nanos,
        };
        structure_put(target, &ts, sizeof ts);
    }
}

SQLRETURN trl_odbc_give_datetime(trl_odbc_diag *diag, const trl_odbc_piece *piece, const trl_odbc_target *target) {
    fields f;
    if (piece->text == NULL || !fields_of(piece->text, &f)) {
        return trl_odbc_fail(diag, "22018", "Invalid character value for cast specification");
    }
    bool date = target->c_type == SQL_C_TYPE_DATE || target->c_type == SQL_C_DATE;
    bool time = target->c_type == SQL_C_TYPE_TIME || target->c_type == SQL_C_TIME;
    if ((date && !f.date) || (time && !f.time)) {
        return trl_odbc_fail(diag, "07006", "Restricted data type attribute violation");
    }
    bool time_dropped = date && (f.hour != 0 || f.minute != 0 || f.second != 0 || f.nanos != 0U);
    bool fraction_dropped = time && f.nanos != 0U;
    fields_put(&f, date, time, target);
    return time_dropped || fraction_dropped ? trl_odbc_warn(diag, "01S07", "Fractional truncation") : SQL_SUCCESS;
}

/* The big-endian number of `count` octets at `octets`. */
static unsigned long big_endian(const uint8_t *octets, size_t count) {
    unsigned long n = 0U;
    for (size_t i = 0U; i < count; ++i) {
        n = (n << TRL_BYTE_BITS) | (unsigned long)octets[i];
    }
    return n;
}

SQLRETURN trl_odbc_give_guid(trl_odbc_diag *diag, const trl_odbc_piece *piece, const trl_odbc_target *target) {
    uint8_t octets[TRL_UUID_BYTES];
    bool read = trl_value_uuid(piece->value, octets) == TRL_OK ||
                (piece->text != NULL && trl_uuid_parse(piece->text, octets) == TRL_OK);
    if (!read) {
        return trl_odbc_fail(diag, "22018", "Invalid character value for cast specification");
    }
    SQLGUID guid;
    size_t third = TRL_GUID_DATA1 + TRL_GUID_DATA2;
    guid.Data1 = (unsigned int)big_endian(octets, TRL_GUID_DATA1);
    guid.Data2 = (unsigned short)big_endian(octets + TRL_GUID_DATA1, TRL_GUID_DATA2);
    guid.Data3 = (unsigned short)big_endian(octets + third, TRL_GUID_DATA2);
    memcpy(guid.Data4, octets + TRL_UUID_BYTES - TRL_GUID_DATA4, TRL_GUID_DATA4);
    structure_put(target, &guid, sizeof guid);
    return SQL_SUCCESS;
}
