/**
 * @file odbc_handles_test.c
 * @brief The ODBC driver's handles against a real server (RULES.md
 * R-TEST-8): connection and statement attributes, each function's
 * refusals and the diagnostics they leave, cursor names, and what
 * SQLGetInfo tells.
 */
#include "harness.h"
#include "odbc_live.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKIPPED 77

/**
 * Verifies: [DESIGN] D2 connection attributes set and read: autocommit,
 * turned on committing what was begun; the login timeout, access mode,
 * isolation, catalog, whether the connection is dead; one the driver takes
 * another value of changed (01S02); one that is none refused (HY092).
 */
static void connection_attributes_are_set_and_read(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLUINTEGER number = 9U;
    TRL_CHECK(test, SQLGetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, &number, 0, NULL) == SQL_SUCCESS &&
                        number == SQL_AUTOCOMMIT_ON);
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE A (K INTEGER)"));
    (void)SQLSetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO A VALUES (1)"));
    TRL_CHECK(test, SQLDisconnect(o.dbc) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "25000") == 0);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_DBC, o.dbc, SQL_ROLLBACK) == SQL_SUCCESS);
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_DBC, o.dbc, 7) == SQL_ERROR);
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_STMT, o.stmt, SQL_COMMIT) == SQL_INVALID_HANDLE);
    static const SQLINTEGER attributes[] = {
        SQL_ATTR_LOGIN_TIMEOUT, SQL_ATTR_ACCESS_MODE,  SQL_ATTR_TXN_ISOLATION, SQL_ATTR_CONNECTION_DEAD,
        SQL_ATTR_METADATA_ID,   SQL_ATTR_ASYNC_ENABLE, SQL_ATTR_AUTO_IPD,      SQL_ATTR_PACKET_SIZE,
    };
    bool every = true;
    for (size_t i = 0U; i < sizeof attributes / sizeof attributes[0]; ++i) {
        every = every && SQLGetConnectAttr(o.dbc, attributes[i], &number, 0, NULL) == SQL_SUCCESS;
    }
    TRL_CHECK(test, every);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_ACCESS_MODE, (SQLPOINTER)SQL_MODE_READ_ONLY, 0) == SQL_SUCCESS);
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    TRL_CHECK(test,
              SQLGetInfo(o.dbc, SQL_DATA_SOURCE_READ_ONLY, text, (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)text, "Y") == 0);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_TXN_ISOLATION, (SQLPOINTER)SQL_TXN_READ_COMMITTED, 0) ==
                        SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)5, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_PACKET_SIZE, (SQLPOINTER)5, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, 9999, NULL, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLGetConnectAttr(o.dbc, 9999, &number, 0, NULL) == SQL_ERROR);
    SQLINTEGER catalog_length = 0;
    TRL_CHECK(test, SQLGetConnectAttr(o.dbc, SQL_ATTR_CURRENT_CATALOG, text, (SQLINTEGER)sizeof text,
                                      &catalog_length) == SQL_SUCCESS);
    TRL_CHECK(test, catalog_length > 0);
    TRL_CHECK(test, SQLGetConnectAttrW(o.dbc, SQL_ATTR_AUTOCOMMIT, &number, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetConnectAttrW(o.dbc, SQL_ATTR_QUIET_MODE, NULL, 0) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE A CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 the connection attributes the driver takes and
 * does nothing with, set; those it tells, read with no buffer; the
 * isolation it has, set.
 */
static void connection_attributes_the_driver_takes(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    static const SQLINTEGER taken[] = {
        SQL_ATTR_CONNECTION_TIMEOUT, SQL_ATTR_ASYNC_ENABLE,  SQL_ATTR_ODBC_CURSORS,     SQL_ATTR_TRACE,
        SQL_ATTR_TRACEFILE,          SQL_ATTR_TRANSLATE_LIB, SQL_ATTR_TRANSLATE_OPTION, SQL_ATTR_CURRENT_CATALOG,
    };
    bool every = true;
    for (size_t i = 0U; i < sizeof taken / sizeof taken[0]; ++i) {
        every = every && SQLSetConnectAttr(o.dbc, taken[i], NULL, 0) == SQL_SUCCESS;
    }
    static const SQLINTEGER told[] = {
        SQL_ATTR_CONNECTION_TIMEOUT, SQL_ATTR_QUIET_MODE, SQL_ATTR_ACCESS_MODE,
        SQL_ATTR_METADATA_ID,        SQL_ATTR_AUTOCOMMIT,
    };
    for (size_t i = 0U; i < sizeof told / sizeof told[0]; ++i) {
        SQLINTEGER said = 0;
        every = every && SQLGetConnectAttr(o.dbc, told[i], NULL, 0, &said) == SQL_SUCCESS;
    }
    TRL_CHECK(test, every);
    TRL_CHECK(test,
              SQLSetConnectAttr(o.dbc, SQL_ATTR_TXN_ISOLATION, (SQLPOINTER)SQL_TXN_SERIALIZABLE, 0) == SQL_SUCCESS);
    odbc_close(&o);
}

/* Whether each statement attribute the driver tells is read of `stmt`. */
static bool every_statement_attribute_read(SQLHSTMT stmt) {
    static const SQLINTEGER numbers[] = {
        SQL_ATTR_ROW_ARRAY_SIZE,
        SQL_ATTR_ROW_BIND_TYPE,
        SQL_ATTR_PARAM_BIND_TYPE,
        SQL_ATTR_PARAMSET_SIZE,
        SQL_ATTR_CURSOR_TYPE,
        SQL_ATTR_CURSOR_SCROLLABLE,
        SQL_ATTR_CONCURRENCY,
        SQL_ATTR_CURSOR_SENSITIVITY,
        SQL_ATTR_MAX_ROWS,
        SQL_ATTR_QUERY_TIMEOUT,
        SQL_ATTR_NOSCAN,
        SQL_ATTR_METADATA_ID,
        SQL_ATTR_ROW_NUMBER,
        SQL_ATTR_USE_BOOKMARKS,
        SQL_ATTR_RETRIEVE_DATA,
        SQL_ATTR_APP_ROW_DESC,
        SQL_ATTR_APP_PARAM_DESC,
        SQL_ATTR_IMP_ROW_DESC,
        SQL_ATTR_IMP_PARAM_DESC,
        SQL_ATTR_ROW_STATUS_PTR,
        SQL_ATTR_ROWS_FETCHED_PTR,
        SQL_ATTR_ROW_BIND_OFFSET_PTR,
        SQL_ATTR_PARAM_BIND_OFFSET_PTR,
        SQL_ATTR_PARAM_STATUS_PTR,
        SQL_ATTR_PARAMS_PROCESSED_PTR,
        SQL_ATTR_PARAM_OPERATION_PTR,
    };
    SQLULEN value = 0U;
    SQLINTEGER length = 0;
    bool every = true;
    for (size_t i = 0U; i < sizeof numbers / sizeof numbers[0]; ++i) {
        every = every && SQLGetStmtAttr(stmt, numbers[i], &value, 0, &length) == SQL_SUCCESS;
    }
    return every;
}

/**
 * Verifies: [DESIGN] D2 statement attributes set and read: the rowset's
 * size and binding, the descriptors in use, the cursor's type, scrollable
 * or not, its concurrency and sensitivity, the most rows, the escape
 * scan, a value the driver takes another of changed (01S02), one that is
 * none refused (HY092), an implementation descriptor not set (HY017).
 */
static void statement_attributes_are_set_and_read(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLULEN value = 0U;
    TRL_CHECK(test, every_statement_attribute_read(o.stmt));
    TRL_CHECK(test, SQLGetStmtAttrW(o.stmt, SQL_ATTR_MAX_ROWS, &value, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, 9999, &value, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, 9999, NULL, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_KEYSET_DRIVEN, 0) ==
                        SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "01S02") == 0);
    TRL_CHECK(test,
              SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_SCROLLABLE, (SQLPOINTER)SQL_NONSCROLLABLE, 0) == SQL_SUCCESS);
    TRL_CHECK(test,
              SQLSetStmtAttr(o.stmt, SQL_ATTR_CONCURRENCY, (SQLPOINTER)SQL_CONCUR_LOCK, 0) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_SENSITIVITY, (SQLPOINTER)SQL_SENSITIVE, 0) ==
                        SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_USE_BOOKMARKS, (SQLPOINTER)SQL_UB_ON, 0) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_IMP_ROW_DESC, NULL, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_KEYSET_SIZE, (SQLPOINTER)1, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_MAX_ROWS, (SQLPOINTER)1, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)3, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttrW(o.stmt, SQL_ROWSET_SIZE, (SQLPOINTER)0, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1, 2"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_NO_DATA);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_NOSCAN, (SQLPOINTER)SQL_NOSCAN_ON, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES {fn UCASE('a')}"), SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLFreeStmt(o.stmt, 99) == SQL_ERROR);
    SQLHSTMT dropped = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_STMT, o.dbc, &dropped) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFreeStmt(dropped, SQL_DROP) == SQL_SUCCESS);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 the statement attributes the driver takes and does
 * nothing with, set; the values it has of the cursor's, set; those it
 * tells, read with no buffer.
 */
static void statement_attributes_the_driver_takes(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLULEN value = 0U;
    SQLINTEGER length = 0;
    bool every = true;
    static const SQLINTEGER taken[] = {
        SQL_ATTR_MAX_LENGTH,       SQL_ATTR_KEYSET_SIZE,          SQL_ATTR_ASYNC_ENABLE,
        SQL_ATTR_RETRIEVE_DATA,    SQL_ATTR_SIMULATE_CURSOR,      SQL_ATTR_ENABLE_AUTO_IPD,
        SQL_ATTR_PARAM_STATUS_PTR, SQL_ATTR_PARAMS_PROCESSED_PTR, SQL_ATTR_PARAM_OPERATION_PTR,
    };
    for (size_t i = 0U; i < sizeof taken / sizeof taken[0]; ++i) {
        every = every && SQLSetStmtAttr(o.stmt, taken[i], NULL, 0) == SQL_SUCCESS;
    }
    TRL_CHECK(test, every && SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_SCROLLABLE, (SQLPOINTER)SQL_SCROLLABLE, 0) ==
                                 SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_CONCURRENCY, (SQLPOINTER)SQL_CONCUR_READ_ONLY, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_SENSITIVITY, (SQLPOINTER)SQL_INSENSITIVE, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_USE_BOOKMARKS, (SQLPOINTER)SQL_UB_OFF, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ATTR_METADATA_ID, NULL, 0, &length) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ATTR_APP_ROW_DESC, NULL, 0, &length) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ATTR_CURSOR_SCROLLABLE, &value, 0, NULL) == SQL_SUCCESS &&
                        value == SQL_SCROLLABLE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a statement's diagnostics: the header's number,
 * return code, row counts and dynamic function, each record's SQLSTATE,
 * native code, message, origins, server and connection names, column and
 * row numbers, as text and SQLWCHAR; a record past the last none; a
 * record's number below 1, or a buffer's length below 0, refused.
 */
static void diagnostics_tell_each_field(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT FROM"), SQL_NTS) == SQL_ERROR);
    SQLINTEGER number = 0;
    SQLRETURN returned = SQL_SUCCESS;
    SQLLEN rows = 0;
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_NUMBER, &number, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test,
              SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_RETURNCODE, &returned, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, returned == SQL_ERROR && number == 1);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_ROW_COUNT, &rows, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test,
              SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_CURSOR_ROW_COUNT, &rows, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_DYNAMIC_FUNCTION_CODE, &number, 0, NULL) ==
                        SQL_SUCCESS);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_DYNAMIC_FUNCTION, text,
                                    (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS);
    static const SQLSMALLINT texts[] = {
        SQL_DIAG_SQLSTATE,        SQL_DIAG_MESSAGE_TEXT,    SQL_DIAG_CLASS_ORIGIN,
        SQL_DIAG_SUBCLASS_ORIGIN, SQL_DIAG_CONNECTION_NAME, SQL_DIAG_SERVER_NAME,
    };
    bool every = true;
    for (size_t i = 0U; i < sizeof texts / sizeof texts[0]; ++i) {
        every = every && SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 1, texts[i], text, (SQLSMALLINT)sizeof text,
                                         &length) == SQL_SUCCESS;
    }
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    every = every && SQLGetDiagFieldW(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_MESSAGE_TEXT, wide, (SQLSMALLINT)sizeof wide,
                                      &length) == SQL_SUCCESS;
    every = every && SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_NATIVE, &number, 0, NULL) == SQL_SUCCESS;
    every =
        every && SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_COLUMN_NUMBER, &number, 0, NULL) == SQL_SUCCESS;
    every = every && SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_ROW_NUMBER, &rows, 0, NULL) == SQL_SUCCESS;
    TRL_CHECK(test, every && number != 0);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_SQLSTATE, text, 6, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_DBC, o.dbc, 0, SQL_DIAG_ROW_COUNT, &rows, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test,
              SQLGetDiagField(SQL_HANDLE_DBC, o.dbc, 0, SQL_DIAG_DYNAMIC_FUNCTION_CODE, &number, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_DBC, o.dbc, 1, SQL_DIAG_SERVER_NAME, text, 6, NULL) == SQL_NO_DATA);
    TRL_CHECK(test, SQLGetDiagRec(SQL_HANDLE_STMT, o.stmt, 0, text, NULL, NULL, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDiagRec(SQL_HANDLE_STMT, o.stmt, 1, text, NULL, text, -1, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDiagRec(SQL_HANDLE_STMT, o.stmt, 1, NULL, NULL, text, 4, &length) == SQL_SUCCESS_WITH_INFO);
    SQLHDESC ird = odbc_descriptor(o.stmt, SQL_ATTR_IMP_ROW_DESC);
    (void)SQLSetDescField(ird, 1, SQL_DESC_TYPE, NULL, 0);
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_DESC, ird, 1, SQL_DIAG_SERVER_NAME, text, (SQLSMALLINT)sizeof text,
                                    NULL) == SQL_SUCCESS);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 each statement function refused in its sequence:
 * SQLExecute, SQLNumParams and SQLDescribeParam with nothing prepared
 * (HY010), SQLExecute with a cursor open (24000), a parameter or column
 * past the last (07009), a null pointer (HY009), data at execution never
 * asked for (HY010), positioned operations (HYC00); SQLMoreResults none;
 * a statement prepared described before it runs.
 */
static void statements_are_refused_out_of_sequence(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLSMALLINT count = 0;
    SQLSMALLINT type = 0;
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && SQLNumParams(o.stmt, &count) == SQL_ERROR);
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 1, &type, NULL, NULL, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLPrepare(o.stmt,
                               sql_text("SELECT CAST(? AS INTEGER) AS K FROM INFORMATION_SCHEMA."
                                        "INFORMATION_SCHEMA_CATALOG_NAME"),
                               SQL_NTS) == SQL_SUCCESS);
    SQLULEN size = 0;
    SQLSMALLINT digits = 0;
    SQLSMALLINT nullable = 0;
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 1, &type, &size, &digits, &nullable) == SQL_SUCCESS);
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 2, &type, &size, &digits, &nullable) == SQL_ERROR);
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 1, NULL, NULL, NULL, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLNumResultCols(o.stmt, &count) == SQL_SUCCESS && count == 1 &&
                        SQLNumResultCols(o.stmt, NULL) == SQL_ERROR);
    SQLCHAR name[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    TRL_CHECK(test, SQLDescribeCol(o.stmt, 1, name, (SQLSMALLINT)sizeof name, &length, &type, &size, &digits,
                                   &nullable) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)name, "K") == 0 && type == SQL_INTEGER);
    TRL_CHECK(test,
              SQLDescribeCol(o.stmt, 2, name, (SQLSMALLINT)sizeof name, &length, NULL, NULL, NULL, NULL) == SQL_ERROR);
    int32_t key = 7;
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &key, 0, NULL);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "24000") == 0);
    SQLLEN rows = 0;
    TRL_CHECK(test, SQLRowCount(o.stmt, &rows) == SQL_SUCCESS && rows == 1 && SQLRowCount(o.stmt, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLSetPos(o.stmt, 1, SQL_POSITION, SQL_LOCK_NO_CHANGE) == SQL_ERROR);
    TRL_CHECK(test, SQLMoreResults(o.stmt) == SQL_NO_DATA);
    SQLPOINTER token = NULL;
    TRL_CHECK(test, SQLParamData(o.stmt, &token) == SQL_ERROR && SQLPutData(o.stmt, NULL, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLCancel(o.stmt) == SQL_SUCCESS);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a cursor's name set and read, as text and
 * SQLWCHAR, an empty or null one refused (34000, HY009); a statement's
 * text null refused (HY009), one the server cannot prepare its failure,
 * an escape not closed refused (42000); SQLNativeSql's escapes written,
 * one that is none refused.
 */
static void cursor_names_and_native_sql(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLCHAR name[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    TRL_CHECK(test, SQLSetCursorName(o.stmt, sql_text("C1"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetCursorName(o.stmt, name, (SQLSMALLINT)sizeof name, &length) == SQL_SUCCESS &&
                        strcmp((const char *)name, "C1") == 0);
    TRL_CHECK(test, SQLSetCursorName(o.stmt, sql_text(""), SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLSetCursorName(o.stmt, NULL, SQL_NTS) == SQL_ERROR);
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    SQLWCHAR cursor[] = {'C', '2', 0};
    TRL_CHECK(test, SQLSetCursorNameW(o.stmt, cursor, SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetCursorNameW(o.stmt, wide, ODBC_TEXT_BYTES, &length) == SQL_SUCCESS && wide[1] == '2');
    TRL_CHECK(test,
              SQLExecDirect(o.stmt, NULL, SQL_NTS) == SQL_ERROR && SQLPrepare(o.stmt, NULL, SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLExecDirectW(o.stmt, NULL, SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("SELECT FROM"), SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("VALUES {d '1'"), SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "42000") == 0);
    SQLCHAR native[ODBC_TEXT_BYTES];
    SQLINTEGER native_length = 0;
    TRL_CHECK(test,
              SQLNativeSql(o.dbc, sql_text("{x}"), SQL_NTS, native, ODBC_TEXT_BYTES, &native_length) == SQL_ERROR);
    TRL_CHECK(test, SQLNativeSql(o.dbc, NULL, SQL_NTS, native, ODBC_TEXT_BYTES, &native_length) == SQL_ERROR);
    SQLWCHAR in[] = {'{', 'd', ' ', '\'', '1', '\'', '}', 0};
    TRL_CHECK(test, SQLNativeSqlW(o.dbc, in, SQL_NTS, wide, ODBC_TEXT_BYTES, &native_length) == SQL_SUCCESS);
    TRL_CHECK(test, SQLNativeSqlW(o.dbc, NULL, SQL_NTS, wide, ODBC_TEXT_BYTES, &native_length) == SQL_ERROR);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 SQLGetInfo answers each type of its list, as text
 * and SQLWCHAR; SQLGetFunctions says each function the driver has, by
 * ODBC 3's bitmap and ODBC 2's list.
 */
static void info_and_functions_are_told(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    bool every = true;
    for (SQLUSMALLINT type = 0U; type < 200U; ++type) {
        SQLRETURN answered = SQLGetInfo(o.dbc, type, text, (SQLSMALLINT)sizeof text, &length);
        every = every && (SQL_SUCCEEDED(answered) || strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "HY096") == 0);
    }
    TRL_CHECK(test, every);
    TRL_CHECK(test, SQLGetInfoW(o.dbc, SQL_DRIVER_NAME, wide, (SQLSMALLINT)sizeof wide, &length) == SQL_SUCCESS);
    TRL_CHECK(test, length == (SQLSMALLINT)(15U * sizeof(SQLWCHAR)));
    TRL_CHECK(test, SQLGetInfo(o.dbc, SQL_ODBC_INTERFACE_CONFORMANCE, NULL, 0, &length) == SQL_SUCCESS);
    SQLUSMALLINT supported[100];
    TRL_CHECK(test, SQLGetFunctions(o.dbc, SQL_API_ALL_FUNCTIONS, supported) == SQL_SUCCESS &&
                        supported[SQL_API_SQLFETCH] == SQL_TRUE);
    TRL_CHECK(test, SQLGetFunctions(o.dbc, SQL_API_SQLTABLES, supported) == SQL_SUCCESS && supported[0] == SQL_TRUE);
    TRL_CHECK(test,
              SQLGetFunctions(o.dbc, SQL_API_SQLBROWSECONNECT, supported) == SQL_SUCCESS && supported[0] == SQL_FALSE);
    TRL_CHECK(test, SQLGetFunctions(o.dbc, SQL_API_SQLTABLES, NULL) == SQL_ERROR);
    odbc_close(&o);
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc-handles", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(connection_attributes_are_set_and_read),
        TRL_TEST_CASE(connection_attributes_the_driver_takes),
        TRL_TEST_CASE(statement_attributes_are_set_and_read),
        TRL_TEST_CASE(statement_attributes_the_driver_takes),
        TRL_TEST_CASE(diagnostics_tell_each_field),
        TRL_TEST_CASE(statements_are_refused_out_of_sequence),
        TRL_TEST_CASE(cursor_names_and_native_sql),
        TRL_TEST_CASE(info_and_functions_are_told),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
