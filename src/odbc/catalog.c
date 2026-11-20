/**
 * @file catalog.c
 * @brief The catalog functions' queries (RULES.md D2): each a query of
 * Turborel's Information Schema whose result has the columns ODBC's
 * reference names, in its order and of its types, run as the statement's
 * cursor; so a table, a column or a key is listed when the user may see
 * it, as the Information Schema lists it. SQLTables and SQLColumns here;
 * keys, statistics, procedures, privileges and types in
 * `catalog_more.c`.
 */
#include "odbc/catalog.h"

#include "format.h"
#include "memory.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* What a query's buffer first holds, and room for a number. */
#define TRL_SQL_FIRST 512U
#define TRL_SQL_GROWTH 2U
#define TRL_NUMBER_BYTES 24U

void trl_odbc_sql_put(trl_odbc_sql *sql, const char *text) {
    size_t length = strlen(text);
    if (sql->failed) {
        return;
    }
    if (sql->length + length + 1U > sql->capacity) {
        size_t capacity = sql->capacity == 0U ? TRL_SQL_FIRST : sql->capacity;
        while (capacity < sql->length + length + 1U) {
            capacity *= TRL_SQL_GROWTH;
        }
        char *grown = trl_realloc(sql->data, capacity);
        if (grown == NULL) {
            sql->failed = true;
            return;
        }
        sql->data = grown;
        sql->capacity = capacity;
    }
    memcpy(sql->data + sql->length, text, length + 1U);
    sql->length += length;
}

void trl_odbc_sql_literal(trl_odbc_sql *sql, const char *value) {
    trl_odbc_sql_put(sql, "'");
    char one[sizeof "x"] = "x";
    for (const char *at = value; *at != '\0'; ++at) {
        one[0] = *at;
        trl_odbc_sql_put(sql, *at == '\'' ? "''" : one);
    }
    trl_odbc_sql_put(sql, "'");
}

/* An argument as an identifier: its body if quoted, a quote in it written
   twice once; else in upper case. A new string; null if memory cannot be had. */
static char *identifier_of(const char *arg) {
    size_t length = strlen(arg);
    char *out = trl_malloc(length + 1U);
    if (out == NULL) {
        return NULL;
    }
    size_t k = 0U;
    if (length >= sizeof "\"\"" - 1U && arg[0] == '"' && arg[length - 1U] == '"') {
        for (size_t i = 1U; i + 1U < length; ++i) {
            out[k++] = arg[i];
            i += arg[i] == '"' && arg[i + 1U] == '"' ? 1U : 0U;
        }
    } else {
        for (size_t i = 0U; i < length; ++i) {
            out[k++] = (char)toupper((unsigned char)arg[i]);
        }
    }
    out[k] = '\0';
    return out;
}

void trl_odbc_sql_filter(trl_odbc_sql *sql, const trl_odbc_stmt *stmt, const char *column, const char *arg,
                         bool pattern) {
    if (arg == NULL) {
        return;
    }
    trl_odbc_sql_put(sql, " AND ");
    trl_odbc_sql_put(sql, column);
    if (stmt->metadata_id) {
        char *name = identifier_of(arg);
        if (name == NULL) {
            sql->failed = true;
            return;
        }
        trl_odbc_sql_put(sql, " = ");
        trl_odbc_sql_literal(sql, name);
        free(name);
        return;
    }
    trl_odbc_sql_put(sql, pattern ? " LIKE " : " = ");
    trl_odbc_sql_literal(sql, arg);
    if (pattern) {
        trl_odbc_sql_put(sql, " ESCAPE '\\'");
    }
}

/* A type of the Information Schema's DATA_TYPE and how SQLColumns gives it. */
typedef struct column_type {
    const char *name;
    const char *size;
    const char *octets;
    const char *digits;
    int code;
    bool numeric;
} column_type;

static const column_type types[] = {
    {"SMALLINT", "5", "2", "0", SQL_SMALLINT, true},
    {"INTEGER", "10", "4", "0", SQL_INTEGER, true},
    {"BIGINT", "19", "8", "0", SQL_BIGINT, true},
    {"NUMERIC", "N.NUMERIC_PRECISION", "N.NUMERIC_PRECISION + 2", "N.NUMERIC_SCALE", SQL_NUMERIC, true},
    {"DECIMAL", "N.NUMERIC_PRECISION", "N.NUMERIC_PRECISION + 2", "N.NUMERIC_SCALE", SQL_DECIMAL, true},
    {"REAL", "7", "4", NULL, SQL_REAL, true},
    {"DOUBLE PRECISION", "15", "8", NULL, SQL_DOUBLE, true},
    {"FLOAT", "15", "8", NULL, SQL_FLOAT, true},
    {"CHARACTER", "C", "N.CHARACTER_OCTET_LENGTH", NULL, SQL_CHAR, false},
    {"CHARACTER VARYING", "C", "N.CHARACTER_OCTET_LENGTH", NULL, SQL_VARCHAR, false},
    {"CHARACTER LARGE OBJECT", "C", "N.CHARACTER_OCTET_LENGTH", NULL, SQL_LONGVARCHAR, false},
    {"BINARY", "C", "C", NULL, SQL_BINARY, false},
    {"BINARY VARYING", "C", "C", NULL, SQL_VARBINARY, false},
    {"BINARY LARGE OBJECT", "C", "C", NULL, SQL_LONGVARBINARY, false},
    {"BOOLEAN", "1", "1", NULL, SQL_BIT, false},
    {"DATE", "10", "6", NULL, SQL_TYPE_DATE, false},
    {
        "TIME",
        "CASE WHEN N.DATETIME_PRECISION > 0 THEN 9 + N.DATETIME_PRECISION ELSE 8 END",
        "6",
        "N.DATETIME_PRECISION",
        SQL_TYPE_TIME,
        false,
    },
    {
        "TIMESTAMP",
        "CASE WHEN N.DATETIME_PRECISION > 0 THEN 20 + N.DATETIME_PRECISION ELSE 19 END",
        "16",
        "N.DATETIME_PRECISION",
        SQL_TYPE_TIMESTAMP,
        false,
    },
    {"UUID", "36", "16", NULL, SQL_GUID, false},
};

/* Appends `expression` with the COLUMNS row's alias for N, and its
   characters for C. */
static void expression_put(trl_odbc_sql *sql, const char *expression, const char *alias) {
    char chars[TRL_SQL_FIRST];
    trl_format(chars, sizeof chars, "COALESCE(%s.CHARACTER_MAXIMUM_LENGTH, %s.CHARACTER_OCTET_LENGTH)", alias, alias);
    char one[sizeof "x"] = "x";
    for (const char *at = expression; *at != '\0'; ++at) {
        bool word_start = at == expression || !isalnum((unsigned char)at[-1]);
        bool alone = !isalnum((unsigned char)at[1]) && at[1] != '.' && at[1] != '_';
        if (*at == 'N' && at[1] == '.' && word_start) {
            trl_odbc_sql_put(sql, alias);
        } else if (*at == 'C' && word_start && alone) {
            trl_odbc_sql_put(sql, chars);
        } else {
            one[0] = *at;
            trl_odbc_sql_put(sql, one);
        }
    }
}

/* What of a type a CASE of its name gives. */
enum type_field {
    FIELD_CODE = 0,
    FIELD_SIZE,
    FIELD_OCTETS,
    FIELD_DIGITS,
    FIELD_RADIX,
};

/* Appends a CASE of the type's name giving each type's `field`, `otherwise` else. */
static void case_put(trl_odbc_sql *sql, const char *alias, size_t field, const char *otherwise) {
    trl_odbc_sql_put(sql, "CASE ");
    trl_odbc_sql_put(sql, alias);
    trl_odbc_sql_put(sql, ".DATA_TYPE");
    for (size_t i = 0U; i < sizeof types / sizeof types[0]; ++i) {
        char code[TRL_NUMBER_BYTES];
        trl_format(code, sizeof code, "%d", types[i].code);
        const char *fields[] = {code, types[i].size, types[i].octets, types[i].digits, types[i].numeric ? "10" : NULL};
        if (fields[field] == NULL) {
            continue;
        }
        trl_odbc_sql_put(sql, " WHEN ");
        trl_odbc_sql_literal(sql, types[i].name);
        trl_odbc_sql_put(sql, " THEN ");
        expression_put(sql, fields[field], alias);
    }
    if (otherwise != NULL) {
        trl_odbc_sql_put(sql, " ELSE ");
        trl_odbc_sql_put(sql, otherwise);
    }
    trl_odbc_sql_put(sql, " END");
}

void trl_odbc_sql_types(trl_odbc_sql *sql, const char *alias) {
    char otherwise[TRL_NUMBER_BYTES];
    trl_format(otherwise, sizeof otherwise, "%d", SQL_VARCHAR);
    trl_odbc_sql_put(sql, "CAST(");
    case_put(sql, alias, FIELD_CODE, otherwise);
    trl_odbc_sql_put(sql, " AS SMALLINT) AS DATA_TYPE, ");
    trl_odbc_sql_put(sql, alias);
    trl_odbc_sql_put(sql, ".DATA_TYPE AS TYPE_NAME, CAST(");
    case_put(sql, alias, FIELD_SIZE, "255");
    trl_odbc_sql_put(sql, " AS INTEGER) AS COLUMN_SIZE, CAST(");
    case_put(sql, alias, FIELD_OCTETS, "255");
    trl_odbc_sql_put(sql, " AS INTEGER) AS BUFFER_LENGTH, CAST(");
    case_put(sql, alias, FIELD_DIGITS, NULL);
    trl_odbc_sql_put(sql, " AS SMALLINT) AS DECIMAL_DIGITS, CAST(");
    case_put(sql, alias, FIELD_RADIX, NULL);
    trl_odbc_sql_put(sql, " AS SMALLINT) AS NUM_PREC_RADIX");
}

SQLRETURN trl_odbc_catalog_run(trl_odbc_stmt *stmt, trl_odbc_sql *sql) {
    trl_odbc_clear(&stmt->diag);
    trl_odbc_close_cursor(stmt);
    trl_odbc_unprepare(stmt);
    if (sql->failed) {
        free(sql->data);
        return trl_odbc_no_memory(&stmt->diag);
    }
    trl_result *result = NULL;
    trl_conn *conn = stmt->dbc->conn;
    SQLRETURN ready = trl_odbc_modes_applied(stmt);
    if (ready != SQL_SUCCESS) {
        free(sql->data);
        sql->data = NULL;
        return ready;
    }
    trl_status ran = trl_query(conn, sql->data, &result);
    free(sql->data);
    sql->data = NULL;
    if (ran != TRL_OK) {
        return ran == TRL_NO_MEMORY ? trl_odbc_no_memory(&stmt->diag)
                                    : trl_odbc_failed(&stmt->diag, trl_conn_failure(conn));
    }
    stmt->result = result;
    stmt->row_count = (SQLLEN)trl_result_row_count(result);
    return SQL_SUCCESS;
}

/* Whether `name` is given and is `of`, as SQL_ALL_CATALOGS and the rest are. */
static bool is(const char *name, const char *of) {
    return name != NULL && strcmp(name, of) == 0;
}

/* Reads the next table type of a list at `*at`, quoted or not, into
   `type`, TABLE a base table's; false at the list's end. */
static bool type_next(const char **at, char *type, size_t capacity) {
    *at += strspn(*at, ", '");
    size_t length = strcspn(*at, ",'");
    const char *from = *at;
    *at += length;
    while (length > 0U && from[length - 1U] == ' ') {
        --length;
    }
    if (length == 0U) {
        return **at != '\0' && type_next(at, type, capacity);
    }
    trl_format(type, capacity, "%.*s", (int)length, from);
    if (strcmp(type, "TABLE") == 0) {
        trl_format(type, capacity, "%s", "BASE TABLE");
    }
    return true;
}

/* The table types `types` lists, quoted or not, comma-separated, as an IN
   list's insides: TABLE a base table, system-versioned or not; the first
   twice, as an IN list has two values at least. */
static void types_put(trl_odbc_sql *sql, const char *types_listed) {
    char head[TRL_SQL_FIRST] = "";
    char type[TRL_SQL_FIRST];
    const char *at = types_listed;
    while (type_next(&at, type, sizeof type)) {
        trl_odbc_sql_put(sql, head[0] == '\0' ? "" : ", ");
        trl_odbc_sql_literal(sql, type);
        trl_odbc_sql_put(sql, strcmp(type, "BASE TABLE") == 0 ? ", 'SYSTEM VERSIONED'" : "");
        if (head[0] == '\0') {
            trl_format(head, sizeof head, "%s", type);
        }
    }
    /* A list of no type is two empty ones, which no table's type is. */
    trl_odbc_sql_put(sql, head[0] == '\0' ? "'', " : ", ");
    trl_odbc_sql_literal(sql, head);
}

/* What SQLTables lists of a catalog, a schema or a table type alone. */
static SQLRETURN tables_alone(trl_odbc_stmt *stmt, const char *const *names, trl_odbc_sql *sql) {
    if (is(names[TRL_NAME_CATALOG], SQL_ALL_CATALOGS) && is(names[TRL_NAME_SCHEMA], "") &&
        is(names[TRL_NAME_TABLE], "")) {
        trl_odbc_sql_put(sql, "SELECT CATALOG_NAME AS TABLE_CAT, CAST(NULL AS VARCHAR(128)) AS TABLE_SCHEM, "
                              "CAST(NULL AS VARCHAR(128)) AS TABLE_NAME, CAST(NULL AS VARCHAR(128)) AS TABLE_TYPE, "
                              "CAST(NULL AS VARCHAR(254)) AS REMARKS FROM "
                              "INFORMATION_SCHEMA.INFORMATION_SCHEMA_CATALOG_NAME");
    } else if (is(names[TRL_NAME_SCHEMA], SQL_ALL_SCHEMAS) && is(names[TRL_NAME_CATALOG], "") &&
               is(names[TRL_NAME_TABLE], "")) {
        trl_odbc_sql_put(sql, "SELECT DISTINCT TABLE_CATALOG AS TABLE_CAT, TABLE_SCHEMA AS TABLE_SCHEM, "
                              "CAST(NULL AS VARCHAR(128)) AS TABLE_NAME, CAST(NULL AS VARCHAR(128)) AS TABLE_TYPE, "
                              "CAST(NULL AS VARCHAR(254)) AS REMARKS FROM INFORMATION_SCHEMA.TABLES ORDER BY "
                              "TABLE_SCHEM");
    } else if (is(names[TRL_NAME_COLUMN], SQL_ALL_TABLE_TYPES) && is(names[TRL_NAME_CATALOG], "") &&
               is(names[TRL_NAME_SCHEMA], "") && is(names[TRL_NAME_TABLE], "")) {
        trl_odbc_sql_put(sql, "SELECT CAST(NULL AS VARCHAR(128)) AS TABLE_CAT, CAST(NULL AS VARCHAR(128)) AS "
                              "TABLE_SCHEM, CAST(NULL AS VARCHAR(128)) AS TABLE_NAME, T AS TABLE_TYPE, CAST(NULL AS "
                              "VARCHAR(254)) AS REMARKS FROM (VALUES ('TABLE'), ('VIEW'), ('GLOBAL TEMPORARY'), "
                              "('LOCAL TEMPORARY')) AS TYPES (T)");
    } else {
        return SQL_NO_DATA;
    }
    return trl_odbc_catalog_run(stmt, sql);
}

SQLRETURN trl_odbc_tables(trl_odbc_stmt *stmt, const char *const *names) {
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    SQLRETURN alone = tables_alone(stmt, names, &sql);
    if (alone != SQL_NO_DATA) {
        return alone;
    }
    trl_odbc_sql_put(&sql, "SELECT TABLE_CATALOG AS TABLE_CAT, TABLE_SCHEMA AS TABLE_SCHEM, TABLE_NAME, CASE "
                           "TABLE_TYPE WHEN 'BASE TABLE' THEN 'TABLE' WHEN 'SYSTEM VERSIONED' THEN 'TABLE' ELSE "
                           "TABLE_TYPE END AS TABLE_TYPE, CAST(NULL AS VARCHAR(254)) AS REMARKS FROM "
                           "INFORMATION_SCHEMA.TABLES WHERE 1 = 1");
    trl_odbc_sql_filter(&sql, stmt, "TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "TABLE_SCHEMA", names[TRL_NAME_SCHEMA], true);
    trl_odbc_sql_filter(&sql, stmt, "TABLE_NAME", names[TRL_NAME_TABLE], true);
    if (names[TRL_NAME_COLUMN] != NULL && !is(names[TRL_NAME_COLUMN], SQL_ALL_TABLE_TYPES) &&
        names[TRL_NAME_COLUMN][0] != '\0') {
        trl_odbc_sql_put(&sql, " AND TABLE_TYPE IN (");
        types_put(&sql, names[TRL_NAME_COLUMN]);
        trl_odbc_sql_put(&sql, ")");
    }
    trl_odbc_sql_put(&sql, " ORDER BY TABLE_TYPE, TABLE_CAT, TABLE_SCHEM, TABLE_NAME");
    return trl_odbc_catalog_run(stmt, &sql);
}

SQLRETURN trl_odbc_columns(trl_odbc_stmt *stmt, const char *const *names) {
    trl_odbc_sql sql = {NULL, 0U, 0U, false};
    trl_odbc_sql_put(&sql, "SELECT C.TABLE_CATALOG AS TABLE_CAT, C.TABLE_SCHEMA AS TABLE_SCHEM, C.TABLE_NAME, "
                           "C.COLUMN_NAME, ");
    trl_odbc_sql_types(&sql, "C");
    trl_odbc_sql_put(&sql, ", CAST(CASE C.IS_NULLABLE WHEN 'NO' THEN 0 ELSE 1 END AS SMALLINT) AS NULLABLE, "
                           "CAST(NULL AS VARCHAR(254)) AS REMARKS, C.COLUMN_DEFAULT AS COLUMN_DEF, CAST(CASE "
                           "C.DATA_TYPE WHEN 'DATE' THEN 9 WHEN 'TIME' THEN 9 WHEN 'TIMESTAMP' THEN 9 ELSE ");
    char otherwise[TRL_NUMBER_BYTES];
    trl_format(otherwise, sizeof otherwise, "%d", SQL_VARCHAR);
    case_put(&sql, "C", FIELD_CODE, otherwise);
    trl_odbc_sql_put(&sql, " END AS SMALLINT) AS SQL_DATA_TYPE, CAST(CASE C.DATA_TYPE WHEN 'DATE' THEN 1 WHEN 'TIME' "
                           "THEN 2 WHEN 'TIMESTAMP' THEN 3 END AS SMALLINT) AS SQL_DATETIME_SUB, "
                           "CAST(C.CHARACTER_OCTET_LENGTH AS INTEGER) AS CHAR_OCTET_LENGTH, CAST(C.ORDINAL_POSITION "
                           "AS INTEGER) AS ORDINAL_POSITION, C.IS_NULLABLE FROM INFORMATION_SCHEMA.COLUMNS AS C WHERE "
                           "1 = 1");
    trl_odbc_sql_filter(&sql, stmt, "C.TABLE_CATALOG", names[TRL_NAME_CATALOG], false);
    trl_odbc_sql_filter(&sql, stmt, "C.TABLE_SCHEMA", names[TRL_NAME_SCHEMA], true);
    trl_odbc_sql_filter(&sql, stmt, "C.TABLE_NAME", names[TRL_NAME_TABLE], true);
    trl_odbc_sql_filter(&sql, stmt, "C.COLUMN_NAME", names[TRL_NAME_COLUMN], true);
    trl_odbc_sql_put(&sql, " ORDER BY TABLE_CAT, TABLE_SCHEM, TABLE_NAME, ORDINAL_POSITION");
    return trl_odbc_catalog_run(stmt, &sql);
}

bool trl_odbc_names(SQLCHAR *const *names, const SQLSMALLINT *lengths, size_t count, char **out) {
    bool made = true;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = names[i] == NULL ? NULL : trl_odbc_text(names[i], lengths[i]);
        made = made && (names[i] == NULL || out[i] != NULL);
    }
    return made;
}

bool trl_odbc_wide_names(SQLWCHAR *const *names, const SQLSMALLINT *lengths, size_t count, char **out) {
    bool made = true;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = names[i] == NULL ? NULL : trl_odbc_wide_text(names[i], lengths[i]);
        made = made && (names[i] == NULL || out[i] != NULL);
    }
    return made;
}

void trl_odbc_names_free(char **names, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        free(names[i]);
    }
}

SQLRETURN trl_odbc_with_names(SQLHSTMT handle, SQLCHAR *const *given, const SQLSMALLINT *lengths, size_t count,
                              SQLRETURN (*work)(trl_odbc_stmt *, const char *const *)) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *names[TRL_CATALOG_NAMES_MOST] = {NULL};
    if (!trl_odbc_names(given, lengths, count, names)) {
        trl_odbc_names_free(names, count);
        trl_odbc_clear(&stmt->diag);
        return trl_odbc_no_memory(&stmt->diag);
    }
    SQLRETURN returned = work(stmt, (const char *const *)names);
    trl_odbc_names_free(names, count);
    return returned;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLTables(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                            SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                            SQLSMALLINT table_length, SQLCHAR *types_given, SQLSMALLINT types_length) {
    SQLCHAR *const given[] = {catalog, schema, table, types_given};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length, types_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_tables);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLColumns(SQLHSTMT handle, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                             SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                             SQLSMALLINT table_length, SQLCHAR *column, SQLSMALLINT column_length) {
    SQLCHAR *const given[] = {catalog, schema, table, column};
    const SQLSMALLINT lengths[] = {catalog_length, schema_length, table_length, column_length};
    return trl_odbc_with_names(handle, given, lengths, sizeof lengths / sizeof lengths[0], trl_odbc_columns);
}
