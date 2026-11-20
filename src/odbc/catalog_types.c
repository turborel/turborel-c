/**
 * @file catalog_types.c
 * @brief SQLGetTypeInfo (RULES.md D2): a row of each of Turborel's types
 * ODBC has a type of, as `types.c` describes them, ordered by DATA_TYPE;
 * and the ANSI entry points of the catalog functions of
 * `catalog_more.c`.
 */
#include "format.h"
#include "odbc/catalog.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Room for a number. */
#define TRL_NUMBER_BYTES 24U
/* The most characters or octets a string type's column size says. */
#define TRL_LENGTH_MOST 2147483647L
/* SQLGetTypeInfo's columns that are text, as their place. */
#define TRL_INFO_COLUMNS 19U

/* A type's row: its name, codes, size and the rest; -1 of a number that is null. */
typedef struct type_row {
    const char *name;
    long data_type;
    long size;
    const char *prefix;
    const char *suffix;
    const char *params;
    long case_sensitive;
    long is_unsigned;
    long min_scale;
    long max_scale;
    long sql_data_type;
    long sub;
    long radix;
} type_row;

/* A number that is null in a type's row. */
#define TRL_NULL (-1L)

static const type_row rows[] = {
    {"UUID", SQL_GUID, 36, NULL, NULL, NULL, 0, TRL_NULL, TRL_NULL, TRL_NULL, SQL_GUID, TRL_NULL, TRL_NULL},
    {"BOOLEAN", SQL_BIT, 1, NULL, NULL, NULL, 0, TRL_NULL, TRL_NULL, TRL_NULL, SQL_BIT, TRL_NULL, TRL_NULL},
    {"BIGINT", SQL_BIGINT, 19, NULL, NULL, NULL, 0, 0, 0, 0, SQL_BIGINT, TRL_NULL, 10},
    {
        "BINARY LARGE OBJECT",
        SQL_LONGVARBINARY,
        TRL_LENGTH_MOST,
        "X'",
        "'",
        "length",
        0,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_LONGVARBINARY,
        TRL_NULL,
        TRL_NULL,
    },
    {
        "BINARY VARYING",
        SQL_VARBINARY,
        TRL_LENGTH_MOST,
        "X'",
        "'",
        "length",
        0,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_VARBINARY,
        TRL_NULL,
        TRL_NULL,
    },
    {
        "BINARY",
        SQL_BINARY,
        TRL_LENGTH_MOST,
        "X'",
        "'",
        "length",
        0,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_BINARY,
        TRL_NULL,
        TRL_NULL,
    },
    {
        "CHARACTER LARGE OBJECT",
        SQL_LONGVARCHAR,
        TRL_LENGTH_MOST,
        "'",
        "'",
        "length",
        1,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_LONGVARCHAR,
        TRL_NULL,
        TRL_NULL,
    },
    {
        "CHARACTER",
        SQL_CHAR,
        TRL_LENGTH_MOST,
        "'",
        "'",
        "length",
        1,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_CHAR,
        TRL_NULL,
        TRL_NULL,
    },
    {"NUMERIC", SQL_NUMERIC, 38, NULL, NULL, "precision,scale", 0, 0, 0, 38, SQL_NUMERIC, TRL_NULL, 10},
    {"DECIMAL", SQL_DECIMAL, 38, NULL, NULL, "precision,scale", 0, 0, 0, 38, SQL_DECIMAL, TRL_NULL, 10},
    {"INTEGER", SQL_INTEGER, 10, NULL, NULL, NULL, 0, 0, 0, 0, SQL_INTEGER, TRL_NULL, 10},
    {"SMALLINT", SQL_SMALLINT, 5, NULL, NULL, NULL, 0, 0, 0, 0, SQL_SMALLINT, TRL_NULL, 10},
    {"FLOAT", SQL_FLOAT, 15, NULL, NULL, "precision", 0, 0, TRL_NULL, TRL_NULL, SQL_FLOAT, TRL_NULL, 10},
    {"REAL", SQL_REAL, 7, NULL, NULL, NULL, 0, 0, TRL_NULL, TRL_NULL, SQL_REAL, TRL_NULL, 10},
    {"DOUBLE PRECISION", SQL_DOUBLE, 15, NULL, NULL, NULL, 0, 0, TRL_NULL, TRL_NULL, SQL_DOUBLE, TRL_NULL, 10},
    {
        "CHARACTER VARYING",
        SQL_VARCHAR,
        TRL_LENGTH_MOST,
        "'",
        "'",
        "length",
        1,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_VARCHAR,
        TRL_NULL,
        TRL_NULL,
    },
    {
        "DATE",
        SQL_TYPE_DATE,
        10,
        "DATE '",
        "'",
        NULL,
        0,
        TRL_NULL,
        TRL_NULL,
        TRL_NULL,
        SQL_DATETIME,
        SQL_CODE_DATE,
        TRL_NULL,
    },
    {"TIME", SQL_TYPE_TIME, 15, "TIME '", "'", "precision", 0, TRL_NULL, 0, 6, SQL_DATETIME, SQL_CODE_TIME, TRL_NULL},
    {
        "TIMESTAMP",
        SQL_TYPE_TIMESTAMP,
        26,
        "TIMESTAMP '",
        "'",
        "precision",
        0,
        TRL_NULL,
        0,
        6,
        SQL_DATETIME,
        SQL_CODE_TIMESTAMP,
        TRL_NULL,
    },
};

/* Appends a number of a row, or a typed null. */
static void number_put(trl_odbc_sql *sql, long number, const char *type) {
    if (number == TRL_NULL) {
        trl_odbc_sql_put(sql, "CAST(NULL AS ");
        trl_odbc_sql_put(sql, type);
        trl_odbc_sql_put(sql, ")");
        return;
    }
    char text[TRL_NUMBER_BYTES];
    trl_format(text, sizeof text, "%ld", number);
    trl_odbc_sql_put(sql, text);
}

/* Appends a text of a row, or a typed null. */
static void text_put(trl_odbc_sql *sql, const char *text) {
    if (text == NULL) {
        trl_odbc_sql_put(sql, "CAST(NULL AS VARCHAR(128))");
    } else {
        trl_odbc_sql_literal(sql, text);
    }
}

/* Appends a type's row of VALUES. */
static void row_put(trl_odbc_sql *sql, const type_row *r) {
    trl_odbc_sql_put(sql, "(");
    text_put(sql, r->name);
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->data_type, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->size, "INTEGER");
    trl_odbc_sql_put(sql, ", ");
    text_put(sql, r->prefix);
    trl_odbc_sql_put(sql, ", ");
    text_put(sql, r->suffix);
    trl_odbc_sql_put(sql, ", ");
    text_put(sql, r->params);
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->case_sensitive, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->is_unsigned, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->min_scale, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->max_scale, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->sql_data_type, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->sub, "SMALLINT");
    trl_odbc_sql_put(sql, ", ");
    number_put(sql, r->radix, "INTEGER");
    trl_odbc_sql_put(sql, ")");
}

/* ODBC 3's type of ODBC 2's datetime types, others as they are. */
static long concise_of(SQLSMALLINT type) {
    switch (type) {
    case SQL_DATE:
        return SQL_TYPE_DATE;
    case SQL_TIME:
        return SQL_TYPE_TIME;
    case SQL_TIMESTAMP:
        return SQL_TYPE_TIMESTAMP;
    default:
        return type;
    }
}

SQLRETURN trl_odbc_type_info(trl_odbc_stmt *stmt, SQLSMALLINT type) {
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql,
                     "SELECT CAST(N AS VARCHAR(128)) AS TYPE_NAME, CAST(D AS SMALLINT) AS DATA_TYPE, CAST(S AS "
                     "INTEGER) AS COLUMN_SIZE, CAST(P AS VARCHAR(128)) AS LITERAL_PREFIX, CAST(X AS VARCHAR(128)) "
                     "AS LITERAL_SUFFIX, CAST(C AS VARCHAR(128)) AS CREATE_PARAMS, CAST(1 AS SMALLINT) AS "
                     "NULLABLE, CAST(CS AS SMALLINT) AS CASE_SENSITIVE, CAST(3 AS SMALLINT) AS SEARCHABLE, "
                     "CAST(U AS SMALLINT) AS UNSIGNED_ATTRIBUTE, CAST(0 AS SMALLINT) AS FIXED_PREC_SCALE, CAST(0 "
                     "AS SMALLINT) AS AUTO_UNIQUE_VALUE, CAST(N AS VARCHAR(128)) AS LOCAL_TYPE_NAME, CAST(MI AS "
                     "SMALLINT) AS MINIMUM_SCALE, CAST(MA AS SMALLINT) AS MAXIMUM_SCALE, CAST(SD AS SMALLINT) "
                     "AS SQL_DATA_TYPE, CAST(SB AS SMALLINT) AS SQL_DATETIME_SUB, CAST(R AS INTEGER) AS "
                     "NUM_PREC_RADIX, CAST(NULL AS SMALLINT) AS INTERVAL_PRECISION FROM (VALUES ");
    for (size_t i = 0U; i < sizeof rows / sizeof rows[0]; ++i) {
        trl_odbc_sql_put(&sql, i == 0U ? "" : ", ");
        row_put(&sql, &rows[i]);
    }
    trl_odbc_sql_put(&sql, ") AS TYPES (N, D, S, P, X, C, CS, U, MI, MA, SD, SB, R)");
    if (type != SQL_ALL_TYPES) {
        trl_odbc_sql_put(&sql, " WHERE D = ");
        number_put(&sql, concise_of(type), "SMALLINT");
    }
    trl_odbc_sql_put(&sql, " ORDER BY DATA_TYPE, TYPE_NAME");
    return trl_odbc_catalog_run(stmt, &sql);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetTypeInfo(SQLHSTMT handle, SQLSMALLINT type) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : trl_odbc_type_info(stmt, type);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLPrimaryKeys(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                 SQLSMALLINT table_length) {
    SQLCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_primary_keys);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLForeignKeys(SQLHSTMT handle, SQLCHAR *pk_catalog, SQLSMALLINT pk_catalog_length,
                                                 SQLCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLCHAR *pk_table,
                                                 SQLSMALLINT pk_table_length, SQLCHAR *fk_catalog,
                                                 SQLSMALLINT fk_catalog_length, SQLCHAR *fk_schema,
                                                 SQLSMALLINT fk_schema_length, SQLCHAR *fk_table,
                                                 SQLSMALLINT fk_table_length) {
    SQLCHAR *const given[] = {pk_catalog, pk_schema, pk_table, fk_catalog, fk_schema, fk_table};
    const SQLSMALLINT lengths[] = {
        pk_catalog_length, pk_schema_length, pk_table_length, fk_catalog_length, fk_schema_length, fk_table_length,
    };
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_foreign_keys);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLStatistics(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved) {
    (void)unique;
    (void)reserved;
    SQLCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_statistics);
}

/* SQLSpecialColumns' best row identifier, the one kind it lists. */
static SQLRETURN best_rowid(trl_odbc_stmt *stmt, const char *const *names) {
    return trl_odbc_special_columns(stmt, SQL_BEST_ROWID, names);
}

/* SQLSpecialColumns' columns changed as a row is, of which there are none. */
static SQLRETURN rowver(trl_odbc_stmt *stmt, const char *const *names) {
    return trl_odbc_special_columns(stmt, SQL_ROWVER, names);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSpecialColumns(SQLHSTMT handle, SQLUSMALLINT type, SQLCHAR *catalog,
                                                    SQLSMALLINT catalog_length, SQLCHAR *schema,
                                                    SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length,
                                                    SQLUSMALLINT scope, SQLUSMALLINT nullable) {
    (void)scope;
    (void)nullable;
    SQLCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0],
                               type == SQL_BEST_ROWID ? best_rowid : rowver);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLProcedures(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                                SQLSMALLINT procedure_length) {
    SQLCHAR *const given[] = {catalog, schema, procedure};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, procedure_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_procedures);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLProcedureColumns(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                      SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                                      SQLSMALLINT procedure_length, SQLCHAR *column,
                                                      SQLSMALLINT column_length) {
    SQLCHAR *const given[] = {catalog, schema, procedure, column};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, procedure_length, column_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_procedure_columns);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLTablePrivileges(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                     SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                     SQLSMALLINT table_length) {
    SQLCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_table_privileges);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLColumnPrivileges(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                      SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                      SQLSMALLINT table_length, SQLCHAR *column,
                                                      SQLSMALLINT column_length) {
    SQLCHAR *const given[] = {catalog, schema, table, column};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length, column_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_column_privileges);
}
