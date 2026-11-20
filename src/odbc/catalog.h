/**
 * @file catalog.h
 * @brief The catalog functions: each a query of the Information Schema
 * whose result has the columns ODBC names, run on the statement as its
 * cursor.
 */
#ifndef TRL_ODBC_CATALOG_H
#define TRL_ODBC_CATALOG_H

#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief The most names a catalog function takes: SQLForeignKeys' six. */
#define TRL_CATALOG_NAMES_MOST 6U

/** @brief Where each name a catalog function takes is in its list. */
enum trl_catalog_name {
    /** @brief The catalog. */
    TRL_NAME_CATALOG = 0,
    /** @brief The schema. */
    TRL_NAME_SCHEMA = 1,
    /** @brief The table, or the procedure. */
    TRL_NAME_TABLE = 2,
    /** @brief The column, or SQLTables' table types. */
    TRL_NAME_COLUMN = 3,
    /** @brief SQLForeignKeys' foreign key's catalog. */
    TRL_NAME_FK_CATALOG = 3,
    /** @brief Its schema. */
    TRL_NAME_FK_SCHEMA = 4,
    /** @brief Its table. */
    TRL_NAME_FK_TABLE = 5,
};

/** @brief A query being written. */
typedef struct trl_odbc_sql {
    /** @brief Its text, null-ended. */
    char *data;
    /** @brief Its bytes. */
    size_t length;
    /** @brief The bytes its buffer holds. */
    size_t capacity;
    /** @brief Whether memory could not be had. */
    bool failed;
} trl_odbc_sql;

/** @brief Appends `text`. */
void trl_odbc_sql_put(trl_odbc_sql *sql, const char *text);

/** @brief Appends `value` as a character string literal, a quote in it written twice. */
void trl_odbc_sql_literal(trl_odbc_sql *sql, const char *value);

/**
 * @brief Appends ` AND column ...` of a catalog function's argument `arg`,
 * none if it is null: a search pattern, LIKE with `\` its escape, if
 * `pattern` and SQL_ATTR_METADATA_ID is off; else the name it is, as an
 * identifier if SQL_ATTR_METADATA_ID is on, in upper case unless quoted.
 */
void trl_odbc_sql_filter(trl_odbc_sql *sql, const trl_odbc_stmt *stmt, const char *column, const char *arg,
                         bool pattern);

/** @brief Appends the columns DATA_TYPE to DECIMAL_DIGITS and NUM_PREC_RADIX of the COLUMNS row `alias`, as SQLColumns
 * gives them. */
void trl_odbc_sql_types(trl_odbc_sql *sql, const char *alias);

/** @brief Runs the catalog query `sql`, freed, its result the statement's cursor. */
SQLRETURN trl_odbc_catalog_run(trl_odbc_stmt *stmt, trl_odbc_sql *sql);

/** @brief SQLTables' work, of a catalog, schema, table and table types, each null if not given. */
SQLRETURN trl_odbc_tables(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLColumns' work, of a catalog, schema, table and column. */
SQLRETURN trl_odbc_columns(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLPrimaryKeys' work, of a catalog, schema and table. */
SQLRETURN trl_odbc_primary_keys(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLForeignKeys' work, of the primary key's catalog, schema and table, then the foreign key's. */
SQLRETURN trl_odbc_foreign_keys(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLStatistics' work, of a catalog, schema and table: its unique constraints. */
SQLRETURN trl_odbc_statistics(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLSpecialColumns' work, of `type`, and a catalog, schema and table. */
SQLRETURN trl_odbc_special_columns(trl_odbc_stmt *stmt, SQLUSMALLINT type, const char *const *names);

/** @brief SQLProcedures' work, of a catalog, schema and procedure. */
SQLRETURN trl_odbc_procedures(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLProcedureColumns' work, of a catalog, schema, procedure and column. */
SQLRETURN trl_odbc_procedure_columns(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLTablePrivileges' work, of a catalog, schema and table. */
SQLRETURN trl_odbc_table_privileges(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLColumnPrivileges' work, of a catalog, schema, table and column. */
SQLRETURN trl_odbc_column_privileges(trl_odbc_stmt *stmt, const char *const *names);

/** @brief SQLGetTypeInfo's work, of `type`, SQL_ALL_TYPES for each. */
SQLRETURN trl_odbc_type_info(trl_odbc_stmt *stmt, SQLSMALLINT type);

/** @brief Each of `count` names an application gave, each `length` bytes or SQL_NTS, as new strings into `out`; false
 * if memory cannot be had. */
bool trl_odbc_names(SQLCHAR *const *names, const SQLSMALLINT *lengths, size_t count, char **out);

/** @brief Each of `count` names an application gave as SQLWCHAR, as new strings into `out`; false if memory cannot be
 * had. */
bool trl_odbc_wide_names(SQLWCHAR *const *names, const SQLSMALLINT *lengths, size_t count, char **out);

/** @brief Frees each of `count` names. */
void trl_odbc_names_free(char **names, size_t count);

/** @brief Runs `work` of the statement `handle` with the `count` names an application gave, as strings. */
SQLRETURN trl_odbc_with_names(SQLHSTMT handle, SQLCHAR *const *given, const SQLSMALLINT *lengths, size_t count,
                              SQLRETURN (*work)(trl_odbc_stmt *, const char *const *));

#endif
