/**
 * @file odbc_test.c
 * @brief The ODBC driver against a real server (RULES.md R-TEST-8), its
 * functions called as a driver manager calls them: connecting by a
 * connection string, statements run directly and prepared, rows fetched
 * bound and by SQLGetData, the catalog functions, transactions and
 * diagnostics.
 */
#include "harness.h"
#include "odbc_live.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIPPED 77

/**
 * Verifies: [DESIGN] D2 the ODBC driver connects by a connection string,
 * its keys the driver's settings and ODBC's own; it tells what it and its
 * data source are, by SQLGetInfo; a connection string it cannot connect
 * by fails, its SQLSTATE the driver's, and a statement before connecting
 * is refused.
 */
static void the_driver_connects(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    TRL_CHECK(test, SQLGetInfo(o.dbc, SQL_DBMS_NAME, text, (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)text, "Turborel") == 0 && length == 8);
    TRL_CHECK(test, SQLGetInfo(o.dbc, SQL_USER_NAME, text, (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)text, "ANN") == 0);
    SQLUSMALLINT longest = 0;
    TRL_CHECK(test, SQLGetInfo(o.dbc, SQL_MAX_TABLE_NAME_LEN, &longest, 0, NULL) == SQL_SUCCESS && longest == 128U);
    TRL_CHECK(test, SQLGetInfo(o.dbc, SQL_DBMS_VER, text, 4, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "01004") == 0);
    TRL_CHECK(test, SQLGetInfo(o.dbc, 9999, text, (SQLSMALLINT)sizeof text, &length) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "HY096") == 0);
    SQLUSMALLINT functions[SQL_API_ODBC3_ALL_FUNCTIONS_SIZE];
    TRL_CHECK(test, SQLGetFunctions(o.dbc, SQL_API_ODBC3_ALL_FUNCTIONS, functions) == SQL_SUCCESS);
    TRL_CHECK(test, SQL_FUNC_EXISTS(functions, SQL_API_SQLFETCHSCROLL) == SQL_TRUE);
    TRL_CHECK(test, SQL_FUNC_EXISTS(functions, SQL_API_SQLBROWSECONNECT) == SQL_FALSE);
    odbc_close(&o);
    SQLHENV env = SQL_NULL_HANDLE;
    SQLHDBC dbc = SQL_NULL_HANDLE;
    (void)SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_ERROR);
    (void)SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_SUCCESS);
    SQLHSTMT stmt = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_DBC, dbc, NULL), "08003") == 0);
    SQLINTEGER native = 0;
    TRL_CHECK(test, SQLDriverConnect(dbc, NULL, sql_text("SERVER=127.0.0.1;PORT=1;UID=ann;TLS_MODE=disable"), SQL_NTS,
                                     NULL, 0, NULL, SQL_DRIVER_NOPROMPT) == SQL_ERROR);
    TRL_CHECK(test, strncmp(odbc_state(SQL_HANDLE_DBC, dbc, &native), "08", 2U) == 0 && native == -919);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_ENV, env) == SQL_ERROR);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_ENV, env) == SQL_SUCCESS);
}

/**
 * Verifies: [DESIGN] D2 statements run directly and prepared, with
 * parameters bound of several C types; the rows a statement changed
 * counted; a failure's SQLSTATE and SQLCODE in the diagnostics.
 */
static void statements_run_with_parameters(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE STAFF (ID INTEGER PRIMARY KEY, NAME VARCHAR(40), PAY NUMERIC(8,2), "
                                     "HIRED DATE, SEEN TIMESTAMP(3), OK BOOLEAN, R DOUBLE PRECISION)"));
    TRL_CHECK(test,
              SQLPrepare(o.stmt, sql_text("INSERT INTO STAFF VALUES (?, ?, ?, ?, ?, ?, ?)"), SQL_NTS) == SQL_SUCCESS);
    SQLSMALLINT params = 0;
    TRL_CHECK(test, SQLNumParams(o.stmt, &params) == SQL_SUCCESS && params == 7);
    SQLINTEGER id = 1;
    char name[41] = "Ann Àrbor";
    SQL_NUMERIC_STRUCT pay;
    memset(&pay, 0, sizeof pay);
    pay.precision = 8;
    pay.scale = 2;
    pay.sign = 1;
    pay.val[0] = 0x39;
    pay.val[1] = 0x30; /* 12345, 123.45 */
    DATE_STRUCT hired = {2020, 1, 31};
    TIMESTAMP_STRUCT seen = {2021, 2, 3, 4, 5, 6, 789000000U};
    unsigned char yes = 1U;
    double r = 2.5;
    SQLLEN nts = SQL_NTS;
    SQLLEN null_data = SQL_NULL_DATA;
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &id, 0, NULL);
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 40, 0, name, 0, &nts);
    (void)SQLBindParameter(o.stmt, 3, SQL_PARAM_INPUT, SQL_C_NUMERIC, SQL_NUMERIC, 8, 2, &pay, 0, NULL);
    (void)SQLBindParameter(o.stmt, 4, SQL_PARAM_INPUT, SQL_C_TYPE_DATE, SQL_TYPE_DATE, 0, 0, &hired, 0, NULL);
    (void)SQLBindParameter(o.stmt, 5, SQL_PARAM_INPUT, SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, 0, 3, &seen, 0, NULL);
    (void)SQLBindParameter(o.stmt, 6, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &yes, 0, NULL);
    (void)SQLBindParameter(o.stmt, 7, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &r, 0, NULL);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_SUCCESS);
    SQLLEN changed = 0;
    TRL_CHECK(test, SQLRowCount(o.stmt, &changed) == SQL_SUCCESS && changed == 1);
    id = 2;
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 40, 0, name, 0, &null_data);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_SUCCESS);
    SQLINTEGER native = 0;
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR);
    TRL_CHECK(test, strncmp(odbc_state(SQL_HANDLE_STMT, o.stmt, &native), "23", 2U) == 0 && native < 0);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a query's columns described, INTEGER as
 * SQL_INTEGER by its declared type, as SQLDescribeCol and SQLColAttribute
 * give them.
 */
static void columns_are_described(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT ID, NAME, PAY, HIRED, SEEN, OK, R FROM STAFF ORDER BY ID"),
                                  SQL_NTS) == SQL_SUCCESS);
    SQLSMALLINT columns = 0;
    TRL_CHECK(test, SQLNumResultCols(o.stmt, &columns) == SQL_SUCCESS && columns == 7);
    SQLCHAR column_name[ODBC_TEXT_BYTES];
    SQLSMALLINT name_length = 0;
    SQLSMALLINT sql_type = 0;
    SQLULEN size = 0;
    SQLSMALLINT digits = 0;
    SQLSMALLINT nullable = 0;
    TRL_CHECK(test, SQLDescribeCol(o.stmt, 1, column_name, (SQLSMALLINT)sizeof column_name, &name_length, &sql_type,
                                   &size, &digits, &nullable) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)column_name, "ID") == 0 && sql_type == SQL_INTEGER && size == 10U);
    TRL_CHECK(test, SQLDescribeCol(o.stmt, 3, column_name, (SQLSMALLINT)sizeof column_name, &name_length, &sql_type,
                                   &size, &digits, &nullable) == SQL_SUCCESS);
    TRL_CHECK(test, sql_type == SQL_NUMERIC && size == 8U && digits == 2);
    SQLLEN type_code = 0;
    TRL_CHECK(test, SQLColAttribute(o.stmt, 2, SQL_DESC_TYPE_NAME, column_name, (SQLSMALLINT)sizeof column_name,
                                    &name_length, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)column_name, "CHARACTER VARYING") == 0);
    TRL_CHECK(test, SQLColAttribute(o.stmt, 4, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &type_code) == SQL_SUCCESS);
    TRL_CHECK(test, type_code == SQL_TYPE_DATE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a query's rows fetched into bound columns, a
 * value cut at a whole character, and read by SQLGetData as the C types
 * asked for; a cursor closed once; a searched UPDATE of no row
 * SQL_NO_DATA.
 */
static void rows_are_fetched_as_c_types(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT ID, NAME, PAY, HIRED, SEEN, OK, R FROM STAFF ORDER BY ID"),
                                  SQL_NTS) == SQL_SUCCESS);
    SQLINTEGER got_id = 0;
    SQLLEN id_length = 0;
    char got_name[6];
    SQLLEN got_name_length = 0;
    (void)SQLBindCol(o.stmt, 1, SQL_C_SLONG, &got_id, 0, &id_length);
    (void)SQLBindCol(o.stmt, 2, SQL_C_CHAR, got_name, (SQLLEN)sizeof got_name, &got_name_length);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS_WITH_INFO && got_id == 1);
    TRL_CHECK(test, strcmp(got_name, "Ann ") == 0 && got_name_length == (SQLLEN)strlen("Ann Àrbor"));
    char text[ODBC_TEXT_BYTES];
    SQLLEN text_length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_CHAR, text, (SQLLEN)sizeof text, &text_length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp(text, "123.45") == 0);
    DATE_STRUCT got_date;
    TRL_CHECK(test, SQLGetData(o.stmt, 4, SQL_C_TYPE_DATE, &got_date, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, got_date.year == 2020 && got_date.month == 1U && got_date.day == 31U);
    TIMESTAMP_STRUCT got_seen;
    TRL_CHECK(test, SQLGetData(o.stmt, 5, SQL_C_TYPE_TIMESTAMP, &got_seen, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, got_seen.hour == 4U && got_seen.fraction == 789000000U);
    unsigned char got_ok = 9U;
    double got_r = 0.0;
    TRL_CHECK(test, SQLGetData(o.stmt, 6, SQL_C_BIT, &got_ok, 0, NULL) == SQL_SUCCESS && got_ok == 1U);
    TRL_CHECK(test, SQLGetData(o.stmt, 7, SQL_C_DOUBLE, &got_r, 0, NULL) == SQL_SUCCESS && got_r == 2.5);
    char piece[4];
    SQLLEN piece_length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 6, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &piece_length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp(piece, "1") == 0);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS && got_id == 2 && got_name_length == SQL_NULL_DATA);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_NO_DATA);
    TRL_CHECK(test, SQLCloseCursor(o.stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLCloseCursor(o.stmt) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "24000") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("UPDATE STAFF SET R = 1 WHERE ID = 99"), SQL_NTS) == SQL_NO_DATA);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE STAFF CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a rowset of several rows fetched into columns
 * bound column-wise, each row's status and the rows fetched told; a
 * static cursor scrolled to its first and its last rowset; a long value
 * read by SQLGetData a piece at a time, each piece cut at a character.
 */
static void rowsets_scroll_and_long_values_come_in_pieces(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE N (K INTEGER, T VARCHAR(100))"));
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO N VALUES (1, 'one'), (2, 'two'), (3, 'three'), (4, 'four'), (5, "
                                     "'fivefivefivefivefivefive')"));
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_STATIC, 0) == SQL_SUCCESS);
    SQLINTEGER keys[2] = {0, 0};
    SQLLEN key_lengths[2] = {0, 0};
    SQLUSMALLINT statuses[2] = {0U, 0U};
    SQLULEN fetched = 0U;
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)2, 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_STATUS_PTR, statuses, 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT K, T FROM N ORDER BY K"), SQL_NTS) == SQL_SUCCESS);
    (void)SQLBindCol(o.stmt, 1, SQL_C_SLONG, keys, 0, key_lengths);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_NEXT, 0) == SQL_SUCCESS && fetched == 2U);
    TRL_CHECK(test, keys[0] == 1 && keys[1] == 2 && statuses[1] == SQL_ROW_SUCCESS);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_LAST, 0) == SQL_SUCCESS && keys[0] == 4 && keys[1] == 5);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_NEXT, 0) == SQL_NO_DATA);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_ABSOLUTE, 5) == SQL_SUCCESS && fetched == 1U && keys[0] == 5);
    TRL_CHECK(test, statuses[1] == SQL_ROW_NOROW);
    char piece[10];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, strcmp(piece, "fivefivef") == 0 && length == 24);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp(piece, "vefive") == 0 && length == 6);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &length) == SQL_NO_DATA);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_FIRST, 0) == SQL_SUCCESS && keys[0] == 1);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE N CASCADE"));
    odbc_close(&o);
}

/* The rows a catalog function gave, its cursor closed after. */
static long rows_of(SQLHSTMT stmt) {
    long rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(stmt))) {
        ++rows;
    }
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    return rows;
}

/**
 * Verifies: [DESIGN] D2 the catalog functions list what the Information
 * Schema has: tables and their types, columns with ODBC's DATA_TYPE, a
 * pattern's escape taken.
 */
static void the_catalog_functions_list_what_there_is(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE P_1 (K INTEGER PRIMARY KEY, U VARCHAR(5) UNIQUE)"));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE PX1 (K INTEGER)"));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE F (K INTEGER, P INTEGER REFERENCES P_1 (K) ON DELETE CASCADE)"));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE VIEW V AS SELECT K FROM F"));
    TRL_CHECK(test, odbc_said(o.stmt, SQLTables(o.stmt, NULL, 0, sql_text("ANN"), SQL_NTS, sql_text("%"), SQL_NTS, NULL,
                                                0)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 4L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLTables(o.stmt, NULL, 0, sql_text("ANN"), SQL_NTS, sql_text("P\\_1"), SQL_NTS,
                                                NULL, 0)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 1L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLTables(o.stmt, NULL, 0, sql_text("ANN"), SQL_NTS, NULL, 0, sql_text("'VIEW'"),
                                                SQL_NTS)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 1L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLTables(o.stmt, sql_text(""), 0, sql_text(""), 0, sql_text(""), 0,
                                                sql_text("%"), SQL_NTS)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 4L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLColumns(o.stmt, NULL, 0, sql_text("ANN"), SQL_NTS, sql_text("P\\_1"), SQL_NTS,
                                                 NULL, 0)) == SQL_SUCCESS);
    SQLSMALLINT data_type = 0;
    SQLINTEGER column_size = 0;
    TRL_CHECK(test, SQL_SUCCEEDED(SQLFetch(o.stmt)));
    TRL_CHECK(test,
              SQLGetData(o.stmt, 5, SQL_C_SSHORT, &data_type, 0, NULL) == SQL_SUCCESS && data_type == SQL_INTEGER);
    TRL_CHECK(test, SQL_SUCCEEDED(SQLFetch(o.stmt)));
    TRL_CHECK(test, odbc_said(o.stmt, SQLGetData(o.stmt, 7, SQL_C_SLONG, &column_size, 0, NULL)) == SQL_SUCCESS);
    TRL_CHECK(test, column_size == 5);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 the catalog functions list keys as the
 * Information Schema has them: primary and foreign keys, unique keys as
 * statistics and the best row identifier, and the types the driver
 * describes; a table's name required where it is.
 */
static void the_catalog_functions_list_keys_and_types(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_said(o.stmt, SQLPrimaryKeys(o.stmt, NULL, 0, sql_text("ANN"), SQL_NTS, sql_text("P_1"),
                                                     SQL_NTS)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 1L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLForeignKeys(o.stmt, NULL, 0, NULL, 0, sql_text("P_1"), SQL_NTS, NULL, 0, NULL,
                                                     0, NULL, 0)) == SQL_SUCCESS);
    SQLSMALLINT rule = 9;
    TRL_CHECK(test, SQL_SUCCEEDED(SQLFetch(o.stmt)));
    TRL_CHECK(test, SQLGetData(o.stmt, 11, SQL_C_SSHORT, &rule, 0, NULL) == SQL_SUCCESS && rule == SQL_CASCADE);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, odbc_said(o.stmt, SQLStatistics(o.stmt, NULL, 0, NULL, 0, sql_text("P_1"), SQL_NTS, SQL_INDEX_ALL,
                                                    SQL_QUICK)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 2L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLSpecialColumns(o.stmt, SQL_BEST_ROWID, NULL, 0, NULL, 0, sql_text("P_1"),
                                                        SQL_NTS, SQL_SCOPE_SESSION, SQL_NULLABLE)) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt) == 1L);
    TRL_CHECK(test, SQLGetTypeInfo(o.stmt, SQL_INTEGER) == SQL_SUCCESS && rows_of(o.stmt) == 1L);
    TRL_CHECK(test, SQLGetTypeInfo(o.stmt, SQL_ALL_TYPES) == SQL_SUCCESS && rows_of(o.stmt) == 19L);
    TRL_CHECK(test, odbc_said(o.stmt, SQLPrimaryKeys(o.stmt, NULL, 0, NULL, 0, NULL, 0)) == SQL_ERROR);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE F CASCADE") && odbc_run(o.stmt, "DROP TABLE PX1 CASCADE") &&
                        odbc_run(o.stmt, "DROP TABLE P_1 CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 autocommit on by default, each statement its own
 * transaction; off, SQLEndTran commits or rolls back; an escape sequence
 * written as SQL, as SQLNativeSql shows; the Unicode functions take and
 * give SQLWCHAR.
 */
static void transactions_escapes_and_unicode(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE T (K INTEGER)"));
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO T VALUES (1)"));
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_DBC, o.dbc, SQL_ROLLBACK) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO T VALUES (2)"));
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_ENV, o.env, SQL_COMMIT) == SQL_SUCCESS);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT {fn UCASE('x')}, {d '2020-02-03'}, K FROM T"), SQL_NTS) ==
                        SQL_SUCCESS);
    char text[ODBC_TEXT_BYTES];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS &&
                        strcmp(text, "X") == 0);
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS &&
                        strcmp(text, "2") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    SQLCHAR native[ODBC_TEXT_BYTES];
    SQLINTEGER native_length = 0;
    TRL_CHECK(test, SQLNativeSql(o.dbc,
                                 sql_text("SELECT {fn CONCAT('a', 'b')} FROM {oj T LEFT OUTER JOIN T AS U ON "
                                          "T.K = U.K}"),
                                 SQL_NTS, native, (SQLINTEGER)sizeof native, &native_length) == SQL_SUCCESS);
    TRL_CHECK(test,
              strcmp((const char *)native, "SELECT ('a' ||  'b') FROM T LEFT OUTER JOIN T AS U ON T.K = U.K") == 0);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT {fn NOPE('a'}"), SQL_NTS) == SQL_ERROR);
    SQLWCHAR sql[] = {'V', 'A', 'L', 'U', 'E', 'S', ' ', '\'', 0xE9, '\'', 0};
    TRL_CHECK(test, SQLExecDirectW(o.stmt, sql, SQL_NTS) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS);
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length) == SQL_SUCCESS);
    TRL_CHECK(test, wide[0] == 0xE9 && wide[1] == 0 && length == (SQLLEN)sizeof(SQLWCHAR));
    SQLWCHAR state[6];
    SQLWCHAR message[ODBC_TEXT_BYTES];
    SQLSMALLINT message_length = 0;
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    SQLWCHAR bad[] = {'S', 'E', 'L', 'E', 'C', 'T', 0};
    TRL_CHECK(test, SQLExecDirectW(o.stmt, bad, SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDiagRecW(SQL_HANDLE_STMT, o.stmt, 1, state, NULL, message, ODBC_TEXT_BYTES,
                                   &message_length) == SQL_SUCCESS);
    TRL_CHECK(test, state[0] == '2' && state[1] == 'A' && message_length > 0);
    TRL_CHECK(test, SQLGetDiagRec(SQL_HANDLE_STMT, o.stmt, 2, NULL, NULL, NULL, 0, NULL) == SQL_NO_DATA);
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE T CASCADE"));
    odbc_close(&o);
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_driver_connects),
        TRL_TEST_CASE(statements_run_with_parameters),
        TRL_TEST_CASE(columns_are_described),
        TRL_TEST_CASE(rows_are_fetched_as_c_types),
        TRL_TEST_CASE(rowsets_scroll_and_long_values_come_in_pieces),
        TRL_TEST_CASE(the_catalog_functions_list_what_there_is),
        TRL_TEST_CASE(the_catalog_functions_list_keys_and_types),
        TRL_TEST_CASE(transactions_escapes_and_unicode),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
