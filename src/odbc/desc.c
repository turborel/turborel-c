/**
 * @file desc.c
 * @brief Descriptors (RULES.md D2): each statement's four, the columns
 * and parameters the application bound and the SQL types of the
 * parameters, and those the application allocates to bind by; read and
 * set by SQLGetDescField, SQLSetDescField, their record forms and
 * SQLCopyDesc. The implementation row descriptor's records are the
 * result's columns, described as SQLColAttribute describes them.
 */
#include "memory.h"
#include "odbc/describe.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The records a descriptor first holds room for. */
#define TRL_DESC_FIRST 8
/* How the records' room grows. */
#define TRL_DESC_GROWTH 2

void trl_odbc_desc_init(trl_odbc_desc *desc, trl_odbc_dbc *dbc, trl_odbc_stmt *stmt, trl_odbc_role role) {
    memset(desc, 0, sizeof *desc);
    desc->kind = SQL_HANDLE_DESC;
    desc->dbc = dbc;
    desc->stmt = stmt;
    desc->role = role;
    desc->array_size = 1U;
    desc->bind_type = SQL_BIND_BY_COLUMN;
}

void trl_odbc_desc_destroy(trl_odbc_desc *desc) {
    free(desc->fields);
    desc->fields = NULL;
    desc->count = 0;
    desc->capacity = 0;
    trl_odbc_clear(&desc->diag);
}

/* A record's defaults: SQL_C_DEFAULT of an application's, a parameter for input. */
static void field_init(trl_odbc_field *field) {
    memset(field, 0, sizeof *field);
    field->type = SQL_C_DEFAULT;
    field->concise_type = SQL_C_DEFAULT;
    field->parameter_type = SQL_PARAM_INPUT;
}

trl_odbc_field *trl_odbc_desc_field(trl_odbc_desc *desc, SQLSMALLINT number) {
    if (number < 1) {
        return NULL;
    }
    if (number >= desc->capacity) {
        SQLSMALLINT capacity = desc->capacity == 0 ? TRL_DESC_FIRST : desc->capacity;
        while (capacity <= number && capacity < INT16_MAX / TRL_DESC_GROWTH) {
            capacity = (SQLSMALLINT)(capacity * TRL_DESC_GROWTH);
        }
        if (capacity <= number) {
            return NULL;
        }
        trl_odbc_field *grown = trl_realloc(desc->fields, (size_t)capacity * sizeof *grown);
        if (grown == NULL) {
            return NULL;
        }
        for (SQLSMALLINT i = desc->capacity; i < capacity; ++i) {
            field_init(&grown[i]);
        }
        desc->fields = grown;
        desc->capacity = capacity;
    }
    desc->count = number > desc->count ? number : desc->count;
    return &desc->fields[number];
}

/* The verbose type and datetime subcode of a concise type, C or SQL. */
static void concise_set(trl_odbc_field *field, SQLSMALLINT concise) {
    static const struct {
        SQLSMALLINT concise;
        SQLSMALLINT code;
    } datetimes[] = {
        {SQL_TYPE_DATE, SQL_CODE_DATE},
        {SQL_TYPE_TIME, SQL_CODE_TIME},
        {SQL_TYPE_TIMESTAMP, SQL_CODE_TIMESTAMP},
    };
    field->concise_type = concise;
    field->type = concise;
    field->interval_code = 0;
    for (size_t i = 0U; i < sizeof datetimes / sizeof datetimes[0]; ++i) {
        if (datetimes[i].concise == concise) {
            field->type = SQL_DATETIME;
            field->interval_code = datetimes[i].code;
        }
    }
}

void trl_odbc_field_type(trl_odbc_field *field, SQLSMALLINT concise) {
    concise_set(field, concise);
}

/* A header field, into `value`; whether `id` is one. */
static bool header_get(const trl_odbc_desc *desc, SQLSMALLINT id, SQLPOINTER value) {
    switch (id) {
    case SQL_DESC_COUNT:
        *(SQLSMALLINT *)value =
            desc->role == TRL_ODBC_IRD && desc->stmt != NULL ? trl_odbc_column_count(desc->stmt) : desc->count;
        return true;
    case SQL_DESC_ARRAY_SIZE:
        *(SQLULEN *)value = desc->array_size;
        return true;
    case SQL_DESC_BIND_TYPE:
        *(SQLINTEGER *)value = (SQLINTEGER)desc->bind_type;
        return true;
    case SQL_DESC_BIND_OFFSET_PTR:
        *(SQLLEN **)value = desc->bind_offset;
        return true;
    case SQL_DESC_ARRAY_STATUS_PTR:
        *(SQLUSMALLINT **)value = desc->array_status;
        return true;
    case SQL_DESC_ROWS_PROCESSED_PTR:
        *(SQLULEN **)value = desc->rows_processed;
        return true;
    case SQL_DESC_ALLOC_TYPE:
        *(SQLSMALLINT *)value = desc->stmt != NULL ? SQL_DESC_ALLOC_AUTO : SQL_DESC_ALLOC_USER;
        return true;
    default:
        return false;
    }
}

/* A record's field, into `value`; whether `id` is one. */
static bool record_get(const trl_odbc_field *field, SQLSMALLINT id, SQLPOINTER value) {
    switch (id) {
    case SQL_DESC_TYPE:
        *(SQLSMALLINT *)value = field->type;
        return true;
    case SQL_DESC_CONCISE_TYPE:
        *(SQLSMALLINT *)value = field->concise_type;
        return true;
    case SQL_DESC_DATETIME_INTERVAL_CODE:
        *(SQLSMALLINT *)value = field->interval_code;
        return true;
    case SQL_DESC_DATA_PTR:
        *(SQLPOINTER *)value = field->data;
        return true;
    case SQL_DESC_OCTET_LENGTH:
        *(SQLLEN *)value = field->octet_length;
        return true;
    case SQL_DESC_OCTET_LENGTH_PTR:
        *(SQLLEN **)value = field->length_ptr;
        return true;
    case SQL_DESC_INDICATOR_PTR:
        *(SQLLEN **)value = field->indicator_ptr;
        return true;
    case SQL_DESC_LENGTH:
        *(SQLULEN *)value = field->length;
        return true;
    case SQL_DESC_PRECISION:
        *(SQLSMALLINT *)value = field->precision;
        return true;
    case SQL_DESC_SCALE:
        *(SQLSMALLINT *)value = field->scale;
        return true;
    case SQL_DESC_PARAMETER_TYPE:
        *(SQLSMALLINT *)value = field->parameter_type;
        return true;
    default:
        return false;
    }
}

SQLRETURN trl_odbc_desc_get(trl_odbc_desc *desc, SQLSMALLINT number, SQLSMALLINT id, const trl_odbc_out *out) {
    trl_odbc_clear(&desc->diag);
    if (header_get(desc, id, out->buffer)) {
        return SQL_SUCCESS;
    }
    if (desc->role == TRL_ODBC_IRD && desc->stmt != NULL) {
        return trl_odbc_column_field(desc->stmt, (SQLUSMALLINT)number, id, out);
    }
    if (number < 1 || number > desc->count) {
        return number < 1 ? trl_odbc_fail(&desc->diag, "07009", "Invalid descriptor index") : SQL_NO_DATA;
    }
    if (!record_get(&desc->fields[number], id, out->buffer)) {
        return trl_odbc_fail(&desc->diag, "HY091", "Invalid descriptor field identifier");
    }
    return SQL_SUCCESS;
}

/* Sets a header field; whether `id` is one. */
static bool header_set(trl_odbc_desc *desc, SQLSMALLINT id, SQLPOINTER value) {
    switch (id) {
    case SQL_DESC_COUNT:
        desc->count = (SQLSMALLINT)(intptr_t)value;
        return trl_odbc_desc_field(desc, desc->count) != NULL || desc->count == 0;
    case SQL_DESC_ARRAY_SIZE:
        desc->array_size = (SQLULEN)(uintptr_t)value;
        return true;
    case SQL_DESC_BIND_TYPE:
        desc->bind_type = (SQLULEN)(uintptr_t)value;
        return true;
    case SQL_DESC_BIND_OFFSET_PTR:
        desc->bind_offset = value;
        return true;
    case SQL_DESC_ARRAY_STATUS_PTR:
        desc->array_status = value;
        return true;
    case SQL_DESC_ROWS_PROCESSED_PTR:
        desc->rows_processed = value;
        return true;
    default:
        return false;
    }
}

/* Sets a record's field; whether `id` is one an application may set. */
static bool record_set(trl_odbc_field *field, SQLSMALLINT id, SQLPOINTER value) {
    SQLSMALLINT small = (SQLSMALLINT)(intptr_t)value;
    switch (id) {
    case SQL_DESC_TYPE:
    case SQL_DESC_CONCISE_TYPE:
        concise_set(field, small);
        return true;
    case SQL_DESC_DATETIME_INTERVAL_CODE:
        field->interval_code = small;
        return true;
    case SQL_DESC_DATA_PTR:
        field->data = value;
        return true;
    case SQL_DESC_OCTET_LENGTH:
        field->octet_length = (SQLLEN)(intptr_t)value;
        return true;
    case SQL_DESC_OCTET_LENGTH_PTR:
        field->length_ptr = value;
        return true;
    case SQL_DESC_INDICATOR_PTR:
        field->indicator_ptr = value;
        return true;
    case SQL_DESC_LENGTH:
        field->length = (SQLULEN)(uintptr_t)value;
        return true;
    case SQL_DESC_PRECISION:
        field->precision = small;
        return true;
    case SQL_DESC_SCALE:
        field->scale = small;
        return true;
    case SQL_DESC_PARAMETER_TYPE:
        field->parameter_type = small;
        return true;
    default:
        return false;
    }
}

/* Sets field `id` of record `number`, or of the header. */
static SQLRETURN desc_set(trl_odbc_desc *desc, SQLSMALLINT number, SQLSMALLINT id, SQLPOINTER value) {
    trl_odbc_clear(&desc->diag);
    if (desc->role == TRL_ODBC_IRD) {
        return trl_odbc_fail(&desc->diag, "HY016", "Cannot modify an implementation row descriptor");
    }
    if (header_set(desc, id, value)) {
        return SQL_SUCCESS;
    }
    trl_odbc_field *field = trl_odbc_desc_field(desc, number);
    if (field == NULL) {
        return number < 1 ? trl_odbc_fail(&desc->diag, "07009", "Invalid descriptor index")
                          : trl_odbc_no_memory(&desc->diag);
    }
    if (!record_set(field, id, value)) {
        return trl_odbc_fail(&desc->diag, "HY091", "Invalid descriptor field identifier");
    }
    return SQL_SUCCESS;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDescField(SQLHDESC handle, SQLSMALLINT number, SQLSMALLINT id, SQLPOINTER value,
                                                  SQLINTEGER capacity, SQLINTEGER *length) {
    trl_odbc_desc *desc = trl_odbc_desc_of(handle);
    if (desc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {value, capacity, NULL, length, NULL, false, true};
    return trl_odbc_desc_get(desc, number, id, &out);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetDescField(SQLHDESC handle, SQLSMALLINT number, SQLSMALLINT id, SQLPOINTER value,
                                                  SQLINTEGER length) {
    (void)length;
    trl_odbc_desc *desc = trl_odbc_desc_of(handle);
    return desc == NULL ? SQL_INVALID_HANDLE : desc_set(desc, number, id, value);
}

/* SQLSetDescRec's values. */
typedef struct rec_values {
    SQLSMALLINT type;
    SQLSMALLINT subtype;
    SQLLEN length;
    SQLSMALLINT precision;
    SQLSMALLINT scale;
    SQLPOINTER data;
    SQLLEN *length_ptr;
    SQLLEN *indicator_ptr;
} rec_values;

/* Sets each of a record's values, as SQLSetDescRec does. */
static SQLRETURN rec_set(trl_odbc_desc *desc, SQLSMALLINT number, const rec_values *v) {
    trl_odbc_clear(&desc->diag);
    if (desc->role == TRL_ODBC_IRD) {
        return trl_odbc_fail(&desc->diag, "HY016", "Cannot modify an implementation row descriptor");
    }
    trl_odbc_field *field = trl_odbc_desc_field(desc, number);
    if (field == NULL) {
        return number < 1 ? trl_odbc_fail(&desc->diag, "07009", "Invalid descriptor index")
                          : trl_odbc_no_memory(&desc->diag);
    }
    concise_set(field, v->type);
    field->interval_code = v->type == SQL_DATETIME ? v->subtype : field->interval_code;
    field->octet_length = v->length;
    field->length = (SQLULEN)v->length;
    field->precision = v->precision;
    field->scale = v->scale;
    field->data = v->data;
    field->length_ptr = v->length_ptr;
    field->indicator_ptr = v->indicator_ptr;
    return SQL_SUCCESS;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetDescRec(SQLHDESC handle, SQLSMALLINT number, SQLSMALLINT type,
                                                SQLSMALLINT subtype, SQLLEN length, SQLSMALLINT precision,
                                                SQLSMALLINT scale, SQLPOINTER data, SQLLEN *length_ptr,
                                                SQLLEN *indicator_ptr) {
    trl_odbc_desc *desc = trl_odbc_desc_of(handle);
    if (desc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    rec_values v = {type, subtype, length, precision, scale, data, length_ptr, indicator_ptr};
    return rec_set(desc, number, &v);
}

/* Reads a record's numbers, as SQLGetDescRec gives them. */
static SQLRETURN rec_numbers(trl_odbc_desc *desc, SQLSMALLINT number, rec_values *v, SQLSMALLINT *nullable) {
    trl_odbc_out none = {NULL, 0, NULL, NULL, NULL, false, true};
    SQLSMALLINT small = 0;
    none.buffer = &v->type;
    SQLRETURN got = trl_odbc_desc_get(desc, number, SQL_DESC_TYPE, &none);
    none.buffer = &v->subtype;
    got = got == SQL_SUCCESS ? trl_odbc_desc_get(desc, number, SQL_DESC_DATETIME_INTERVAL_CODE, &none) : got;
    none.buffer = &v->length;
    got = got == SQL_SUCCESS ? trl_odbc_desc_get(desc, number, SQL_DESC_OCTET_LENGTH, &none) : got;
    none.buffer = &v->precision;
    got = got == SQL_SUCCESS ? trl_odbc_desc_get(desc, number, SQL_DESC_PRECISION, &none) : got;
    none.buffer = &v->scale;
    got = got == SQL_SUCCESS ? trl_odbc_desc_get(desc, number, SQL_DESC_SCALE, &none) : got;
    none.buffer = &small;
    bool implementation = desc->role == TRL_ODBC_IRD || desc->role == TRL_ODBC_IPD;
    if (got == SQL_SUCCESS && implementation && desc->role == TRL_ODBC_IRD) {
        got = trl_odbc_desc_get(desc, number, SQL_DESC_NULLABLE, &none);
    }
    *nullable = 0;
    if (implementation) {
        *nullable = desc->role == TRL_ODBC_IRD ? small : (SQLSMALLINT)SQL_NULLABLE;
    }
    return got;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDescRec(SQLHDESC handle, SQLSMALLINT number, SQLCHAR *name,
                                                SQLSMALLINT capacity, SQLSMALLINT *name_length, SQLSMALLINT *type,
                                                SQLSMALLINT *subtype, SQLLEN *length, SQLSMALLINT *precision,
                                                SQLSMALLINT *scale, SQLSMALLINT *nullable) {
    trl_odbc_desc *desc = trl_odbc_desc_of(handle);
    if (desc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    rec_values v;
    memset(&v, 0, sizeof v);
    SQLSMALLINT null_said = 0;
    SQLRETURN got = rec_numbers(desc, number, &v, &null_said);
    if (got != SQL_SUCCESS) {
        return got;
    }
    trl_odbc_out named = {name, capacity, name_length, NULL, NULL, false, false};
    got = desc->role == TRL_ODBC_IRD ? trl_odbc_desc_get(desc, number, SQL_DESC_NAME, &named)
                                     : trl_odbc_put(&desc->diag, &named, "");
    SQLSMALLINT *outs[] = {type, subtype, precision, scale, nullable};
    const SQLSMALLINT values[] = {v.type, v.subtype, v.precision, v.scale, null_said};
    for (size_t i = 0U; i < sizeof values / sizeof values[0]; ++i) {
        if (outs[i] != NULL) {
            *outs[i] = values[i];
        }
    }
    if (length != NULL) {
        *length = v.length;
    }
    return got;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLCopyDesc(SQLHDESC source_handle, SQLHDESC target_handle) {
    trl_odbc_desc *source = trl_odbc_desc_of(source_handle);
    trl_odbc_desc *target = trl_odbc_desc_of(target_handle);
    if (source == NULL || target == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&target->diag);
    if (target->role == TRL_ODBC_IRD) {
        return trl_odbc_fail(&target->diag, "HY016", "Cannot modify an implementation row descriptor");
    }
    if (source->role == TRL_ODBC_IRD) {
        return trl_odbc_fail(&target->diag, "HYC00", "Optional feature not implemented");
    }
    if (source->count > 0 && trl_odbc_desc_field(target, source->count) == NULL) {
        return trl_odbc_no_memory(&target->diag);
    }
    for (SQLSMALLINT i = 1; i <= source->count; ++i) {
        target->fields[i] = source->fields[i];
    }
    target->count = source->count;
    target->array_size = source->array_size;
    target->bind_type = source->bind_type;
    target->bind_offset = source->bind_offset;
    target->array_status = source->array_status;
    target->rows_processed = source->rows_processed;
    return SQL_SUCCESS;
}
