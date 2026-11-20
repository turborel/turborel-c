/**
 * @file describe.c
 * @brief A statement's columns described (RULES.md D2): SQLNumResultCols,
 * SQLDescribeCol and SQLColAttribute, and the implementation row
 * descriptor, of the result's columns or, after SQLPrepare, of the
 * prepared statement's; each type as ODBC's (`types.c`), by its declared
 * name where the server tells it.
 */
#include "odbc/describe.h"

#include "odbc/odbc.h"
#include "odbc/types.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

SQLSMALLINT trl_odbc_column_count(const trl_odbc_stmt *stmt) {
    size_t count = 0U;
    if (stmt->result != NULL) {
        count = trl_result_has_rows(stmt->result) ? trl_result_column_count(stmt->result) : 0U;
    } else if (stmt->prepared != NULL) {
        count = trl_statement_column_count(stmt->prepared);
    }
    return (SQLSMALLINT)(count > INT16_MAX ? INT16_MAX : count);
}

bool trl_odbc_column_type(const trl_odbc_stmt *stmt, SQLUSMALLINT column, const trl_data_type **type,
                          const char **declared) {
    if (column < 1 || column > (SQLUSMALLINT)trl_odbc_column_count(stmt)) {
        return false;
    }
    size_t at = (size_t)column - 1U;
    if (stmt->result != NULL) {
        *type = trl_result_column_type(stmt->result, at);
        *declared = trl_result_column_type_name(stmt->result, at);
    } else {
        *type = trl_statement_column_type(stmt->prepared, at);
        *declared = trl_statement_column_type_name(stmt->prepared, at);
    }
    return true;
}

/* Column `column`'s name, "" of one unnamed. */
static const char *name_of(const trl_odbc_stmt *stmt, SQLUSMALLINT column) {
    size_t at = (size_t)column - 1U;
    const char *name =
        stmt->result != NULL ? trl_result_column_name(stmt->result, at) : trl_statement_column_name(stmt->prepared, at);
    return name != NULL ? name : "";
}

/* An attribute that is text, of the column `column` of type `t`; null if `id` is none of them. */
static const char *text_attribute(const trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLUSMALLINT id,
                                  const trl_odbc_type *t) {
    switch (id) {
    case SQL_DESC_NAME:
    case SQL_DESC_LABEL:
    case SQL_DESC_BASE_COLUMN_NAME:
    case SQL_COLUMN_NAME:
        return name_of(stmt, column);
    case SQL_DESC_TYPE_NAME:
    case SQL_DESC_LOCAL_TYPE_NAME:
        return trl_odbc_type_name(t->sql_type);
    case SQL_DESC_TABLE_NAME:
    case SQL_DESC_BASE_TABLE_NAME:
    case SQL_DESC_SCHEMA_NAME:
    case SQL_DESC_CATALOG_NAME:
        /* The protocol does not tell where a column of a result came from. */
        return "";
    case SQL_DESC_LITERAL_PREFIX:
    case SQL_DESC_LITERAL_SUFFIX:
        return t->text ? "'" : "";
    default:
        return NULL;
    }
}

/* An attribute of the column's size; whether `id` is one of them. */
static bool size_attribute(SQLUSMALLINT id, const trl_odbc_type *t, SQLLEN *number) {
    switch (id) {
    case SQL_DESC_LENGTH:
    case SQL_COLUMN_LENGTH:
        *number = t->text || t->radix == 0 ? (SQLLEN)t->column_size : t->octet_length;
        return true;
    case SQL_DESC_PRECISION:
    case SQL_COLUMN_PRECISION:
        *number = (SQLLEN)t->column_size;
        return true;
    case SQL_DESC_SCALE:
    case SQL_COLUMN_SCALE:
        *number = t->digits;
        return true;
    case SQL_DESC_OCTET_LENGTH:
        *number = t->octet_length;
        return true;
    case SQL_DESC_DISPLAY_SIZE:
        *number = t->display_size;
        return true;
    case SQL_DESC_NUM_PREC_RADIX:
        *number = t->radix;
        return true;
    default:
        return false;
    }
}

/* An attribute that is a number, of type `t`; whether `id` is one of them. */
static bool number_attribute(SQLUSMALLINT id, const trl_odbc_type *t, SQLLEN *number) {
    if (size_attribute(id, t, number)) {
        return true;
    }
    switch (id) {
    case SQL_DESC_TYPE:
        *number = t->verbose_type;
        return true;
    case SQL_DESC_CONCISE_TYPE:
        *number = t->sql_type;
        return true;
    case SQL_DESC_DATETIME_INTERVAL_CODE:
        *number = t->interval_code;
        return true;
    case SQL_DESC_DATETIME_INTERVAL_PRECISION:
        /* An interval's leading precision; none is told, as an interval
           is given as its text (RULES.md D8). */
        *number = 0;
        return true;
    case SQL_DESC_NULLABLE:
    case SQL_COLUMN_NULLABLE:
        *number = SQL_NULLABLE_UNKNOWN;
        return true;
    case SQL_DESC_CASE_SENSITIVE:
        *number = t->text ? SQL_TRUE : SQL_FALSE;
        return true;
    case SQL_DESC_SEARCHABLE:
        *number = SQL_PRED_SEARCHABLE;
        return true;
    case SQL_DESC_UPDATABLE:
        *number = SQL_ATTR_READWRITE_UNKNOWN;
        return true;
    case SQL_DESC_UNNAMED:
        *number = SQL_NAMED;
        return true;
    case SQL_DESC_AUTO_UNIQUE_VALUE:
    case SQL_DESC_FIXED_PREC_SCALE:
    case SQL_DESC_UNSIGNED:
        *number = id == SQL_DESC_UNSIGNED && t->radix == 0 ? SQL_TRUE : SQL_FALSE;
        return true;
    default:
        return false;
    }
}

SQLRETURN trl_odbc_column_attribute(trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLUSMALLINT id,
                                    trl_odbc_attribute *out) {
    out->text = NULL;
    out->number = 0;
    if (id == SQL_DESC_COUNT || id == SQL_COLUMN_COUNT) {
        out->number = trl_odbc_column_count(stmt);
        return SQL_SUCCESS;
    }
    const trl_data_type *type = NULL;
    const char *declared = NULL;
    if (!trl_odbc_column_type(stmt, column, &type, &declared)) {
        return trl_odbc_fail(&stmt->diag, "07009", "Invalid descriptor index");
    }
    trl_odbc_type t;
    trl_odbc_type_of(type, declared, &t);
    out->text = text_attribute(stmt, column, id, &t);
    if (out->text != NULL) {
        if (id == SQL_DESC_NAME && out->text[0] == '\0') {
            out->number = SQL_UNNAMED;
        }
        return SQL_SUCCESS;
    }
    if (!number_attribute(id, &t, &out->number)) {
        return trl_odbc_fail(&stmt->diag, "HY091", "Invalid descriptor field identifier");
    }
    return SQL_SUCCESS;
}

/* The bytes of the implementation row descriptor's field `id`, a number. */
static size_t width_of(SQLSMALLINT id) {
    switch (id) {
    case SQL_DESC_LENGTH:
        return sizeof(SQLULEN);
    case SQL_DESC_OCTET_LENGTH:
    case SQL_DESC_DISPLAY_SIZE:
        return sizeof(SQLLEN);
    case SQL_DESC_AUTO_UNIQUE_VALUE:
    case SQL_DESC_CASE_SENSITIVE:
    case SQL_DESC_NUM_PREC_RADIX:
    case SQL_DESC_DATETIME_INTERVAL_PRECISION:
        return sizeof(SQLINTEGER);
    default:
        return sizeof(SQLSMALLINT);
    }
}

SQLRETURN trl_odbc_column_field(trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLSMALLINT id, const trl_odbc_out *out) {
    trl_odbc_attribute attribute;
    SQLRETURN got = trl_odbc_column_attribute(stmt, column, (SQLUSMALLINT)id, &attribute);
    if (got != SQL_SUCCESS) {
        return got;
    }
    if (attribute.text != NULL) {
        return trl_odbc_put(&stmt->diag, out, attribute.text);
    }
    size_t width = width_of(id);
    if (width == sizeof(SQLLEN)) {
        SQLLEN number = attribute.number;
        memcpy(out->buffer, &number, sizeof number);
    } else if (width == sizeof(SQLINTEGER)) {
        *(SQLINTEGER *)out->buffer = (SQLINTEGER)attribute.number;
    } else {
        *(SQLSMALLINT *)out->buffer = (SQLSMALLINT)attribute.number;
    }
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT handle, SQLSMALLINT *count) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (count == NULL) {
        return trl_odbc_fail(&stmt->diag, "HY009", "Invalid use of null pointer");
    }
    *count = trl_odbc_column_count(stmt);
    return SQL_SUCCESS;
}

/* SQLDescribeCol's work, its name into `name`. */
SQLRETURN trl_odbc_describe_col(trl_odbc_stmt *stmt, SQLUSMALLINT column, const trl_odbc_out *name,
                                const trl_odbc_described *out) {
    trl_odbc_clear(&stmt->diag);
    const trl_data_type *type = NULL;
    const char *declared = NULL;
    if (!trl_odbc_column_type(stmt, column, &type, &declared)) {
        return trl_odbc_fail(&stmt->diag, "07009", "Invalid descriptor index");
    }
    trl_odbc_type t;
    trl_odbc_type_of(type, declared, &t);
    if (out->sql_type != NULL) {
        *out->sql_type = t.sql_type;
    }
    if (out->size != NULL) {
        *out->size = t.column_size;
    }
    if (out->digits != NULL) {
        *out->digits = t.digits;
    }
    if (out->nullable != NULL) {
        *out->nullable = SQL_NULLABLE_UNKNOWN;
    }
    return trl_odbc_put(&stmt->diag, name, name_of(stmt, column));
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT handle, SQLUSMALLINT column, SQLCHAR *name,
                                                 SQLSMALLINT capacity, SQLSMALLINT *name_length, SQLSMALLINT *sql_type,
                                                 SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out named = {name, capacity, name_length, NULL, NULL, false, false};
    trl_odbc_described out = {sql_type, size, digits, nullable};
    return trl_odbc_describe_col(stmt, column, &named, &out);
}

/* SQLColAttribute's work: text into `text`, a number into `number`. */
SQLRETURN trl_odbc_col_attribute(trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLUSMALLINT id, const trl_odbc_out *text,
                                 SQLLEN *number) {
    trl_odbc_clear(&stmt->diag);
    trl_odbc_attribute attribute;
    SQLRETURN got = trl_odbc_column_attribute(stmt, column, id, &attribute);
    if (got != SQL_SUCCESS) {
        return got;
    }
    if (attribute.text != NULL) {
        return trl_odbc_put(&stmt->diag, text, attribute.text);
    }
    if (number != NULL) {
        *number = attribute.number;
    }
    return SQL_SUCCESS;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLColAttribute(SQLHSTMT handle, SQLUSMALLINT column, SQLUSMALLINT id,
                                                  SQLPOINTER text, SQLSMALLINT capacity, SQLSMALLINT *length,
                                                  SQLLEN *number) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {text, capacity, length, NULL, NULL, false, true};
    return trl_odbc_col_attribute(stmt, column, id, &out, number);
}
