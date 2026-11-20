/**
 * @file describe.h
 * @brief A statement's columns, as SQLDescribeCol, SQLColAttribute and
 * the implementation row descriptor describe them.
 */
#ifndef TRL_ODBC_DESCRIBE_H
#define TRL_ODBC_DESCRIBE_H

#include "odbc/odbc.h"

#include <stdbool.h>

/** @brief What a column's attribute is: text, or a number. */
typedef struct trl_odbc_attribute {
    /** @brief Its text, if it is text. */
    const char *text;
    /** @brief Its number, if it is a number. */
    SQLLEN number;
} trl_odbc_attribute;

/** @brief How many columns the statement's result has, or its prepared statement's. */
SQLSMALLINT trl_odbc_column_count(const trl_odbc_stmt *stmt);

/** @brief Column `column`'s data type and declared type's name, from 1; false past the last. */
bool trl_odbc_column_type(const trl_odbc_stmt *stmt, SQLUSMALLINT column, const trl_data_type **type,
                          const char **declared);

/**
 * @brief Attribute `id` of column `column`, from 1, SQL_DESC_ or ODBC 2's
 * SQL_COLUMN_, into `out`; SQL_ERROR with the statement's diagnostics of
 * a column past the last or an attribute that is none.
 */
SQLRETURN trl_odbc_column_attribute(trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLUSMALLINT id, trl_odbc_attribute *out);

/** @brief Field `id` of the implementation row descriptor's record `column`, into `out`, of the field's own width. */
SQLRETURN trl_odbc_column_field(trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLSMALLINT id, const trl_odbc_out *out);

/** @brief Where SQLDescribeCol's numbers go; each may be null. */
typedef struct trl_odbc_described {
    /** @brief The SQL type. */
    SQLSMALLINT *sql_type;
    /** @brief The column size. */
    SQLULEN *size;
    /** @brief The decimal digits. */
    SQLSMALLINT *digits;
    /** @brief Whether it may be null. */
    SQLSMALLINT *nullable;
} trl_odbc_described;

/** @brief SQLDescribeCol's work: column `column`'s name into `name`, its numbers into `out`. */
SQLRETURN trl_odbc_describe_col(trl_odbc_stmt *stmt, SQLUSMALLINT column, const trl_odbc_out *name,
                                const trl_odbc_described *out);

/** @brief SQLColAttribute's work: text into `text`, a number into `number`. */
SQLRETURN trl_odbc_col_attribute(trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLUSMALLINT id, const trl_odbc_out *text,
                                 SQLLEN *number);

#endif
