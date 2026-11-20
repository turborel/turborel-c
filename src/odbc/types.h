/**
 * @file types.h
 * @brief Turborel's types as ODBC's SQL types, and the rows
 * SQLGetTypeInfo gives.
 */
#ifndef TRL_ODBC_TYPES_H
#define TRL_ODBC_TYPES_H

#include "odbc/odbc.h"

#include <stdbool.h>

/** @brief A type as ODBC describes it. */
typedef struct trl_odbc_type {
    /** @brief Its concise SQL type, SQL_DESC_CONCISE_TYPE. */
    SQLSMALLINT sql_type;
    /** @brief Its verbose type, SQL_DESC_TYPE: SQL_DATETIME of a datetime. */
    SQLSMALLINT verbose_type;
    /** @brief A datetime's subcode, SQL_DESC_DATETIME_INTERVAL_CODE. */
    SQLSMALLINT interval_code;
    /** @brief Its column size: characters, digits, or a datetime's characters. */
    SQLULEN column_size;
    /** @brief Its decimal digits: a number's scale, a datetime's fractional digits. */
    SQLSMALLINT digits;
    /** @brief Its octet length: the bytes a value takes in its default C type. */
    SQLLEN octet_length;
    /** @brief Its display size, in characters. */
    SQLLEN display_size;
    /** @brief A number's radix, 10, else 0. */
    SQLSMALLINT radix;
    /** @brief Whether it is a character string, of which case tells values apart. */
    bool text;
} trl_odbc_type;

/** @brief The concise SQL type of `type`, by `declared`, its declared type's name, if told. */
SQLSMALLINT trl_odbc_sql_type(const trl_data_type *type, const char *declared);

/** @brief `type`, of the declared name `declared` if told, as ODBC describes it, into `out`. */
void trl_odbc_type_of(const trl_data_type *type, const char *declared, trl_odbc_type *out);

/** @brief The name of Turborel's type of the concise SQL type `sql_type`, as SQLGetTypeInfo gives it. */
const char *trl_odbc_type_name(SQLSMALLINT sql_type);

#endif
