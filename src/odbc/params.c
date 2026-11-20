/**
 * @file params.c
 * @brief The parameters an application bound, as the values sent for a
 * statement's `?` marks (RULES.md D2): each by its C type, a character
 * string's text, which the server assigns to the parameter's type; an
 * integer or SQL_C_NUMERIC an exact number; a float an approximate one; a
 * truth value; a date, a time or a timestamp structure its literal's
 * text; octets a binary string; SQLGUID a UUID. One set of parameters is
 * sent at a time.
 */
#include "odbc/params.h"

#include "format.h"
#include "odbc/convert.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Room for a datetime's literal text, and SQLGUID's parts. */
#define TRL_DATETIME_BYTES 48U
#define TRL_NANO_DIGITS 9U
#define TRL_BYTE_BITS 8U
#define TRL_BYTE_MASK 0xFFU
#define TRL_HALF_BYTES 8U
#define TRL_GUID_DATA1 4U
#define TRL_GUID_DATA2 2U
#define TRL_GUID_DATA4 8U
#define TRL_DECIMAL_BASE 10U

/* A bound parameter's place: its value, length and indicator, offset as bound. */
typedef struct bound {
    SQLPOINTER data;
    SQLLEN *length;
    SQLLEN *indicator;
    SQLLEN octet_length;
    SQLSMALLINT c_type;
} bound;

/* The address of a bound pointer, offset by the descriptor's bind offset. */
static void *offset(void *at, const SQLLEN *by) {
    return at == NULL || by == NULL ? at : (char *)at + *by;
}

/* An exact number of a signed integer. */
static trl_status signed_add(trl_params *params, int64_t v) {
    trl_decimal d = {{(uint64_t)v, v < 0 ? -1 : 0}, 0U};
    return trl_params_add_decimal(params, d);
}

/* An exact number of an unsigned integer. */
static trl_status unsigned_add(trl_params *params, uint64_t v) {
    trl_decimal d = {{v, 0}, 0U};
    return trl_params_add_decimal(params, d);
}

/* An integer C type's value; whether `c_type` is one. */
static bool integer_of(const bound *b, trl_params *params, trl_status *added) {
    const void *p = b->data;
    switch (b->c_type) {
    case SQL_C_STINYINT:
    case SQL_C_TINYINT:
        *added = signed_add(params, *(const signed char *)p);
        return true;
    case SQL_C_UTINYINT:
        *added = unsigned_add(params, *(const unsigned char *)p);
        return true;
    case SQL_C_SSHORT:
    case SQL_C_SHORT:
        *added = signed_add(params, *(const short *)p);
        return true;
    case SQL_C_USHORT:
        *added = unsigned_add(params, *(const unsigned short *)p);
        return true;
    case SQL_C_SLONG:
    case SQL_C_LONG:
        *added = signed_add(params, *(const int32_t *)p);
        return true;
    case SQL_C_ULONG:
        *added = unsigned_add(params, *(const uint32_t *)p);
        return true;
    case SQL_C_SBIGINT:
        *added = signed_add(params, *(const int64_t *)p);
        return true;
    case SQL_C_UBIGINT:
        *added = unsigned_add(params, *(const uint64_t *)p);
        return true;
    default:
        return false;
    }
}

/* SQL_C_NUMERIC's value: its little-endian magnitude, sign and scale. */
static trl_status numeric_add(trl_params *params, const SQL_NUMERIC_STRUCT *n) {
    uint64_t low = 0U;
    uint64_t high = 0U;
    for (size_t i = TRL_HALF_BYTES; i-- > 0U;) {
        low = (low << TRL_BYTE_BITS) | n->val[i];
        high = (high << TRL_BYTE_BITS) | n->val[i + TRL_HALF_BYTES];
    }
    if (n->sign == 0U) {
        low = ~low + 1U;
        high = ~high + (low == 0U ? 1U : 0U);
    }
    trl_decimal d = {{low, (int64_t)high}, n->scale > 0 ? (uint32_t)n->scale : 0U};
    return trl_params_add_decimal(params, d);
}

/* A date, time or timestamp structure's literal text; whether `c_type` is one of them. */
static bool datetime_of(const bound *b, char *text, size_t capacity) {
    if (b->c_type == SQL_C_TYPE_DATE || b->c_type == SQL_C_DATE) {
        const DATE_STRUCT *d = b->data;
        trl_format(text, capacity, "%04d-%02u-%02u", (int)d->year, (unsigned)d->month, (unsigned)d->day);
        return true;
    }
    if (b->c_type == SQL_C_TYPE_TIME || b->c_type == SQL_C_TIME) {
        const TIME_STRUCT *t = b->data;
        trl_format(text, capacity, "%02u:%02u:%02u", (unsigned)t->hour, (unsigned)t->minute, (unsigned)t->second);
        return true;
    }
    if (b->c_type == SQL_C_TYPE_TIMESTAMP || b->c_type == SQL_C_TIMESTAMP) {
        const TIMESTAMP_STRUCT *ts = b->data;
        trl_format(text, capacity, "%04d-%02u-%02u %02u:%02u:%02u.%09lu", (int)ts->year, (unsigned)ts->month,
                   (unsigned)ts->day, (unsigned)ts->hour, (unsigned)ts->minute, (unsigned)ts->second,
                   (unsigned long)ts->fraction);
        return true;
    }
    return false;
}

/* SQLGUID's value, as a UUID's octets. */
static trl_status guid_add(trl_params *params, const SQLGUID *guid) {
    uint8_t octets[TRL_UUID_BYTES];
    for (size_t i = 0U; i < TRL_GUID_DATA1; ++i) {
        octets[i] = (uint8_t)((guid->Data1 >> ((TRL_GUID_DATA1 - 1U - i) * TRL_BYTE_BITS)) & TRL_BYTE_MASK);
    }
    octets[TRL_GUID_DATA1] = (uint8_t)(guid->Data2 >> TRL_BYTE_BITS);
    octets[TRL_GUID_DATA1 + 1U] = (uint8_t)(guid->Data2 & TRL_BYTE_MASK);
    size_t third = TRL_GUID_DATA1 + TRL_GUID_DATA2;
    octets[third] = (uint8_t)(guid->Data3 >> TRL_BYTE_BITS);
    octets[third + 1U] = (uint8_t)(guid->Data3 & TRL_BYTE_MASK);
    memcpy(octets + TRL_UUID_BYTES - TRL_GUID_DATA4, guid->Data4, TRL_GUID_DATA4);
    return trl_params_add_uuid(params, octets);
}

/* The length of a character or binary parameter: as its length says, or
   to its null, or as its buffer is long. */
static size_t length_of(const bound *b, bool text) {
    SQLLEN said = b->length != NULL ? *b->length : SQL_NTS;
    if (said >= 0) {
        return (size_t)said;
    }
    if (!text) {
        return b->octet_length > 0 ? (size_t)b->octet_length : 0U;
    }
    return strlen(b->data);
}

/* A string's value: text, wide text, or octets. */
static trl_status string_add(trl_odbc_diag *diag, const bound *b, trl_params *params, bool *known) {
    *known = true;
    if (b->c_type == SQL_C_CHAR) {
        return trl_params_add_text(params, b->data, length_of(b, true));
    }
    if (b->c_type == SQL_C_BINARY) {
        return trl_params_add_bytes(params, b->data, length_of(b, false));
    }
    if (b->c_type == SQL_C_WCHAR) {
        SQLLEN said = b->length != NULL ? *b->length : SQL_NTS;
        SQLINTEGER units = said >= 0 ? (SQLINTEGER)((size_t)said / sizeof(SQLWCHAR)) : SQL_NTS;
        char *text = trl_odbc_wide_text(b->data, units);
        if (text == NULL) {
            (void)trl_odbc_no_memory(diag);
            return TRL_NO_MEMORY;
        }
        trl_status added = trl_params_add_text(params, text, strlen(text));
        free(text);
        return added;
    }
    *known = false;
    return TRL_OK;
}

/* A number's or a truth value's value, of a float, a double or SQL_C_BIT; whether `c_type` is one of them. */
static bool approximate_of(const bound *b, trl_params *params, trl_status *added) {
    switch (b->c_type) {
    case SQL_C_BIT:
        *added = trl_params_add_boolean(params, *(const unsigned char *)b->data != 0U);
        return true;
    case SQL_C_FLOAT:
        *added = trl_params_add_double(params, (double)*(const float *)b->data);
        return true;
    case SQL_C_DOUBLE:
        *added = trl_params_add_double(params, *(const double *)b->data);
        return true;
    case SQL_C_NUMERIC:
        *added = numeric_add(params, b->data);
        return true;
    case SQL_C_GUID:
        *added = guid_add(params, b->data);
        return true;
    default:
        return false;
    }
}

/* A value of a C type that is not a string; whether `c_type` is one. */
static bool scalar_of(const bound *b, trl_params *params, trl_status *added) {
    char text[TRL_DATETIME_BYTES];
    if (datetime_of(b, text, sizeof text)) {
        *added = trl_params_add_text(params, text, strlen(text));
        return true;
    }
    return integer_of(b, params, added) || approximate_of(b, params, added);
}

/* Adds the parameter `b` holds, of its C type. */
static SQLRETURN one_added(trl_odbc_diag *diag, const bound *b, trl_params *params) {
    if (b->indicator != NULL && *b->indicator == SQL_NULL_DATA) {
        return trl_params_add_null(params) == TRL_OK ? SQL_SUCCESS : trl_odbc_no_memory(diag);
    }
    bool at_exec =
        b->indicator != NULL && (*b->indicator == SQL_DATA_AT_EXEC || *b->indicator <= SQL_LEN_DATA_AT_EXEC_OFFSET);
    if (at_exec) {
        return trl_odbc_fail(diag, "HYC00", "Optional feature not implemented: data at execution");
    }
    if (b->data == NULL) {
        return trl_odbc_fail(diag, "HY009", "Invalid use of null pointer");
    }
    trl_status added = TRL_OK;
    bool known = scalar_of(b, params, &added);
    if (!known) {
        added = string_add(diag, b, params, &known);
    }
    if (!known) {
        return trl_odbc_fail(diag, "HY003", "Invalid application buffer type");
    }
    return added == TRL_OK ? SQL_SUCCESS : trl_odbc_no_memory(diag);
}

SQLRETURN trl_odbc_params(trl_odbc_stmt *stmt, size_t count, trl_params **out) {
    *out = NULL;
    const trl_odbc_desc *apd = stmt->apd;
    const trl_odbc_desc *ipd = &stmt->implicit[TRL_ODBC_IPD];
    if ((size_t)apd->count < count) {
        return trl_odbc_fail(&stmt->diag, "07002", "COUNT field incorrect");
    }
    if (trl_params_new(out) != TRL_OK) {
        return trl_odbc_no_memory(&stmt->diag);
    }
    for (size_t i = 1U; i <= count; ++i) {
        const trl_odbc_field *field = &apd->fields[i];
        SQLSMALLINT sql_type = (SQLSMALLINT)i <= ipd->count ? ipd->fields[i].concise_type : SQL_VARCHAR;
        bound b = {
            offset(field->data, apd->bind_offset),
            offset(field->length_ptr, apd->bind_offset),
            offset(field->indicator_ptr, apd->bind_offset),
            field->octet_length,
            field->concise_type,
        };
        b.c_type = b.c_type == SQL_C_DEFAULT ? trl_odbc_default_c_type(sql_type) : b.c_type;
        SQLRETURN added = one_added(&stmt->diag, &b, *out);
        if (added != SQL_SUCCESS) {
            trl_params_free(*out);
            *out = NULL;
            return added;
        }
    }
    return SQL_SUCCESS;
}
