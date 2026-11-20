/**
 * @file info.c
 * @brief What the driver and its data source are and do (RULES.md D2):
 * SQLGetInfo's answers, ODBC 3.80 of SQL-92's entry level, its catalog
 * of one catalog, its identifiers in upper case unless quoted, its
 * cursors forward-only or static and read only; and SQLGetFunctions'
 * list of the functions the driver has.
 */
#include "format.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The longest name, and room for the DBMS's version text. */
#define TRL_NAME_MOST 128U
#define TRL_VERSION_BYTES 160U
/* The session's setting that tells its catalog. */
#define TRL_CATALOG_SETTING "CATALOG"
/* SQL_API_ODBC3_ALL_FUNCTIONS' bitmap: each element's bits, as a shift;
   SQL_API_ALL_FUNCTIONS' list of ODBC 2's functions. */
#define TRL_ODBC_FUNCTION_BITS 16U
#define TRL_ODBC_FUNCTION_SHIFT 4U
#define TRL_ODBC2_FUNCTIONS 100U
/* The radix of a version's numbers. */
#define TRL_DECIMAL_BASE 10

/* An answer that is text. */
typedef struct text_info {
    SQLUSMALLINT type;
    const char *text;
} text_info;

static const text_info texts[] = {
    {SQL_DRIVER_NAME, "libturborelodbc"},
    {SQL_DRIVER_ODBC_VER, "03.80"},
    {SQL_ODBC_VER, "03.80"},
    {SQL_DBMS_NAME, "Turborel"},
    {SQL_IDENTIFIER_QUOTE_CHAR, "\""},
    {SQL_CATALOG_NAME_SEPARATOR, "."},
    {SQL_CATALOG_TERM, "catalog"},
    {SQL_SCHEMA_TERM, "schema"},
    {SQL_TABLE_TERM, "table"},
    {SQL_PROCEDURE_TERM, "procedure"},
    {SQL_CATALOG_NAME, "Y"},
    {SQL_SEARCH_PATTERN_ESCAPE, "\\"},
    {SQL_SPECIAL_CHARACTERS, ""},
    {SQL_KEYWORDS, ""},
    {SQL_LIKE_ESCAPE_CLAUSE, "Y"},
    {SQL_ACCESSIBLE_TABLES, "N"},
    {SQL_ACCESSIBLE_PROCEDURES, "N"},
    {SQL_EXPRESSIONS_IN_ORDERBY, "Y"},
    {SQL_ORDER_BY_COLUMNS_IN_SELECT, "N"},
    {SQL_COLUMN_ALIAS, "Y"},
    {SQL_MAX_ROW_SIZE_INCLUDES_LONG, "Y"},
    {SQL_MULT_RESULT_SETS, "N"},
    {SQL_MULTIPLE_ACTIVE_TXN, "Y"},
    {SQL_NEED_LONG_DATA_LEN, "N"},
    {SQL_OUTER_JOINS, "Y"},
    {SQL_PROCEDURES, "Y"},
    {SQL_ROW_UPDATES, "N"},
    {SQL_INTEGRITY, "Y"},
    {SQL_DESCRIBE_PARAMETER, "Y"},
    {SQL_COLLATION_SEQ, ""},
    {SQL_XOPEN_CLI_YEAR, "1995"},
};

/* An answer that is a number, of SQLUSMALLINT or SQLUINTEGER. */
typedef struct number_info {
    SQLUINTEGER number;
    SQLUSMALLINT type;
    bool small;
} number_info;

static const number_info numbers[] = {
    {.type = SQL_MAX_CATALOG_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_SCHEMA_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_TABLE_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_COLUMN_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_CURSOR_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_IDENTIFIER_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_USER_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_PROCEDURE_NAME_LEN, .number = TRL_NAME_MOST, .small = true},
    {.type = SQL_MAX_COLUMNS_IN_GROUP_BY, .number = 0U, .small = true},
    {.type = SQL_MAX_COLUMNS_IN_INDEX, .number = 0U, .small = true},
    {.type = SQL_MAX_COLUMNS_IN_ORDER_BY, .number = 0U, .small = true},
    {.type = SQL_MAX_COLUMNS_IN_SELECT, .number = 0U, .small = true},
    {.type = SQL_MAX_COLUMNS_IN_TABLE, .number = 0U, .small = true},
    {.type = SQL_MAX_TABLES_IN_SELECT, .number = 0U, .small = true},
    {.type = SQL_MAX_CONCURRENT_ACTIVITIES, .number = 0U, .small = true},
    {.type = SQL_MAX_DRIVER_CONNECTIONS, .number = 0U, .small = true},
    {.type = SQL_TXN_CAPABLE, .number = SQL_TC_ALL, .small = true},
    {.type = SQL_CURSOR_COMMIT_BEHAVIOR, .number = SQL_CB_PRESERVE, .small = true},
    {.type = SQL_CURSOR_ROLLBACK_BEHAVIOR, .number = SQL_CB_PRESERVE, .small = true},
    {.type = SQL_CONCAT_NULL_BEHAVIOR, .number = SQL_CB_NULL, .small = true},
    {.type = SQL_CORRELATION_NAME, .number = SQL_CN_ANY, .small = true},
    {.type = SQL_NON_NULLABLE_COLUMNS, .number = SQL_NNC_NON_NULL, .small = true},
    {.type = SQL_NULL_COLLATION, .number = SQL_NC_HIGH, .small = true},
    {.type = SQL_IDENTIFIER_CASE, .number = SQL_IC_UPPER, .small = true},
    {.type = SQL_QUOTED_IDENTIFIER_CASE, .number = SQL_IC_SENSITIVE, .small = true},
    {.type = SQL_GROUP_BY, .number = SQL_GB_GROUP_BY_CONTAINS_SELECT, .small = true},
    {.type = SQL_FILE_USAGE, .number = SQL_FILE_NOT_SUPPORTED, .small = true},
    {.type = SQL_CATALOG_LOCATION, .number = SQL_CL_START, .small = true},
    {
        .type = SQL_TXN_ISOLATION_OPTION,
        .number = SQL_TXN_READ_UNCOMMITTED | SQL_TXN_READ_COMMITTED | SQL_TXN_REPEATABLE_READ | SQL_TXN_SERIALIZABLE,
        .small = false,
    },
    {.type = SQL_DEFAULT_TXN_ISOLATION, .number = SQL_TXN_READ_COMMITTED, .small = false},
    {.type = SQL_GETDATA_EXTENSIONS, .number = SQL_GD_ANY_COLUMN | SQL_GD_ANY_ORDER | SQL_GD_BOUND, .small = false},
    {.type = SQL_SCROLL_OPTIONS, .number = SQL_SO_FORWARD_ONLY | SQL_SO_STATIC, .small = false},
    {
        .type = SQL_STATIC_CURSOR_ATTRIBUTES1,
        .number = SQL_CA1_NEXT | SQL_CA1_ABSOLUTE | SQL_CA1_RELATIVE,
        .small = false,
    },
    {
        .type = SQL_STATIC_CURSOR_ATTRIBUTES2,
        .number = SQL_CA2_READ_ONLY_CONCURRENCY | SQL_CA2_MAX_ROWS_SELECT,
        .small = false,
    },
    {.type = SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1, .number = SQL_CA1_NEXT, .small = false},
    {
        .type = SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2,
        .number = SQL_CA2_READ_ONLY_CONCURRENCY | SQL_CA2_MAX_ROWS_SELECT,
        .small = false,
    },
    {.type = SQL_KEYSET_CURSOR_ATTRIBUTES1, .number = 0U, .small = false},
    {.type = SQL_KEYSET_CURSOR_ATTRIBUTES2, .number = 0U, .small = false},
    {.type = SQL_DYNAMIC_CURSOR_ATTRIBUTES1, .number = 0U, .small = false},
    {.type = SQL_DYNAMIC_CURSOR_ATTRIBUTES2, .number = 0U, .small = false},
    {.type = SQL_SCROLL_CONCURRENCY, .number = SQL_SCCO_READ_ONLY, .small = false},
    {.type = SQL_CURSOR_SENSITIVITY, .number = SQL_INSENSITIVE, .small = false},
    {.type = SQL_BOOKMARK_PERSISTENCE, .number = 0U, .small = false},
    {.type = SQL_POS_OPERATIONS, .number = 0U, .small = false},
    {.type = SQL_POSITIONED_STATEMENTS, .number = 0U, .small = false},
    {.type = SQL_LOCK_TYPES, .number = 0U, .small = false},
    {.type = SQL_STATIC_SENSITIVITY, .number = 0U, .small = false},
    {
        .type = SQL_CATALOG_USAGE,
        .number = SQL_CU_DML_STATEMENTS | SQL_CU_PROCEDURE_INVOCATION | SQL_CU_TABLE_DEFINITION,
        .small = false,
    },
    {
        .type = SQL_SCHEMA_USAGE,
        .number = SQL_SU_DML_STATEMENTS | SQL_SU_PROCEDURE_INVOCATION | SQL_SU_TABLE_DEFINITION |
                  SQL_SU_INDEX_DEFINITION | SQL_SU_PRIVILEGE_DEFINITION,
        .small = false,
    },
    {
        .type = SQL_STRING_FUNCTIONS,
        .number = SQL_FN_STR_CONCAT | SQL_FN_STR_LCASE | SQL_FN_STR_UCASE | SQL_FN_STR_LENGTH | SQL_FN_STR_LTRIM |
                  SQL_FN_STR_RTRIM,
        .small = false,
    },
    {
        .type = SQL_NUMERIC_FUNCTIONS,
        .number = SQL_FN_NUM_ABS | SQL_FN_NUM_CEILING | SQL_FN_NUM_FLOOR | SQL_FN_NUM_MOD | SQL_FN_NUM_SQRT |
                  SQL_FN_NUM_EXP | SQL_FN_NUM_POWER,
        .small = false,
    },
    {
        .type = SQL_TIMEDATE_FUNCTIONS,
        .number = SQL_FN_TD_NOW | SQL_FN_TD_CURDATE | SQL_FN_TD_CURTIME | SQL_FN_TD_CURRENT_DATE |
                  SQL_FN_TD_CURRENT_TIME | SQL_FN_TD_CURRENT_TIMESTAMP,
        .small = false,
    },
    {.type = SQL_SYSTEM_FUNCTIONS, .number = SQL_FN_SYS_USERNAME | SQL_FN_SYS_IFNULL, .small = false},
    {.type = SQL_CONVERT_FUNCTIONS, .number = SQL_FN_CVT_CAST | SQL_FN_CVT_CONVERT, .small = false},
    {
        .type = SQL_OJ_CAPABILITIES,
        .number = SQL_OJ_LEFT | SQL_OJ_RIGHT | SQL_OJ_FULL | SQL_OJ_NESTED | SQL_OJ_NOT_ORDERED | SQL_OJ_INNER |
                  SQL_OJ_ALL_COMPARISON_OPS,
        .small = false,
    },
    {.type = SQL_SQL_CONFORMANCE, .number = SQL_SC_SQL92_ENTRY, .small = false},
    {.type = SQL_ODBC_INTERFACE_CONFORMANCE, .number = SQL_OIC_CORE, .small = false},
    {.type = SQL_BATCH_SUPPORT, .number = 0U, .small = false},
    {.type = SQL_BATCH_ROW_COUNT, .number = 0U, .small = false},
    {.type = SQL_PARAM_ARRAY_ROW_COUNTS, .number = SQL_PARC_NO_BATCH, .small = false},
    {.type = SQL_PARAM_ARRAY_SELECTS, .number = SQL_PAS_NO_SELECT, .small = false},
    {.type = SQL_ASYNC_MODE, .number = SQL_AM_NONE, .small = false},
    {.type = SQL_MAX_ASYNC_CONCURRENT_STATEMENTS, .number = 0U, .small = false},
    {.type = SQL_AGGREGATE_FUNCTIONS, .number = SQL_AF_ALL, .small = false},
    {
        .type = SQL_DATETIME_LITERALS,
        .number = SQL_DL_SQL92_DATE | SQL_DL_SQL92_TIME | SQL_DL_SQL92_TIMESTAMP,
        .small = false,
    },
    {.type = SQL_INDEX_KEYWORDS, .number = SQL_IK_NONE, .small = false},
    {.type = SQL_INSERT_STATEMENT, .number = SQL_IS_INSERT_LITERALS | SQL_IS_INSERT_SEARCHED, .small = false},
    {.type = SQL_UNION, .number = SQL_U_UNION | SQL_U_UNION_ALL, .small = false},
    {
        .type = SQL_SUBQUERIES,
        .number = SQL_SQ_CORRELATED_SUBQUERIES | SQL_SQ_COMPARISON | SQL_SQ_EXISTS | SQL_SQ_IN | SQL_SQ_QUANTIFIED,
        .small = false,
    },
    {.type = SQL_MAX_BINARY_LITERAL_LEN, .number = 0U, .small = false},
    {.type = SQL_MAX_CHAR_LITERAL_LEN, .number = 0U, .small = false},
    {.type = SQL_MAX_INDEX_SIZE, .number = 0U, .small = false},
    {.type = SQL_MAX_ROW_SIZE, .number = 0U, .small = false},
    {.type = SQL_MAX_STATEMENT_LEN, .number = 0U, .small = false},
    {.type = SQL_DTC_TRANSITION_COST, .number = 0U, .small = false},
};

/* The DBMS's version, as ODBC asks: ##.##.####, then the server's product. */
static void dbms_version(const trl_odbc_dbc *dbc, char *out, size_t capacity) {
    unsigned int major = 0U;
    unsigned int minor = 0U;
    unsigned int patch = 0U;
    const char *server = dbc->conn != NULL ? trl_conn_server(dbc->conn) : NULL;
    const char *digits = server != NULL ? strpbrk(server, "0123456789") : NULL;
    if (digits != NULL) {
        char *end = NULL;
        major = (unsigned int)strtoul(digits, &end, TRL_DECIMAL_BASE);
        minor = *end == '.' ? (unsigned int)strtoul(end + 1, &end, TRL_DECIMAL_BASE) : 0U;
        patch = *end == '.' ? (unsigned int)strtoul(end + 1, &end, TRL_DECIMAL_BASE) : 0U;
    }
    trl_format(out, capacity, "%02u.%02u.%04u %s", major, minor, patch, server != NULL ? server : "");
}

/* An answer of the connection itself; null if `type` is none of them. */
static const char *of_connection(const trl_odbc_dbc *dbc, SQLUSMALLINT type, char *buffer, size_t capacity) {
    switch (type) {
    case SQL_DRIVER_VER:
        trl_format(buffer, capacity, "%02d.%02d.%04d", TRL_VERSION_MAJOR, TRL_VERSION_MINOR, TRL_VERSION_PATCH);
        return buffer;
    case SQL_DBMS_VER:
        dbms_version(dbc, buffer, capacity);
        return buffer;
    case SQL_DATA_SOURCE_NAME:
        return dbc->dsn != NULL ? dbc->dsn : "";
    case SQL_SERVER_NAME:
        return dbc->conn != NULL ? trl_conn_server(dbc->conn) : "";
    case SQL_DATABASE_NAME: {
        const char *catalog = dbc->conn != NULL ? trl_conn_setting(dbc->conn, TRL_CATALOG_SETTING) : NULL;
        return catalog != NULL ? catalog : "";
    }
    case SQL_USER_NAME:
        return dbc->conn != NULL ? trl_conn_authorization(dbc->conn) : "";
    case SQL_DATA_SOURCE_READ_ONLY:
        return dbc->read_only ? "Y" : "N";
    default:
        return NULL;
    }
}

/* Gives an answer that is a number, of its width. */
static void number_put(const number_info *info, const trl_odbc_out *out) {
    if (out->buffer != NULL && info->small) {
        *(SQLUSMALLINT *)out->buffer = (SQLUSMALLINT)info->number;
    } else if (out->buffer != NULL) {
        *(SQLUINTEGER *)out->buffer = info->number;
    }
    if (out->small_length != NULL) {
        *out->small_length = info->small ? (SQLSMALLINT)sizeof(SQLUSMALLINT) : (SQLSMALLINT)sizeof(SQLUINTEGER);
    }
}

SQLRETURN trl_odbc_get_info(trl_odbc_dbc *dbc, SQLUSMALLINT type, const trl_odbc_out *out) {
    trl_odbc_clear(&dbc->diag);
    char buffer[TRL_VERSION_BYTES];
    const char *text = of_connection(dbc, type, buffer, sizeof buffer);
    for (size_t i = 0U; text == NULL && i < sizeof texts / sizeof texts[0]; ++i) {
        text = texts[i].type == type ? texts[i].text : NULL;
    }
    if (text != NULL) {
        return trl_odbc_put(&dbc->diag, out, text);
    }
    for (size_t i = 0U; i < sizeof numbers / sizeof numbers[0]; ++i) {
        if (numbers[i].type == type) {
            number_put(&numbers[i], out);
            return SQL_SUCCESS;
        }
    }
    return trl_odbc_fail(&dbc->diag, "HY096", "Information type out of range");
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetInfo(SQLHDBC handle, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT capacity,
                                             SQLSMALLINT *length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {value, capacity, length, NULL, NULL, false, true};
    return trl_odbc_get_info(dbc, type, &out);
}

/* The functions the driver has. */
static const SQLUSMALLINT functions[] = {
    SQL_API_SQLALLOCHANDLE,     SQL_API_SQLBINDCOL,          SQL_API_SQLBINDPARAMETER,  SQL_API_SQLCANCEL,
    SQL_API_SQLCLOSECURSOR,     SQL_API_SQLCOLATTRIBUTE,     SQL_API_SQLCOLUMNS,        SQL_API_SQLCONNECT,
    SQL_API_SQLCOPYDESC,        SQL_API_SQLDESCRIBECOL,      SQL_API_SQLDESCRIBEPARAM,  SQL_API_SQLDISCONNECT,
    SQL_API_SQLDRIVERCONNECT,   SQL_API_SQLENDTRAN,          SQL_API_SQLEXECDIRECT,     SQL_API_SQLEXECUTE,
    SQL_API_SQLFETCH,           SQL_API_SQLFETCHSCROLL,      SQL_API_SQLFOREIGNKEYS,    SQL_API_SQLFREEHANDLE,
    SQL_API_SQLFREESTMT,        SQL_API_SQLGETCONNECTATTR,   SQL_API_SQLGETDATA,        SQL_API_SQLGETDESCFIELD,
    SQL_API_SQLGETDESCREC,      SQL_API_SQLGETDIAGFIELD,     SQL_API_SQLGETDIAGREC,     SQL_API_SQLGETENVATTR,
    SQL_API_SQLGETFUNCTIONS,    SQL_API_SQLGETINFO,          SQL_API_SQLGETSTMTATTR,    SQL_API_SQLGETTYPEINFO,
    SQL_API_SQLMORERESULTS,     SQL_API_SQLNATIVESQL,        SQL_API_SQLNUMPARAMS,      SQL_API_SQLNUMRESULTCOLS,
    SQL_API_SQLPARAMDATA,       SQL_API_SQLPREPARE,          SQL_API_SQLPRIMARYKEYS,    SQL_API_SQLPROCEDURES,
    SQL_API_SQLPUTDATA,         SQL_API_SQLROWCOUNT,         SQL_API_SQLSETCONNECTATTR, SQL_API_SQLSETDESCFIELD,
    SQL_API_SQLSETDESCREC,      SQL_API_SQLSETENVATTR,       SQL_API_SQLSETSTMTATTR,    SQL_API_SQLSPECIALCOLUMNS,
    SQL_API_SQLSTATISTICS,      SQL_API_SQLTABLES,           SQL_API_SQLSETPOS,         SQL_API_SQLPROCEDURECOLUMNS,
    SQL_API_SQLTABLEPRIVILEGES, SQL_API_SQLCOLUMNPRIVILEGES, SQL_API_SQLGETCURSORNAME,  SQL_API_SQLSETCURSORNAME,
};

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetFunctions(SQLHDBC handle, SQLUSMALLINT id, SQLUSMALLINT *supported) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&dbc->diag);
    if (supported == NULL) {
        return trl_odbc_fail(&dbc->diag, "HY009", "Invalid use of null pointer");
    }
    size_t count = sizeof functions / sizeof functions[0];
    if (id == SQL_API_ODBC3_ALL_FUNCTIONS) {
        memset(supported, 0, SQL_API_ODBC3_ALL_FUNCTIONS_SIZE * sizeof *supported);
        for (size_t i = 0U; i < count; ++i) {
            supported[functions[i] >> TRL_ODBC_FUNCTION_SHIFT] |=
                (SQLUSMALLINT)(1U << (functions[i] & (TRL_ODBC_FUNCTION_BITS - 1U)));
        }
        return SQL_SUCCESS;
    }
    if (id == SQL_API_ALL_FUNCTIONS) {
        memset(supported, 0, (size_t)TRL_ODBC2_FUNCTIONS * sizeof *supported);
        for (size_t i = 0U; i < count; ++i) {
            if (functions[i] < TRL_ODBC2_FUNCTIONS) {
                supported[functions[i]] = SQL_TRUE;
            }
        }
        return SQL_SUCCESS;
    }
    *supported = SQL_FALSE;
    for (size_t i = 0U; i < count; ++i) {
        *supported = functions[i] == id ? SQL_TRUE : *supported;
    }
    return SQL_SUCCESS;
}
