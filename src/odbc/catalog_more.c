/**
 * @file catalog_more.c
 * @brief The catalog functions of keys, statistics, procedures,
 * privileges and types (RULES.md D2), each a query of the Information
 * Schema with the columns ODBC names: SQLPrimaryKeys and SQLForeignKeys
 * of KEY_COLUMN_USAGE and REFERENTIAL_CONSTRAINTS; SQLStatistics and
 * SQLSpecialColumns of a table's unique keys, as the protocol tells no
 * index; SQLProcedures of ROUTINES; SQLTablePrivileges and
 * SQLColumnPrivileges of their views; SQLGetTypeInfo of the types the
 * driver describes (`types.c`). SQLProcedureColumns gives no row.
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

/* Appends a number. */
static void number_put(trl_odbc_sql *sql, long number) {
    char text[TRL_NUMBER_BYTES];
    trl_format(text, sizeof text, "%ld", number);
    trl_odbc_sql_put(sql, text);
}

/* Appends a CASE of a referential action's rule as ODBC's code. */
static void rule_put(trl_odbc_sql *sql, const char *rule) {
    static const struct {
        const char *name;
        long code;
    } rules[] = {
        {"CASCADE", SQL_CASCADE},     {"RESTRICT", SQL_RESTRICT},       {"SET NULL", SQL_SET_NULL},
        {"NO ACTION", SQL_NO_ACTION}, {"SET DEFAULT", SQL_SET_DEFAULT},
    };
    trl_odbc_sql_put(sql, "CAST(CASE R.");
    trl_odbc_sql_put(sql, rule);
    for (size_t i = 0U; i < sizeof rules / sizeof rules[0]; ++i) {
        trl_odbc_sql_put(sql, " WHEN ");
        trl_odbc_sql_literal(sql, rules[i].name);
        trl_odbc_sql_put(sql, " THEN ");
        number_put(sql, rules[i].code);
    }
    trl_odbc_sql_put(sql, " END AS SMALLINT) AS ");
    trl_odbc_sql_put(sql, rule);
}

/* A table's unique keys' columns, their constraint's type as `kinds`
   says, as K joined to its constraint T, and to its column C if `columns`. */
static void keys_from(trl_odbc_sql *sql, const char *kinds, bool columns) {
    trl_odbc_sql_put(sql, " FROM INFORMATION_SCHEMA.KEY_COLUMN_USAGE AS K JOIN INFORMATION_SCHEMA.TABLE_CONSTRAINTS "
                          "AS T ON T.CONSTRAINT_CATALOG = K.CONSTRAINT_CATALOG AND T.CONSTRAINT_SCHEMA = "
                          "K.CONSTRAINT_SCHEMA AND T.CONSTRAINT_NAME = K.CONSTRAINT_NAME");
    if (columns) {
        trl_odbc_sql_put(sql, " JOIN INFORMATION_SCHEMA.COLUMNS AS C ON C.TABLE_CATALOG = K.TABLE_CATALOG AND "
                              "C.TABLE_SCHEMA = K.TABLE_SCHEMA AND C.TABLE_NAME = K.TABLE_NAME AND C.COLUMN_NAME = "
                              "K.COLUMN_NAME");
    }
    trl_odbc_sql_put(sql, " WHERE T.CONSTRAINT_TYPE ");
    trl_odbc_sql_put(sql, kinds);
}

/* A table named whole, as SQLPrimaryKeys and the rest take it: its name required. */
static bool table_given(trl_odbc_stmt *stmt, const char *table) {
    if (table != NULL) {
        return true;
    }
    trl_odbc_clear(&stmt->diag);
    (void)trl_odbc_fail(&stmt->diag, "HY009", "Invalid use of null pointer: the table's name is required");
    return false;
}

SQLRETURN trl_odbc_primary_keys(trl_odbc_stmt *stmt, const char *const *names) {
    if (!table_given(stmt, names[TRL_NAME_TABLE])) {
        return SQL_ERROR;
    }
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT K.TABLE_CATALOG AS TABLE_CAT, K.TABLE_SCHEMA AS TABLE_SCHEM, K.TABLE_NAME, "
                           "K.COLUMN_NAME, CAST(K.ORDINAL_POSITION AS SMALLINT) AS KEY_SEQ, K.CONSTRAINT_NAME AS "
                           "PK_NAME");
    keys_from(&sql, "= 'PRIMARY KEY'", false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_SCHEMA", names[TRL_NAME_SCHEMA], false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_NAME", names[TRL_NAME_TABLE], false);
    trl_odbc_sql_put(&sql, " ORDER BY TABLE_CAT, TABLE_SCHEM, TABLE_NAME, KEY_SEQ");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_foreign_keys(trl_odbc_stmt *stmt, const char *const *names) {
    if (names[TRL_NAME_TABLE] == NULL && names[TRL_NAME_FK_TABLE] == NULL) {
        return table_given(stmt, NULL) ? SQL_SUCCESS : SQL_ERROR;
    }
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT P.TABLE_CATALOG AS PKTABLE_CAT, P.TABLE_SCHEMA AS PKTABLE_SCHEM, P.TABLE_NAME AS "
                           "PKTABLE_NAME, P.COLUMN_NAME AS PKCOLUMN_NAME, F.TABLE_CATALOG AS FKTABLE_CAT, "
                           "F.TABLE_SCHEMA AS FKTABLE_SCHEM, F.TABLE_NAME AS FKTABLE_NAME, F.COLUMN_NAME AS "
                           "FKCOLUMN_NAME, CAST(F.ORDINAL_POSITION AS SMALLINT) AS KEY_SEQ, ");
    rule_put(&sql, "UPDATE_RULE");
    trl_odbc_sql_put(&sql, ", ");
    rule_put(&sql, "DELETE_RULE");
    trl_odbc_sql_put(&sql, ", F.CONSTRAINT_NAME AS FK_NAME, P.CONSTRAINT_NAME AS PK_NAME, CAST(");
    number_put(&sql, SQL_NOT_DEFERRABLE);
    trl_odbc_sql_put(&sql,
                     " AS SMALLINT) AS DEFERRABILITY FROM INFORMATION_SCHEMA.REFERENTIAL_CONSTRAINTS AS R JOIN "
                     "INFORMATION_SCHEMA.KEY_COLUMN_USAGE AS F ON F.CONSTRAINT_CATALOG = R.CONSTRAINT_CATALOG AND "
                     "F.CONSTRAINT_SCHEMA = R.CONSTRAINT_SCHEMA AND F.CONSTRAINT_NAME = R.CONSTRAINT_NAME JOIN "
                     "INFORMATION_SCHEMA.KEY_COLUMN_USAGE AS P ON P.CONSTRAINT_CATALOG = "
                     "R.UNIQUE_CONSTRAINT_CATALOG AND P.CONSTRAINT_SCHEMA = R.UNIQUE_CONSTRAINT_SCHEMA AND "
                     "P.CONSTRAINT_NAME = R.UNIQUE_CONSTRAINT_NAME AND P.ORDINAL_POSITION = "
                     "F.POSITION_IN_UNIQUE_CONSTRAINT WHERE 1 = 1");
    trl_odbc_sql_filter(&sql, stmt, "P.TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "P.TABLE_SCHEMA", names[TRL_NAME_SCHEMA], false);
    trl_odbc_sql_filter(&sql, stmt, "P.TABLE_NAME", names[TRL_NAME_TABLE], false);
    trl_odbc_sql_filter(&sql, stmt, "F.TABLE_CATALOG", names[TRL_NAME_FK_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "F.TABLE_SCHEMA", names[TRL_NAME_FK_SCHEMA], false);
    trl_odbc_sql_filter(&sql, stmt, "F.TABLE_NAME", names[TRL_NAME_FK_TABLE], false);
    trl_odbc_sql_put(&sql, names[TRL_NAME_TABLE] != NULL
                               ? " ORDER BY FKTABLE_CAT, FKTABLE_SCHEM, FKTABLE_NAME, KEY_SEQ"
                               : " ORDER BY PKTABLE_CAT, PKTABLE_SCHEM, PKTABLE_NAME, KEY_SEQ");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_statistics(trl_odbc_stmt *stmt, const char *const *names) {
    if (!table_given(stmt, names[TRL_NAME_TABLE])) {
        return SQL_ERROR;
    }
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT K.TABLE_CATALOG AS TABLE_CAT, K.TABLE_SCHEMA AS TABLE_SCHEM, K.TABLE_NAME, "
                           "CAST(0 AS SMALLINT) AS NON_UNIQUE, K.CONSTRAINT_SCHEMA AS INDEX_QUALIFIER, "
                           "K.CONSTRAINT_NAME AS INDEX_NAME, CAST(");
    number_put(&sql, SQL_INDEX_OTHER);
    trl_odbc_sql_put(&sql, " AS SMALLINT) AS TYPE, CAST(K.ORDINAL_POSITION AS SMALLINT) AS ORDINAL_POSITION, "
                           "K.COLUMN_NAME, CAST(NULL AS CHARACTER(1)) AS ASC_OR_DESC, CAST(NULL AS INTEGER) AS "
                           "CARDINALITY, CAST(NULL AS INTEGER) AS PAGES, CAST(NULL AS VARCHAR(128)) AS "
                           "FILTER_CONDITION");
    keys_from(&sql, "IN ('PRIMARY KEY', 'UNIQUE')", false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_SCHEMA", names[TRL_NAME_SCHEMA], false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_NAME", names[TRL_NAME_TABLE], false);
    trl_odbc_sql_put(&sql, " ORDER BY NON_UNIQUE, TYPE, INDEX_QUALIFIER, INDEX_NAME, ORDINAL_POSITION");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_special_columns(trl_odbc_stmt *stmt, SQLUSMALLINT type, const char *const *names) {
    if (!table_given(stmt, names[TRL_NAME_TABLE])) {
        return SQL_ERROR;
    }
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT CAST(");
    number_put(&sql, SQL_SCOPE_SESSION);
    trl_odbc_sql_put(&sql, " AS SMALLINT) AS SCOPE, C.COLUMN_NAME, ");
    trl_odbc_sql_types(&sql, "C");
    trl_odbc_sql_put(&sql, ", CAST(");
    number_put(&sql, SQL_PC_NOT_PSEUDO);
    trl_odbc_sql_put(&sql, " AS SMALLINT) AS PSEUDO_COLUMN");
    keys_from(&sql, "= 'PRIMARY KEY'", true);
    /* The best row identifier is the primary key; no column changes as a
       row is updated but by the application. */
    trl_odbc_sql_put(&sql, type == SQL_BEST_ROWID ? "" : " AND 1 = 0");
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_SCHEMA", names[TRL_NAME_SCHEMA], false);
    trl_odbc_sql_filter(&sql, stmt, "K.TABLE_NAME", names[TRL_NAME_TABLE], false);
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_procedures(trl_odbc_stmt *stmt, const char *const *names) {
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT ROUTINE_CATALOG AS PROCEDURE_CAT, ROUTINE_SCHEMA AS PROCEDURE_SCHEM, ROUTINE_NAME "
                           "AS PROCEDURE_NAME, CAST(NULL AS INTEGER) AS NUM_INPUT_PARAMS, CAST(NULL AS INTEGER) AS "
                           "NUM_OUTPUT_PARAMS, CAST(NULL AS INTEGER) AS NUM_RESULT_SETS, CAST(NULL AS VARCHAR(254)) AS "
                           "REMARKS, CAST(CASE ROUTINE_TYPE WHEN 'PROCEDURE' THEN ");
    number_put(&sql, SQL_PT_PROCEDURE);
    trl_odbc_sql_put(&sql, " ELSE ");
    number_put(&sql, SQL_PT_FUNCTION);
    trl_odbc_sql_put(&sql, " END AS SMALLINT) AS PROCEDURE_TYPE FROM INFORMATION_SCHEMA.ROUTINES WHERE 1 = 1");
    trl_odbc_sql_filter(&sql, stmt, "ROUTINE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "ROUTINE_SCHEMA", names[TRL_NAME_SCHEMA], true);
    trl_odbc_sql_filter(&sql, stmt, "ROUTINE_NAME", names[TRL_NAME_TABLE], true);
    trl_odbc_sql_put(&sql, " ORDER BY PROCEDURE_CAT, PROCEDURE_SCHEM, PROCEDURE_NAME");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_procedure_columns(trl_odbc_stmt *stmt, const char *const *names) {
    (void)names;
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT CAST(NULL AS VARCHAR(128)) AS PROCEDURE_CAT, CAST(NULL AS VARCHAR(128)) AS "
                           "PROCEDURE_SCHEM, CAST(NULL AS VARCHAR(128)) AS PROCEDURE_NAME, CAST(NULL AS VARCHAR(128)) "
                           "AS COLUMN_NAME, CAST(NULL AS SMALLINT) AS COLUMN_TYPE, CAST(NULL AS SMALLINT) AS "
                           "DATA_TYPE, CAST(NULL AS VARCHAR(128)) AS TYPE_NAME, CAST(NULL AS INTEGER) AS COLUMN_SIZE, "
                           "CAST(NULL AS INTEGER) AS BUFFER_LENGTH, CAST(NULL AS SMALLINT) AS DECIMAL_DIGITS, "
                           "CAST(NULL AS SMALLINT) AS NUM_PREC_RADIX, CAST(NULL AS SMALLINT) AS NULLABLE, CAST(NULL "
                           "AS VARCHAR(254)) AS REMARKS, CAST(NULL AS VARCHAR(254)) AS COLUMN_DEF, CAST(NULL AS "
                           "SMALLINT) AS SQL_DATA_TYPE, CAST(NULL AS SMALLINT) AS SQL_DATETIME_SUB, CAST(NULL AS "
                           "INTEGER) AS CHAR_OCTET_LENGTH, CAST(NULL AS INTEGER) AS ORDINAL_POSITION, CAST(NULL AS "
                           "VARCHAR(3)) AS IS_NULLABLE FROM INFORMATION_SCHEMA.INFORMATION_SCHEMA_CATALOG_NAME WHERE "
                           "1 = 0");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_table_privileges(trl_odbc_stmt *stmt, const char *const *names) {
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql,
                     "SELECT TABLE_CATALOG AS TABLE_CAT, TABLE_SCHEMA AS TABLE_SCHEM, TABLE_NAME, GRANTOR, "
                     "GRANTEE, PRIVILEGE_TYPE AS PRIVILEGE, IS_GRANTABLE FROM INFORMATION_SCHEMA.TABLE_PRIVILEGES "
                     "WHERE 1 = 1");
    trl_odbc_sql_filter(&sql, stmt, "TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "TABLE_SCHEMA", names[TRL_NAME_SCHEMA], true);
    trl_odbc_sql_filter(&sql, stmt, "TABLE_NAME", names[TRL_NAME_TABLE], true);
    trl_odbc_sql_put(&sql, " ORDER BY TABLE_CAT, TABLE_SCHEM, TABLE_NAME, PRIVILEGE");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_column_privileges(trl_odbc_stmt *stmt, const char *const *names) {
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT TABLE_CATALOG AS TABLE_CAT, TABLE_SCHEMA AS TABLE_SCHEM, TABLE_NAME, COLUMN_NAME, "
                           "GRANTOR, GRANTEE, PRIVILEGE_TYPE AS PRIVILEGE, IS_GRANTABLE FROM "
                           "INFORMATION_SCHEMA.COLUMN_PRIVILEGES WHERE 1 = 1");
    trl_odbc_sql_filter(&sql, stmt, "TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "TABLE_SCHEMA", names[TRL_NAME_SCHEMA], false);
    trl_odbc_sql_filter(&sql, stmt, "TABLE_NAME", names[TRL_NAME_TABLE], false);
    trl_odbc_sql_filter(&sql, stmt, "COLUMN_NAME", names[TRL_NAME_COLUMN], true);
    trl_odbc_sql_put(&sql, " ORDER BY TABLE_CAT, TABLE_SCHEM, TABLE_NAME, COLUMN_NAME, PRIVILEGE");
    return trl_odbc_catalog_run(stmt, &sql);
}
