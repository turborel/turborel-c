/**
 * @file wide_catalog.c
 * @brief The catalog functions' Unicode entry points (RULES.md D2): each
 * name given as SQLWCHAR, turned to UTF-8, then the ANSI function's work.
 */
#include "odbc/catalog.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>

/* Runs `work` of the statement `handle` with `count` names given as SQLWCHAR. */
static SQLRETURN with_wide_names(SQLHSTMT handle, SQLWCHAR *const *given, const SQLSMALLINT *lengths, size_t count,
                                 SQLRETURN (*work)(trl_odbc_stmt *, const char *const *)) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *names[TRL_CATALOG_NAMES_MOST] = {NULL};
    if (!trl_odbc_wide_names(given, lengths, count, names)) {
        trl_odbc_names_free(names, count);
        trl_odbc_clear(&stmt->diag);
        return trl_odbc_no_memory(&stmt->diag);
    }
    SQLRETURN returned = work(stmt, (const char *const *)names);
    trl_odbc_names_free(names, count);
    return returned;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLTablesW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                             SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                             SQLSMALLINT table_length, SQLWCHAR *types, SQLSMALLINT types_length) {
    SQLWCHAR *const given[] = {catalog, schema, table, types};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length, types_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_tables);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLColumnsW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                              SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                              SQLSMALLINT table_length, SQLWCHAR *column, SQLSMALLINT column_length) {
    SQLWCHAR *const given[] = {catalog, schema, table, column};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length, column_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_columns);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLPrimaryKeysW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                  SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                  SQLSMALLINT table_length) {
    SQLWCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_primary_keys);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLForeignKeysW(SQLHSTMT handle, SQLWCHAR *pk_catalog, SQLSMALLINT pk_catalog_length,
                                                  SQLWCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLWCHAR *pk_table,
                                                  SQLSMALLINT pk_table_length, SQLWCHAR *fk_catalog,
                                                  SQLSMALLINT fk_catalog_length, SQLWCHAR *fk_schema,
                                                  SQLSMALLINT fk_schema_length, SQLWCHAR *fk_table,
                                                  SQLSMALLINT fk_table_length) {
    SQLWCHAR *const given[] = {pk_catalog, pk_schema, pk_table, fk_catalog, fk_schema, fk_table};
    const SQLSMALLINT lengths[] = {
        pk_catalog_length, pk_schema_length, pk_table_length, fk_catalog_length, fk_schema_length, fk_table_length,
    };
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_foreign_keys);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLStatisticsW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                 SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved) {
    (void)unique;
    (void)reserved;
    SQLWCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_statistics);
}

/* SQLSpecialColumnsW's best row identifier. */
static SQLRETURN best_rowid(trl_odbc_stmt *stmt, const char *const *names) {
    return trl_odbc_special_columns(stmt, SQL_BEST_ROWID, names);
}

/* SQLSpecialColumnsW's columns changed as a row is, of which there are none. */
static SQLRETURN rowver(trl_odbc_stmt *stmt, const char *const *names) {
    return trl_odbc_special_columns(stmt, SQL_ROWVER, names);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSpecialColumnsW(SQLHSTMT handle, SQLUSMALLINT type, SQLWCHAR *catalog,
                                                     SQLSMALLINT catalog_length, SQLWCHAR *schema,
                                                     SQLSMALLINT schema_length, SQLWCHAR *table,
                                                     SQLSMALLINT table_length, SQLUSMALLINT scope,
                                                     SQLUSMALLINT nullable) {
    (void)scope;
    (void)nullable;
    SQLWCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0],
                           type == SQL_BEST_ROWID ? best_rowid : rowver);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLProceduresW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure,
                                                 SQLSMALLINT procedure_length) {
    SQLWCHAR *const given[] = {catalog, schema, procedure};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, procedure_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_procedures);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLProcedureColumnsW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                       SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure,
                                                       SQLSMALLINT procedure_length, SQLWCHAR *column,
                                                       SQLSMALLINT column_length) {
    SQLWCHAR *const given[] = {catalog, schema, procedure, column};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, procedure_length, column_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_procedure_columns);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLTablePrivilegesW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                      SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                      SQLSMALLINT table_length) {
    SQLWCHAR *const given[] = {catalog, schema, table};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_table_privileges);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLColumnPrivilegesW(SQLHSTMT handle, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                       SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                       SQLSMALLINT table_length, SQLWCHAR *column,
                                                       SQLSMALLINT column_length) {
    SQLWCHAR *const given[] = {catalog, schema, table, column};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length, column_length};
    return with_wide_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_column_privileges);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetTypeInfoW(SQLHSTMT handle, SQLSMALLINT type) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : trl_odbc_type_info(stmt, type);
}
