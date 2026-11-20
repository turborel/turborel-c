/**
 * @file units_test.c
 * @brief The ODBC driver's parts that need no server: escape sequences
 * written as SQL, text as UTF-8 and as SQLWCHAR, handles allocated and
 * refused, and the diagnostics of each.
 */
#include "harness.h"
#include "odbc/escapes.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Whether `sql` is written as `expected`. */
static bool escaped_as(const char *sql, const char *expected) {
    bool malformed = false;
    char *written = trl_odbc_escaped(sql, &malformed);
    bool same = written != NULL && strcmp(written, expected) == 0;
    free(written);
    return same;
}

/* Whether `sql` is refused as malformed. */
static bool refused(const char *sql) {
    bool malformed = false;
    char *written = trl_odbc_escaped(sql, &malformed);
    free(written);
    return written == NULL && malformed;
}

/**
 * Verifies: [DESIGN] D2 ODBC's escape sequences written as Turborel's
 * SQL: datetime literals, outer joins, calls, LIKE's escape, intervals,
 * scalar functions renamed or rewritten, nested; quotes and comments
 * copied as they are; an escape not closed, or of no kind, refused.
 */
static void escape_sequences_are_written_as_sql(trl_test *test) {
    TRL_CHECK(test, escaped_as("SELECT {d '2020-01-02'}, {t '10:00:00'}, {ts '2020-01-02 10:00:00'}",
                               "SELECT DATE '2020-01-02', TIME '10:00:00', TIMESTAMP '2020-01-02 10:00:00'"));
    TRL_CHECK(test, escaped_as("SELECT * FROM {oj A LEFT OUTER JOIN B ON A.K = B.K}",
                               "SELECT * FROM A LEFT OUTER JOIN B ON A.K = B.K"));
    TRL_CHECK(test, escaped_as("{call P(1, 2)}", "CALL P(1, 2)"));
    TRL_CHECK(test,
              escaped_as("SELECT 1 WHERE 'a' LIKE 'a!_' {escape '!'}", "SELECT 1 WHERE 'a' LIKE 'a!_' ESCAPE '!'"));
    TRL_CHECK(test, escaped_as("VALUES {interval '1' DAY}", "VALUES INTERVAL '1' DAY"));
    TRL_CHECK(test, escaped_as("VALUES {fn UCASE('a')}, {fn LCASE('B')}", "VALUES UPPER('a'), LOWER('B')"));
    TRL_CHECK(test, escaped_as("VALUES {fn LENGTH('a')}, {fn IFNULL(NULL, 1)}",
                               "VALUES CHAR_LENGTH('a'), COALESCE(NULL, 1)"));
    TRL_CHECK(test, escaped_as("VALUES {fn NOW()}, {fn CURDATE()}, {fn CURTIME()}, {fn USER()}",
                               "VALUES LOCALTIMESTAMP, CURRENT_DATE, LOCALTIME, CURRENT_USER"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONCAT('a,b', 'c')}", "VALUES ('a,b' ||  'c')"));
    TRL_CHECK(test, escaped_as("VALUES {fn LTRIM(' a')}, {fn RTRIM('a ')}",
                               "VALUES TRIM(LEADING FROM ' a'), TRIM(TRAILING FROM 'a ')"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONVERT(1, SQL_VARCHAR)}, {fn CONVERT('1', SQL_INTEGER )}",
                               "VALUES CAST(1 AS CHARACTER VARYING), CAST('1' AS INTEGER)"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONVERT(1, MYTYPE)}", "VALUES CAST(1 AS MYTYPE)"));
    TRL_CHECK(test, escaped_as("VALUES {fn ABS(-1)}, {fn PI}", "VALUES ABS(-1), PI"));
    TRL_CHECK(test, escaped_as("VALUES {fn UCASE({fn LTRIM(' a')})}", "VALUES UPPER(TRIM(LEADING FROM ' a'))"));
    TRL_CHECK(test, escaped_as("VALUES '{d', \"x{\" -- {oj\n /* {fn */ 1", "VALUES '{d', \"x{\" -- {oj\n /* {fn */ 1"));
    TRL_CHECK(test, escaped_as("VALUES 'it''s {x}'", "VALUES 'it''s {x}'"));
    TRL_CHECK(test, refused("VALUES {d '2020-01-02'"));
    TRL_CHECK(test, refused("VALUES {nope 1}"));
    TRL_CHECK(test, refused("VALUES 1}"));
    TRL_CHECK(test, refused("VALUES {fn A({fn B({fn C({fn D({fn E({fn F({fn G({fn H({fn I({fn J({fn K({fn L({fn M({fn "
                            "N({fn O({fn P({fn Q(1)})})})})})})})})})})})})})})})})}"));
    TRL_CHECK(test, escaped_as("VALUES 1 /* not closed", "VALUES 1 /* not closed"));
}

/**
 * Verifies: [DESIGN] D2 scalar functions at their edges: a function
 * written as it is where its arguments are not those it is rewritten for,
 * three arguments or one, a function whose name only begins as another's,
 * one named bare given arguments; arguments holding quotes, commas and
 * parentheses; a type spaced about; a comment that runs to the text's end.
 */
static void escapes_at_their_edges(trl_test *test) {
    TRL_CHECK(test, escaped_as("VALUES {fn CONCAT('a', 'b', 'c')}", "VALUES CONCAT('a', 'b', 'c')"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONCAT('a')}", "VALUES CONCAT('a')"));
    TRL_CHECK(test, escaped_as("VALUES {fn LTRIM('a', 'b')}, {fn RTRIM()}", "VALUES LTRIM('a', 'b'), RTRIM()"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONVERT(1)}", "VALUES CONVERT(1)"));
    TRL_CHECK(test, escaped_as("VALUES {fn NOW_X()}, {fn NOW(1)}", "VALUES NOW_X(), NOW(1)"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONCAT('(a,'', b', f(1, (2)))}", "VALUES ('(a,'', b' ||  f(1, (2)))"));
    TRL_CHECK(test, escaped_as("VALUES {fn CONVERT(1,   SQL_INTEGER   )}", "VALUES CAST(1 AS INTEGER)"));
    TRL_CHECK(test, escaped_as("VALUES 1 -- {fn UCASE('a')}", "VALUES 1 -- {fn UCASE('a')}"));
    TRL_CHECK(test, escaped_as("VALUES \"a\"\"b\", 'c''", "VALUES \"a\"\"b\", 'c''"));
    TRL_CHECK(test, escaped_as("VALUES {fn UCASE}", "VALUES UCASE"));
}

/**
 * Verifies: [DESIGN] D2 text turned to SQLWCHAR and back: characters
 * past the first plane as surrogate pairs where SQLWCHAR has two bytes,
 * text cut short at a whole character, a malformed one taken as U+FFFD.
 */
static void text_is_turned_to_and_from_sqlwchar(trl_test *test) {
    const char *text = "a\xC3\xA9\xE2\x98\xBA\xF0\x9F\x98\x80";
    SQLWCHAR wide[16];
    size_t units = trl_odbc_widen(text, wide, 16U);
    bool pairs = sizeof(SQLWCHAR) == 2U;
    TRL_CHECK(test, units == (pairs ? 5U : 4U) && wide[0] == 'a' && wide[1] == 0xE9 && wide[2] == 0x263A);
    char *back = trl_odbc_wide_text(wide, SQL_NTS);
    TRL_CHECK(test, back != NULL && strcmp(back, text) == 0);
    free(back);
    SQLWCHAR cut[4];
    TRL_CHECK(test, trl_odbc_widen(text, cut, 4U) == units && cut[3] == 0);
    SQLWCHAR lone[] = {0xD800, 'a', 0};
    back = trl_odbc_wide_text(lone, 2);
    TRL_CHECK(test, back != NULL && strcmp(back, "\xEF\xBF\xBD"
                                                 "a") == 0);
    free(back);
    SQLWCHAR bad[4];
    TRL_CHECK(test, trl_odbc_widen("\x80\xC3", bad, 4U) == 2U && bad[0] == 0xFFFD);
    TRL_CHECK(test, trl_odbc_wide_text(NULL, SQL_NTS) == NULL && trl_odbc_wide_text(lone, -5) == NULL);
    TRL_CHECK(test, trl_odbc_text(NULL, SQL_NTS) == NULL && trl_odbc_text((const SQLCHAR *)"x", -5) == NULL);
    char *two = trl_odbc_text((const SQLCHAR *)"abc", 2);
    TRL_CHECK(test, two != NULL && strcmp(two, "ab") == 0);
    free(two);
    trl_odbc_diag diag = {NULL, 0U, SQL_SUCCESS};
    char out[4];
    SQLSMALLINT length = 0;
    trl_odbc_out narrow = {out, (SQLLEN)sizeof out, &length, NULL, NULL, false, false};
    TRL_CHECK(test, trl_odbc_put(&diag, &narrow, "ab\xC3\xA9") == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, strcmp(out, "ab") == 0 && length == 4 && diag.count == 1U);
    TRL_CHECK(test, strcmp(diag.records[0].sqlstate, "01004") == 0);
    trl_odbc_clear(&diag);
    SQLWCHAR wide_out[3];
    SQLINTEGER bytes = 0;
    trl_odbc_out widened = {wide_out, (SQLLEN)sizeof wide_out, NULL, &bytes, NULL, true, true};
    TRL_CHECK(test, trl_odbc_put(&diag, &widened, "ab") == SQL_SUCCESS && wide_out[1] == 'b' &&
                        bytes == (SQLINTEGER)(2U * sizeof(SQLWCHAR)));
    SQLLEN long_length = 0;
    trl_odbc_out none = {NULL, 0, NULL, NULL, &long_length, false, false};
    TRL_CHECK(test, trl_odbc_put(&diag, &none, "abc") == SQL_SUCCESS && long_length == 3);
    trl_odbc_out empty = {out, 0, NULL, NULL, NULL, false, false};
    TRL_CHECK(test, trl_odbc_put(NULL, &empty, "abc") == SQL_SUCCESS_WITH_INFO);
    trl_odbc_clear(&diag);
}

/**
 * Verifies: [DESIGN] D2 handles allocated and freed in their order: a
 * connection only of an environment of an ODBC version, a statement and a
 * descriptor only of a connected connection; a handle of another kind
 * invalid; an environment's attributes; each failure's SQLSTATE, class
 * origin and native code in the diagnostics.
 */
static void handles_are_allocated_in_order(trl_test *test) {
    SQLHENV env = SQL_NULL_HANDLE;
    SQLHDBC dbc = SQL_NULL_HANDLE;
    SQLHANDLE other = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) == SQL_SUCCESS);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_ERROR);
    SQLCHAR state[6];
    SQLCHAR origin[16];
    SQLINTEGER number = 0;
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_ENV, env, 0, SQL_DIAG_NUMBER, &number, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, number == 1);
    TRL_CHECK(test, SQLGetDiagRec(SQL_HANDLE_ENV, env, 1, state, NULL, NULL, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)state, "HY010") == 0);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_ENV, env, 1, SQL_DIAG_CLASS_ORIGIN, origin, (SQLSMALLINT)sizeof origin,
                                    NULL) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)origin, "ODBC 3.0") == 0);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_ENV, env, 2, SQL_DIAG_SQLSTATE, state, 6, NULL) == SQL_NO_DATA);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_ENV, env, 1, 9999, state, 6, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)99, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3_80, 0) == SQL_SUCCESS);
    SQLINTEGER version = 0;
    TRL_CHECK(test, SQLGetEnvAttr(env, SQL_ATTR_ODBC_VERSION, &version, 0, NULL) == SQL_SUCCESS &&
                        version == SQL_OV_ODBC3_80);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_OUTPUT_NTS, (SQLPOINTER)SQL_FALSE, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetEnvAttr(env, 9999, NULL, 0) == SQL_ERROR &&
                        SQLGetEnvAttr(env, 9999, &version, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_SUCCESS);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_STMT, env, &other) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DESC, dbc, &other) == SQL_ERROR);
    TRL_CHECK(test, SQLAllocHandle(99, env, &other) == SQL_ERROR);
    TRL_CHECK(test, SQLDisconnect(dbc) == SQL_ERROR && SQLDisconnect(env) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_STMT, dbc) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_DESC, dbc) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLFreeHandle(99, dbc) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDiagRec(99, dbc, 1, state, NULL, NULL, 0, NULL) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_DBC, dbc) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_ENV, env) == SQL_SUCCESS);
}

/* Whether each of a function's outcomes says its handle is invalid. */
static bool invalid(const SQLRETURN *returned, size_t count) {
    bool every = true;
    for (size_t i = 0U; i < count; ++i) {
        every = every && returned[i] == SQL_INVALID_HANDLE;
    }
    return every;
}

/* What a function is given beside its handle, none of it read. */
typedef struct given {
    SQLCHAR text[8];
    SQLWCHAR wide[8];
    SQLSMALLINT small;
    SQLINTEGER integer;
    SQLLEN length;
    SQLULEN size;
    SQLPOINTER pointer;
} given;

/* Whether the connection's and the statement's running functions refuse `x`. */
static bool running_refuses(SQLHANDLE x, given *g) {
    const SQLRETURN returned[] = {
        SQLConnect(x, g->text, 0, g->text, 0, g->text, 0),
        SQLConnectW(x, g->wide, 0, g->wide, 0, g->wide, 0),
        SQLDriverConnect(x, NULL, g->text, 0, g->text, 0, &g->small, SQL_DRIVER_NOPROMPT),
        SQLDriverConnectW(x, NULL, g->wide, 0, g->wide, 0, &g->small, SQL_DRIVER_NOPROMPT),
        SQLDisconnect(x),
        SQLExecDirect(x, g->text, 0),
        SQLExecDirectW(x, g->wide, 0),
        SQLPrepare(x, g->text, 0),
        SQLPrepareW(x, g->wide, 0),
        SQLExecute(x),
        SQLNumParams(x, &g->small),
        SQLDescribeParam(x, 1, &g->small, &g->size, &g->small, &g->small),
        SQLBindParameter(x, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, 1, 0, g->text, 1, &g->length),
        SQLRowCount(x, &g->length),
        SQLMoreResults(x),
        SQLCloseCursor(x),
        SQLCancel(x),
        SQLParamData(x, &g->pointer),
        SQLPutData(x, g->text, 1),
        SQLNativeSql(x, g->text, 0, g->text, 0, &g->integer),
        SQLNativeSqlW(x, g->wide, 0, g->wide, 0, &g->integer),
        SQLSetCursorName(x, g->text, 0),
        SQLSetCursorNameW(x, g->wide, 0),
        SQLGetCursorName(x, g->text, 0, &g->small),
        SQLGetCursorNameW(x, g->wide, 0, &g->small),
        SQLEndTran(SQL_HANDLE_DBC, x, SQL_COMMIT),
    };
    return invalid(returned, sizeof returned / sizeof returned[0]);
}

/* Whether the functions of a result's columns and rows refuse `x`. */
static bool results_refuse(SQLHANDLE x, given *g) {
    const SQLRETURN returned[] = {
        SQLNumResultCols(x, &g->small),
        SQLDescribeCol(x, 1, g->text, 0, &g->small, &g->small, &g->size, &g->small, &g->small),
        SQLDescribeColW(x, 1, g->wide, 0, &g->small, &g->small, &g->size, &g->small, &g->small),
        SQLColAttribute(x, 1, SQL_DESC_NAME, g->text, 0, &g->small, &g->length),
        SQLColAttributeW(x, 1, SQL_DESC_NAME, g->wide, 0, &g->small, &g->length),
        SQLFetch(x),
        SQLFetchScroll(x, SQL_FETCH_NEXT, 0),
        SQLBindCol(x, 1, SQL_C_CHAR, g->text, 1, &g->length),
        SQLGetData(x, 1, SQL_C_CHAR, g->text, 1, &g->length),
        SQLSetPos(x, 1, SQL_POSITION, SQL_LOCK_NO_CHANGE),
        SQLFreeStmt(x, SQL_CLOSE),
        SQLGetInfo(x, SQL_DRIVER_NAME, g->text, 0, &g->small),
        SQLGetInfoW(x, SQL_DRIVER_NAME, g->wide, 0, &g->small),
        SQLGetFunctions(x, SQL_API_SQLFETCH, (SQLUSMALLINT *)&g->small),
        SQLGetDiagField(SQL_HANDLE_STMT, x, 1, SQL_DIAG_SQLSTATE, g->text, 0, &g->small),
        SQLGetDiagFieldW(SQL_HANDLE_DESC, x, 1, SQL_DIAG_SQLSTATE, g->wide, 0, &g->small),
        SQLGetDiagRecW(SQL_HANDLE_DBC, x, 1, g->wide, &g->integer, g->wide, 0, &g->small),
    };
    return invalid(returned, sizeof returned / sizeof returned[0]);
}

/* Whether the attribute and descriptor functions refuse `x`. */
static bool attributes_refuse(SQLHANDLE x, given *g) {
    const SQLRETURN returned[] = {
        SQLSetConnectAttr(x, SQL_ATTR_AUTOCOMMIT, NULL, 0),
        SQLSetConnectAttrW(x, SQL_ATTR_AUTOCOMMIT, NULL, 0),
        SQLGetConnectAttr(x, SQL_ATTR_AUTOCOMMIT, &g->integer, 0, &g->integer),
        SQLGetConnectAttrW(x, SQL_ATTR_AUTOCOMMIT, &g->integer, 0, &g->integer),
        SQLSetStmtAttr(x, SQL_ATTR_MAX_ROWS, NULL, 0),
        SQLSetStmtAttrW(x, SQL_ATTR_MAX_ROWS, NULL, 0),
        SQLGetStmtAttr(x, SQL_ATTR_MAX_ROWS, &g->size, 0, &g->integer),
        SQLGetStmtAttrW(x, SQL_ATTR_MAX_ROWS, &g->size, 0, &g->integer),
        SQLGetDescField(x, 1, SQL_DESC_TYPE, &g->small, 0, &g->integer),
        SQLGetDescFieldW(x, 1, SQL_DESC_TYPE, &g->small, 0, &g->integer),
        SQLSetDescField(x, 1, SQL_DESC_TYPE, NULL, 0),
        SQLSetDescFieldW(x, 1, SQL_DESC_TYPE, NULL, 0),
        SQLGetDescRec(x, 1, g->text, 0, &g->small, &g->small, &g->small, &g->length, &g->small, &g->small, &g->small),
        SQLSetDescRec(x, 1, SQL_C_CHAR, 0, 1, 0, 0, g->text, &g->length, &g->length),
        SQLCopyDesc(x, x),
    };
    return invalid(returned, sizeof returned / sizeof returned[0]);
}

/* Whether the catalog functions refuse `x`. */
static bool catalog_refuses(SQLHANDLE x, given *g) {
    SQLCHAR *t = g->text;
    SQLWCHAR *w = g->wide;
    const SQLRETURN returned[] = {
        SQLTables(x, t, 0, t, 0, t, 0, t, 0),
        SQLTablesW(x, w, 0, w, 0, w, 0, w, 0),
        SQLColumns(x, t, 0, t, 0, t, 0, t, 0),
        SQLColumnsW(x, w, 0, w, 0, w, 0, w, 0),
        SQLPrimaryKeys(x, t, 0, t, 0, t, 0),
        SQLPrimaryKeysW(x, w, 0, w, 0, w, 0),
        SQLForeignKeys(x, t, 0, t, 0, t, 0, t, 0, t, 0, t, 0),
        SQLForeignKeysW(x, w, 0, w, 0, w, 0, w, 0, w, 0, w, 0),
        SQLStatistics(x, t, 0, t, 0, t, 0, SQL_INDEX_ALL, SQL_QUICK),
        SQLStatisticsW(x, w, 0, w, 0, w, 0, SQL_INDEX_ALL, SQL_QUICK),
        SQLSpecialColumns(x, SQL_BEST_ROWID, t, 0, t, 0, t, 0, SQL_SCOPE_SESSION, SQL_NULLABLE),
        SQLSpecialColumnsW(x, SQL_BEST_ROWID, w, 0, w, 0, w, 0, SQL_SCOPE_SESSION, SQL_NULLABLE),
        SQLProcedures(x, t, 0, t, 0, t, 0),
        SQLProceduresW(x, w, 0, w, 0, w, 0),
        SQLProcedureColumns(x, t, 0, t, 0, t, 0, t, 0),
        SQLProcedureColumnsW(x, w, 0, w, 0, w, 0, w, 0),
        SQLTablePrivileges(x, t, 0, t, 0, t, 0),
        SQLTablePrivilegesW(x, w, 0, w, 0, w, 0),
        SQLColumnPrivileges(x, t, 0, t, 0, t, 0, t, 0),
        SQLColumnPrivilegesW(x, w, 0, w, 0, w, 0, w, 0),
        SQLGetTypeInfo(x, SQL_ALL_TYPES),
        SQLGetTypeInfoW(x, SQL_ALL_TYPES),
    };
    return invalid(returned, sizeof returned / sizeof returned[0]);
}

/**
 * Verifies: [DESIGN] D2 each entry point refuses a null handle, and one of
 * another kind, as invalid, touching nothing.
 */
static void every_entry_point_refuses_an_invalid_handle(trl_test *test) {
    SQLHENV env = SQL_NULL_HANDLE;
    (void)SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    given g = {{0}, {0}, 0, 0, 0, 0U, NULL};
    const SQLHANDLE handles[] = {SQL_NULL_HANDLE, env};
    for (size_t h = 0U; h < sizeof handles / sizeof handles[0]; ++h) {
        TRL_CHECK(test, running_refuses(handles[h], &g) && results_refuse(handles[h], &g) &&
                            attributes_refuse(handles[h], &g) && catalog_refuses(handles[h], &g));
    }
    TRL_CHECK(test, SQLSetEnvAttr(SQL_NULL_HANDLE, SQL_ATTR_ODBC_VERSION, NULL, 0) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLGetEnvAttr(SQL_NULL_HANDLE, SQL_ATTR_ODBC_VERSION, &g.integer, 0, NULL) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_ENV, SQL_NULL_HANDLE, SQL_COMMIT) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_DBC, env) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DBC, SQL_NULL_HANDLE, &g.pointer) == SQL_INVALID_HANDLE);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DESC, env, &g.pointer) == SQL_INVALID_HANDLE);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
}

/**
 * Verifies: [DESIGN] D2 an environment's attributes set and read: ODBC's
 * version, of 2 too; pooling and its matching, taken; null-terminated
 * output, always.
 */
static void environment_attributes_are_set_and_read(trl_test *test) {
    SQLHENV env = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC2, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_CONNECTION_POOLING, (SQLPOINTER)SQL_CP_OFF, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_CP_MATCH, (SQLPOINTER)SQL_CP_STRICT_MATCH, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetEnvAttr(env, SQL_ATTR_OUTPUT_NTS, (SQLPOINTER)SQL_TRUE, 0) == SQL_SUCCESS);
    static const SQLINTEGER attributes[] = {SQL_ATTR_OUTPUT_NTS, SQL_ATTR_CONNECTION_POOLING, SQL_ATTR_CP_MATCH};
    SQLINTEGER value = 0;
    for (size_t i = 0U; i < sizeof attributes / sizeof attributes[0]; ++i) {
        TRL_CHECK(test, SQLGetEnvAttr(env, attributes[i], &value, 0, &value) == SQL_SUCCESS);
    }
    TRL_CHECK(test, SQLGetEnvAttr(env, SQL_ATTR_ODBC_VERSION, NULL, 0, NULL) == SQL_SUCCESS);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(escape_sequences_are_written_as_sql),
        TRL_TEST_CASE(escapes_at_their_edges),
        TRL_TEST_CASE(text_is_turned_to_and_from_sqlwchar),
        TRL_TEST_CASE(handles_are_allocated_in_order),
        TRL_TEST_CASE(every_entry_point_refuses_an_invalid_handle),
        TRL_TEST_CASE(environment_attributes_are_set_and_read),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
