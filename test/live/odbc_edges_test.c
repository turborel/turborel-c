/**
 * @file odbc_edges_test.c
 * @brief The ODBC driver at its edges against a real server (RULES.md
 * R-TEST-8): attributes read back as they were set, a connection's
 * attributes once it is gone, scrolling past either end and the most
 * rows, parameters of ODBC 2's C types and of none, and values given into
 * buffers too small or of no length.
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

/* Whether connection attribute `attribute` reads as `expected`. */
static bool connection_reads(SQLHDBC dbc, SQLINTEGER attribute, SQLUINTEGER expected) {
    SQLUINTEGER told = 0U;
    return SQLGetConnectAttr(dbc, attribute, &told, 0, NULL) == SQL_SUCCESS && told == expected;
}

/* Whether statement attribute `attribute` reads as `expected`. */
static bool statement_reads(SQLHSTMT stmt, SQLINTEGER attribute, SQLULEN expected) {
    SQLULEN told = 0U;
    return SQLGetStmtAttr(stmt, attribute, &told, 0, NULL) == SQL_SUCCESS && told == expected;
}

/**
 * Verifies: [DESIGN] D2 attributes read back as set: autocommit off,
 * READ ONLY, the metadata ID; of a statement, the escape scan off and a
 * sensitivity unspecified, those the driver takes read as none; once
 * disconnected, the connection dead, its catalog empty, SQLEndTran
 * refused (08003), the environment's SQLEndTran of none ending nothing.
 */
static void attributes_read_back_as_set(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    (void)SQLSetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    (void)SQLSetConnectAttr(o.dbc, SQL_ATTR_ACCESS_MODE, (SQLPOINTER)SQL_MODE_READ_ONLY, 0);
    (void)SQLSetConnectAttr(o.dbc, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0);
    TRL_CHECK(test, connection_reads(o.dbc, SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF) &&
                        connection_reads(o.dbc, SQL_ATTR_ACCESS_MODE, SQL_MODE_READ_ONLY) &&
                        connection_reads(o.dbc, SQL_ATTR_METADATA_ID, SQL_TRUE) &&
                        connection_reads(o.dbc, SQL_ATTR_CONNECTION_DEAD, SQL_CD_FALSE));
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_NOSCAN, (SQLPOINTER)SQL_NOSCAN_ON, 0) == SQL_SUCCESS &&
                        statement_reads(o.stmt, SQL_ATTR_NOSCAN, SQL_NOSCAN_ON));
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_SENSITIVITY, (SQLPOINTER)SQL_UNSPECIFIED, 0) == SQL_SUCCESS);
    static const SQLINTEGER none[] = {
        SQL_ATTR_MAX_LENGTH,      SQL_ATTR_KEYSET_SIZE,     SQL_ATTR_ASYNC_ENABLE,
        SQL_ATTR_ENABLE_AUTO_IPD, SQL_ATTR_SIMULATE_CURSOR,
    };
    bool every = true;
    for (size_t i = 0U; i < sizeof none / sizeof none[0]; ++i) {
        SQLULEN told = 9U;
        every = every && SQLGetStmtAttr(o.stmt, none[i], &told, 0, NULL) == SQL_SUCCESS;
    }
    TRL_CHECK(test, every && SQLEndTran(SQL_HANDLE_ENV, o.env, SQL_ROLLBACK) == SQL_SUCCESS);
    (void)SQLSetConnectAttr(o.dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    (void)SQLFreeHandle(SQL_HANDLE_STMT, o.stmt);
    o.stmt = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLDisconnect(o.dbc) == SQL_SUCCESS);
    TRL_CHECK(test, connection_reads(o.dbc, SQL_ATTR_CONNECTION_DEAD, SQL_CD_TRUE));
    SQLCHAR catalog[ODBC_TEXT_BYTES] = "x";
    TRL_CHECK(test, SQLGetConnectAttr(o.dbc, SQL_ATTR_CURRENT_CATALOG, catalog, (SQLINTEGER)sizeof catalog, NULL) ==
                            SQL_SUCCESS &&
                        catalog[0] == '\0');
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_DBC, o.dbc, SQL_COMMIT) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "08003") == 0);
    TRL_CHECK(test, SQLEndTran(SQL_HANDLE_ENV, o.env, SQL_COMMIT) == SQL_SUCCESS);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, o.dbc);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, o.env);
}

/* Fetches as `orientation` and `offset` say: the first key of the rowset, or -1 of none. */
static int fetched(SQLHSTMT stmt, SQLSMALLINT orientation, SQLLEN offset, const int32_t *key) {
    return SQLFetchScroll(stmt, orientation, offset) == SQL_SUCCESS ? (int)*key : -1;
}

/**
 * Verifies: [DESIGN] D2 a static cursor scrolled past either end and back:
 * relative from before the first, absolute from the end, past the last
 * and before the first none; the last rowset of two; the row number; the
 * most rows cutting the result; SQLGetData before a fetch refused (24000).
 */
static void a_cursor_is_scrolled_past_its_ends(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_STATIC, 0);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1, 2, 3, 4, 5"), SQL_NTS) == SQL_SUCCESS);
    int32_t key = 0;
    char text[8];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_ERROR);
    TRL_CHECK(test, statement_reads(o.stmt, SQL_ATTR_ROW_NUMBER, 0U));
    (void)SQLBindCol(o.stmt, 1, SQL_C_SLONG, &key, 0, NULL);
    TRL_CHECK(test, fetched(o.stmt, SQL_FETCH_RELATIVE, 2, &key) == 2);
    TRL_CHECK(test, statement_reads(o.stmt, SQL_ATTR_ROW_NUMBER, 2U));
    TRL_CHECK(test, fetched(o.stmt, SQL_FETCH_ABSOLUTE, -2, &key) == 4);
    TRL_CHECK(test,
              fetched(o.stmt, SQL_FETCH_ABSOLUTE, 9, &key) == -1 && fetched(o.stmt, SQL_FETCH_PRIOR, 0, &key) == 5);
    TRL_CHECK(test, fetched(o.stmt, SQL_FETCH_RELATIVE, -9, &key) == -1);
    TRL_CHECK(test,
              fetched(o.stmt, SQL_FETCH_NEXT, 0, &key) == 1 && fetched(o.stmt, SQL_FETCH_ABSOLUTE, -9, &key) == -1);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)2, 0);
    int32_t keys[2] = {0, 0};
    (void)SQLBindCol(o.stmt, 1, SQL_C_SLONG, keys, 0, NULL);
    TRL_CHECK(test, fetched(o.stmt, SQL_FETCH_LAST, 0, keys) == 4 && keys[1] == 5);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)1, 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_MAX_ROWS, (SQLPOINTER)2, 0);
    SQLLEN rows = 0;
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1, 2, 3"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_NO_DATA && SQLRowCount(o.stmt, &rows) == SQL_SUCCESS);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/* The time, timestamp and numeric parameters of ODBC 2's and 3's C types. */
typedef struct older {
    signed char tiny;
    short small;
    long integer;
    TIME_STRUCT time;
    TIMESTAMP_STRUCT timestamp;
    SQL_NUMERIC_STRUCT numeric;
    unsigned char octets[2];
    SQLLEN octets_length;
    SQLLEN null_indicator;
    int32_t defaulted;
} older;

/* The parameters' values: each negative, a time, a timestamp of half a
   second, two octets, the null value and 9. */
static void older_made(older *p) {
    memset(p, 0, sizeof *p);
    p->tiny = -1;
    p->small = -2;
    p->integer = -3;
    p->time.hour = 1U;
    p->timestamp.year = 2020;
    p->timestamp.month = 1U;
    p->timestamp.day = 2U;
    p->timestamp.fraction = 500000000U;
    p->numeric.sign = 0U;
    p->numeric.val[0] = 7U;
    p->octets[0] = 0xABU;
    p->octets[1] = 0xCDU;
    p->null_indicator = SQL_NULL_DATA;
    p->defaulted = 9;
}

/* Binds the nine parameters of `p`, each of its C type. */
static void older_bound(SQLHSTMT stmt, older *p) {
    (void)SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_TINYINT, SQL_VARCHAR, 4, 0, &p->tiny, 0, NULL);
    (void)SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_VARCHAR, 4, 0, &p->small, 0, NULL);
    (void)SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_LONG, SQL_VARCHAR, 4, 0, &p->integer, 0, NULL);
    (void)SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_TIME, SQL_VARCHAR, 8, 0, &p->time, 0, NULL);
    (void)SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_TIMESTAMP, SQL_VARCHAR, 26, 0, &p->timestamp, 0, NULL);
    (void)SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_NUMERIC, SQL_VARCHAR, 4, 0, &p->numeric, 0, NULL);
    (void)SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY, 2, 0, p->octets, 2, NULL);
    (void)SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &p->defaulted, 0,
                           &p->null_indicator);
    (void)SQLBindParameter(stmt, 9, SQL_PARAM_INPUT, SQL_C_DEFAULT, SQL_INTEGER, 0, 0, &p->defaulted, 0, NULL);
}

/**
 * Verifies: [DESIGN] D2 parameters of ODBC 2's C types, SQL_C_TINYINT,
 * SQL_C_SHORT and SQL_C_LONG, a time and a timestamp, a negative numeric
 * of no scale, octets of their buffer's length, the null value by its
 * indicator, and SQL_C_DEFAULT taking the SQL type's C type.
 */
static void parameters_of_older_c_types(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    older p;
    older_made(&p);
    SQLRETURN prepared = SQLPrepare(
        o.stmt,
        sql_text("VALUES (CAST(? AS VARCHAR(40)), CAST(? AS VARCHAR(40)), CAST(? AS VARCHAR(40)), "
                 "CAST(? AS VARCHAR(40)), CAST(? AS VARCHAR(40)), CAST(? AS VARCHAR(40)), CAST(? AS VARBINARY(2)), "
                 "CAST(? AS INTEGER), CAST(? AS INTEGER))"),
        SQL_NTS);
    TRL_CHECK(test, prepared == SQL_SUCCESS);
    older_bound(o.stmt, &p);
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecute(o.stmt)) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS);
    static const char *const expected[] = {"-1", "-2", "-3", "01:00:00", "2020-01-02 00:00:00.5", "-7", "ABCD"};
    char text[ODBC_TEXT_BYTES];
    SQLLEN length = 0;
    bool every = true;
    for (SQLUSMALLINT i = 0U; i < 7U; ++i) {
        every =
            every &&
            SQLGetData(o.stmt, (SQLUSMALLINT)(i + 1U), SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS &&
            strstr(text, expected[i]) != NULL;
    }
    TRL_CHECK(test, every);
    TRL_CHECK(test, SQLGetData(o.stmt, 8, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS &&
                        length == SQL_NULL_DATA);
    TRL_CHECK(test, SQLGetData(o.stmt, 9, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS &&
                        strcmp(text, "9") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 values given at their edges: text into no buffer
 * telling its length, SQLWCHAR into a buffer of less than one unit cut
 * (01004); a binary string of no length pointer; a truth value as text;
 * a timestamp as a date, its time dropped (01S07); a text that is no UUID
 * refused (22018); a column bound with neither buffer nor indicator
 * passed over.
 */
static void values_given_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, SQLExecDirect(o.stmt,
                                  sql_text("VALUES ('abc', X'0102', TRUE, TIMESTAMP '2020-01-02 03:04:05', 'nope', 1)"),
                                  SQL_NTS) == SQL_SUCCESS);
    (void)SQLBindCol(o.stmt, 6, SQL_C_SLONG, NULL, 0, NULL);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS);
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, NULL, 0, &length) == SQL_SUCCESS_WITH_INFO && length == 3);
    SQLWCHAR wide[1];
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, wide, 1, &length) == SQL_SUCCESS_WITH_INFO);
    unsigned char octets[4];
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, octets, (SQLLEN)sizeof octets, NULL) == SQL_SUCCESS &&
                        octets[1] == 2U);
    char truth[4];
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_CHAR, truth, (SQLLEN)sizeof truth, &length) == SQL_SUCCESS);
    DATE_STRUCT date;
    TRL_CHECK(test,
              SQLGetData(o.stmt, 4, SQL_C_TYPE_DATE, &date, 0, &length) == SQL_SUCCESS_WITH_INFO && date.day == 2U);
    SQLGUID guid;
    TRL_CHECK(test, SQLGetData(o.stmt, 5, SQL_C_GUID, &guid, 0, &length) == SQL_ERROR);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    odbc_close(&o);
}

/* Gets column `column` as C type `type` into `buffer`; what SQLGetData said. */
static SQLRETURN got(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type, void *buffer) {
    SQLLEN length = 0;
    return SQLGetData(stmt, column, type, buffer, 0, &length);
}

/* Whether the statement's first diagnostic is `state`. */
static bool said(SQLHSTMT stmt, const char *state) {
    return strcmp(odbc_state(SQL_HANDLE_STMT, stmt, NULL), state) == 0;
}

/**
 * Verifies: [DESIGN] D2 numbers given at their edges: text that is an
 * exact number, an approximate one, or none (22018); a truth value as an
 * integer; a fraction dropped (01S07); a double past a float's range, an
 * integer's or a truth value's (22003); a negative fraction as an
 * unsigned integer, zero; a value of no number, a binary string, refused
 * (07006); a numeric of its scale, signed.
 */
static void numbers_given_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    static SQLCHAR sql[] = "VALUES ('12', '1.5e0', 'x', 1.5, TRUE, CAST(1e40 AS DOUBLE PRECISION), "
                           "CAST(1e40 AS DOUBLE PRECISION), -0.5, -0.5, 2.25, CAST(-1.5 AS DOUBLE PRECISION), X'01', "
                           "-5, 2.5)";
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecDirect(o.stmt, sql, SQL_NTS)) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS);
    SQLINTEGER integer = 0;
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_SLONG, &integer) == SQL_SUCCESS && integer == 12);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_SLONG, &integer) == SQL_SUCCESS_WITH_INFO && integer == 1);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_SLONG, &integer) == SQL_ERROR && said(o.stmt, "22018"));
    unsigned char bit = 9U;
    TRL_CHECK(test, got(o.stmt, 4, SQL_C_BIT, &bit) == SQL_SUCCESS_WITH_INFO && bit == 1U);
    TRL_CHECK(test, got(o.stmt, 5, SQL_C_SLONG, &integer) == SQL_SUCCESS && integer == 1);
    float single = 0.0F;
    TRL_CHECK(test, got(o.stmt, 6, SQL_C_FLOAT, &single) == SQL_ERROR && said(o.stmt, "22003"));
    TRL_CHECK(test, got(o.stmt, 7, SQL_C_SLONG, &integer) == SQL_ERROR && said(o.stmt, "22003"));
    SQLUINTEGER unsigned_integer = 9U;
    TRL_CHECK(test, got(o.stmt, 8, SQL_C_ULONG, &unsigned_integer) == SQL_SUCCESS_WITH_INFO && unsigned_integer == 0U);
    TRL_CHECK(test, got(o.stmt, 9, SQL_C_BIT, &bit) == SQL_ERROR && said(o.stmt, "22003"));
    SQL_NUMERIC_STRUCT numeric;
    TRL_CHECK(test, got(o.stmt, 10, SQL_C_NUMERIC, &numeric) == SQL_SUCCESS_WITH_INFO && numeric.val[0] == 2U &&
                        numeric.sign == 1U);
    TRL_CHECK(test,
              got(o.stmt, 11, SQL_C_NUMERIC, &numeric) == SQL_SUCCESS && numeric.val[0] == 2U && numeric.sign == 0U);
    TRL_CHECK(test, got(o.stmt, 12, SQL_C_SLONG, &integer) == SQL_ERROR && said(o.stmt, "07006"));
    unsigned char tiny = 0U;
    TRL_CHECK(test, got(o.stmt, 13, SQL_C_UTINYINT, &tiny) == SQL_ERROR && said(o.stmt, "22003"));
    TRL_CHECK(test, got(o.stmt, 14, SQL_C_BIT, &bit) == SQL_ERROR && said(o.stmt, "22003"));
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/* Whether text field `field` of diagnostic record `number` reads as `expected`. */
static bool diag_reads(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, SQLSMALLINT field,
                       const char *expected) {
    SQLCHAR text[ODBC_TEXT_BYTES] = "";
    SQLSMALLINT length = 0;
    return SQLGetDiagField(type, handle, number, field, text, (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS &&
           strcmp((const char *)text, expected) == 0;
}

/* What SQLLEN field `field` of record `number` holds, or -99 if none is read. */
static SQLLEN diag_number(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, SQLSMALLINT field) {
    SQLLEN told = 0;
    return SQLGetDiagField(type, handle, number, field, &told, 0, NULL) == SQL_SUCCESS ? told : -99;
}

/* What SQLINTEGER field `field` of record `number` holds, or -99 if none is read. */
static SQLINTEGER diag_integer(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, SQLSMALLINT field) {
    SQLINTEGER told = 0;
    return SQLGetDiagField(type, handle, number, field, &told, 0, NULL) == SQL_SUCCESS ? told : -99;
}

/**
 * Verifies: [DESIGN] D2 each diagnostic field: a class and subclass of
 * SQL's from ISO 9075, of ODBC's or a subclass ending in S from ODBC 3.0;
 * the server's name of each kind of handle, a descriptor's among them;
 * the cursor's row count; a statement's function unknown; a record's
 * column and row unknown; a row count of a connection, a record of
 * number 0 or a field of none refused; a record past the last, no data.
 */
static void diagnostics_read_each_field(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, SQLSetConnectAttr(o.dbc, SQL_ATTR_TXN_ISOLATION, (SQLPOINTER)99, 0) == SQL_ERROR);
    TRL_CHECK(test, diag_reads(SQL_HANDLE_DBC, o.dbc, 1, SQL_DIAG_CLASS_ORIGIN, "ODBC 3.0") &&
                        diag_reads(SQL_HANDLE_DBC, o.dbc, 1, SQL_DIAG_SUBCLASS_ORIGIN, "ODBC 3.0") &&
                        diag_reads(SQL_HANDLE_DBC, o.dbc, 1, SQL_DIAG_SERVER_NAME, "") &&
                        diag_reads(SQL_HANDLE_DBC, o.dbc, 1, SQL_DIAG_CONNECTION_NAME, ""));
    TRL_CHECK(test, diag_number(SQL_HANDLE_DBC, o.dbc, 0, SQL_DIAG_ROW_COUNT) == -99 &&
                        diag_integer(SQL_HANDLE_DBC, o.dbc, 0, SQL_DIAG_DYNAMIC_FUNCTION_CODE) == -99 &&
                        diag_number(SQL_HANDLE_DBC, o.dbc, 0, SQL_DIAG_SQLSTATE) == -99);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT FROM"), SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, diag_reads(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_CLASS_ORIGIN, "ISO 9075") &&
                        diag_reads(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_SUBCLASS_ORIGIN, "ISO 9075") &&
                        diag_reads(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_SERVER_NAME, ""));
    TRL_CHECK(test, diag_integer(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_COLUMN_NUMBER) == SQL_COLUMN_NUMBER_UNKNOWN &&
                        diag_number(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_ROW_NUMBER) == SQL_ROW_NUMBER_UNKNOWN &&
                        diag_integer(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_DYNAMIC_FUNCTION_CODE) ==
                            SQL_DIAG_UNKNOWN_STATEMENT &&
                        diag_number(SQL_HANDLE_STMT, o.stmt, 1, (SQLSMALLINT)9999) == -99);
    SQLSMALLINT none = 0;
    SQLCHAR text[8];
    TRL_CHECK(test, SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_SQLSTATE, text, 8, &none) == SQL_ERROR &&
                        SQLGetDiagField(SQL_HANDLE_STMT, o.stmt, 9, SQL_DIAG_SQLSTATE, text, 8, &none) == SQL_NO_DATA);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1.5, 2, 3"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, diag_number(SQL_HANDLE_STMT, o.stmt, 0, SQL_DIAG_CURSOR_ROW_COUNT) == 3);
    SQLINTEGER integer = 0;
    SQLLEN length = 0;
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS &&
                        SQLGetData(o.stmt, 1, SQL_C_SLONG, &integer, 0, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, diag_reads(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_CLASS_ORIGIN, "ISO 9075") &&
                        diag_reads(SQL_HANDLE_STMT, o.stmt, 1, SQL_DIAG_SUBCLASS_ORIGIN, "ODBC 3.0"));
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    SQLHDESC ard = SQL_NULL_HANDLE;
    (void)SQLGetStmtAttr(o.stmt, SQL_ATTR_APP_ROW_DESC, (SQLPOINTER)&ard, 0, NULL);
    TRL_CHECK(test, SQLSetDescField(ard, 1, 9999, (SQLPOINTER)1, 0) == SQL_ERROR &&
                        diag_reads(SQL_HANDLE_DESC, ard, 1, SQL_DIAG_SERVER_NAME, ""));
    TRL_CHECK(test, SQLSetEnvAttr(o.env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)99, 0) == SQL_ERROR &&
                        diag_reads(SQL_HANDLE_ENV, o.env, 1, SQL_DIAG_SERVER_NAME, ""));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 statements at their edges: a searched DELETE,
 * written in lower case after spaces, that deletes nothing SQL_NO_DATA,
 * another statement counting no rows SQL_SUCCESS; the parameters counted
 * into no count; a parameter of number 0 or past the last refused (07009);
 * SQLNativeSql of a malformed escape refused (42000), of no text (HY009);
 * a cursor's name of nothing or past 128 characters refused (34000), of
 * no text (HY009), one set read back, one not set made by the driver.
 */
static void statements_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE D (K INTEGER)") && odbc_run(o.stmt, "INSERT INTO D VALUES (1)"));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("  \n delete from D WHERE K = 9"), SQL_NTS) == SQL_NO_DATA);
    SQLLEN rows = 0;
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SET SCHEMA 'ANN'"), SQL_NTS) == SQL_SUCCESS &&
                        SQLRowCount(o.stmt, &rows) == SQL_SUCCESS && rows == 0);
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("SELECT K FROM D WHERE K = ?"), SQL_NTS) == SQL_SUCCESS);
    SQLSMALLINT type = 0;
    TRL_CHECK(test, SQLNumParams(o.stmt, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 0, &type, NULL, NULL, NULL) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "07009") == 0);
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 2, &type, NULL, NULL, NULL) == SQL_ERROR);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLINTEGER length = 0;
    TRL_CHECK(test, SQLNativeSql(o.dbc, sql_text("VALUES {nope 1}"), SQL_NTS, text, (SQLINTEGER)sizeof text, &length) ==
                            SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "42000") == 0);
    TRL_CHECK(test, SQLNativeSql(o.dbc, NULL, SQL_NTS, text, (SQLINTEGER)sizeof text, &length) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_DBC, o.dbc, NULL), "HY009") == 0);
    char long_name[130];
    memset(long_name, 'C', sizeof long_name - 1U);
    long_name[sizeof long_name - 1U] = '\0';
    TRL_CHECK(test, SQLSetCursorName(o.stmt, sql_text(""), SQL_NTS) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "34000") == 0);
    TRL_CHECK(test, SQLSetCursorName(o.stmt, (SQLCHAR *)long_name, SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, SQLSetCursorName(o.stmt, NULL, SQL_NTS) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HY009") == 0);
    SQLSMALLINT name_length = 0;
    TRL_CHECK(test, SQLGetCursorName(o.stmt, text, (SQLSMALLINT)sizeof text, &name_length) == SQL_SUCCESS &&
                        strncmp((const char *)text, "SQL_CUR", 7U) == 0);
    TRL_CHECK(test, SQLSetCursorName(o.stmt, sql_text("C1"), SQL_NTS) == SQL_SUCCESS &&
                        SQLGetCursorName(o.stmt, text, (SQLSMALLINT)sizeof text, &name_length) == SQL_SUCCESS &&
                        strcmp((const char *)text, "C1") == 0);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE D CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 columns bound at their edges: a column bound as
 * number 0 refused (07009); columns unbound, last first and one never
 * bound; a column bound by its length alone told its length; a row of a
 * column in error and one cut, its status an error.
 */
static void columns_bound_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLINTEGER key = 0;
    SQLLEN length = 0;
    TRL_CHECK(test, SQLBindCol(o.stmt, 0, SQL_C_SLONG, &key, 0, NULL) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "07009") == 0);
    (void)SQLBindCol(o.stmt, 1, SQL_C_SLONG, &key, 0, NULL);
    (void)SQLBindCol(o.stmt, 2, SQL_C_SLONG, &key, 0, NULL);
    TRL_CHECK(test, SQLBindCol(o.stmt, 2, SQL_C_SLONG, NULL, 0, NULL) == SQL_SUCCESS &&
                        SQLBindCol(o.stmt, 1, SQL_C_SLONG, NULL, 0, NULL) == SQL_SUCCESS &&
                        SQLBindCol(o.stmt, 5, SQL_C_SLONG, NULL, 0, NULL) == SQL_SUCCESS);
    SQLLEN told = 0;
    (void)SQLBindCol(o.stmt, 1, SQL_C_CHAR, NULL, 0, &told);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES ('abc', 'x', 'abc')"), SQL_NTS) == SQL_SUCCESS);
    SQLINTEGER wrong = 0;
    char cut[2];
    (void)SQLBindCol(o.stmt, 2, SQL_C_SLONG, &wrong, 0, NULL);
    (void)SQLBindCol(o.stmt, 3, SQL_C_CHAR, cut, (SQLLEN)sizeof cut, &length);
    SQLUSMALLINT status = 0U;
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_STATUS_PTR, &status, 0);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_ERROR && status == SQL_ROW_ERROR && told == 3);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_STATUS_PTR, NULL, 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 rowsets at their edges: a rowset of two back
 * from the second row the first two; the last rowset of a result smaller
 * than it; the most rows past the rows there; SQLGetData of SQL_ARD_TYPE,
 * bound or not, and of a column of number 0 or past the last refused
 * (07009).
 */
static void rowsets_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLINTEGER key = 0;
    SQLLEN length = 0;
    char cut[2];
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_STATIC, 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)2, 0);
    SQLINTEGER keys[2] = {0, 0};
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1, 2, 3, 4, 5"), SQL_NTS) == SQL_SUCCESS);
    (void)SQLBindCol(o.stmt, 1, SQL_C_SLONG, keys, 0, NULL);
    TRL_CHECK(test, SQL_SUCCEEDED(SQLFetchScroll(o.stmt, SQL_FETCH_ABSOLUTE, 2)) && keys[0] == 2);
    TRL_CHECK(test, SQL_SUCCEEDED(SQLFetchScroll(o.stmt, SQL_FETCH_PRIOR, 0)) && keys[0] == 1 && keys[1] == 2);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 7"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQL_SUCCEEDED(SQLFetchScroll(o.stmt, SQL_FETCH_LAST, 0)) && keys[0] == 7);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)1, 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_MAX_ROWS, (SQLPOINTER)10, 0);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES (1, 2)"), SQL_NTS) == SQL_SUCCESS);
    (void)SQLBindCol(o.stmt, 2, SQL_C_CHAR, cut, (SQLLEN)sizeof cut, &length);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_ARD_TYPE, &key, 0, &length) == SQL_SUCCESS && key == 1);
    char text[8];
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_ARD_TYPE, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS &&
                        strcmp(text, "2") == 0);
    TRL_CHECK(test, SQLGetData(o.stmt, 0, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_ERROR &&
                        SQLGetData(o.stmt, 3, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "07009") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    odbc_close(&o);
}

/* Parameters at their edges, each of the C type it is bound as. */
typedef struct edged {
    char text[4];
    SQLLEN text_length;
    unsigned char octets[3];
    SQLLEN octets_length;
    SQLWCHAR wide[4];
    SQLLEN wide_length;
    SQL_NUMERIC_STRUCT scaled;
    SQL_NUMERIC_STRUCT zero;
    DATE_STRUCT date;
    float single;
    unsigned char bit;
} edged;

/* Binds `e`'s nine parameters: text cut by its length, octets of their
   buffer's length, SQLWCHAR cut by its octets, numerics of a scale and
   of a negative zero, a date, a float and a truth value. */
static void edged_bound(SQLHSTMT stmt, edged *e) {
    memset(e, 0, sizeof *e);
    memcpy(e->text, "abc", 4U);
    e->text_length = 2;
    memcpy(e->octets, "\x01\x02\x03", 3U);
    e->octets_length = SQL_NTS;
    e->wide[0] = 'x';
    e->wide[1] = 'y';
    e->wide[2] = 'z';
    e->wide_length = (SQLLEN)(2U * sizeof(SQLWCHAR));
    e->scaled.sign = 1U;
    e->scaled.scale = 2;
    e->scaled.val[0] = 0xD2U;
    e->scaled.val[1] = 0x04U;
    e->date.year = 2020;
    e->date.month = 3U;
    e->date.day = 4U;
    e->single = 1.5F;
    e->bit = 1U;
    (void)SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 4, 0, e->text, 4, &e->text_length);
    (void)SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY, 3, 0, e->octets, 3,
                           &e->octets_length);
    (void)SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR, 4, 0, e->wide, 0, &e->wide_length);
    (void)SQLBindParameter(stmt, 4, SQL_PARAM_INPUT, SQL_C_NUMERIC, SQL_NUMERIC, 6, 2, &e->scaled, 0, NULL);
    (void)SQLBindParameter(stmt, 5, SQL_PARAM_INPUT, SQL_C_NUMERIC, SQL_NUMERIC, 6, 0, &e->zero, 0, NULL);
    (void)SQLBindParameter(stmt, 6, SQL_PARAM_INPUT, SQL_C_TYPE_DATE, SQL_TYPE_DATE, 10, 0, &e->date, 0, NULL);
    (void)SQLBindParameter(stmt, 7, SQL_PARAM_INPUT, SQL_C_FLOAT, SQL_REAL, 0, 0, &e->single, 0, NULL);
    (void)SQLBindParameter(stmt, 8, SQL_PARAM_INPUT, SQL_C_BIT, SQL_BIT, 0, 0, &e->bit, 0, NULL);
}

/**
 * Verifies: [DESIGN] D2 parameters at their edges: text cut by its
 * length, octets of no length their buffer's, SQLWCHAR cut by its
 * length in octets, a numeric of a scale and a negative zero, a date, a
 * float and a truth value, each read back as sent; a parameter of data
 * at execution refused (HYC00), one of no data and no indicator (HY009).
 */
static void parameters_given_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    static SQLCHAR sql[] = "VALUES (CAST(? AS VARCHAR(4)), CAST(? AS VARBINARY(3)), CAST(? AS VARCHAR(4)), "
                           "CAST(? AS NUMERIC(6, 2)), CAST(? AS NUMERIC(6)), CAST(? AS DATE), CAST(? AS REAL), "
                           "CAST(? AS BOOLEAN))";
    TRL_CHECK(test, odbc_said(o.stmt, SQLPrepare(o.stmt, sql, SQL_NTS)) == SQL_SUCCESS);
    edged e;
    edged_bound(o.stmt, &e);
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecute(o.stmt)) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS);
    static const char *const expected[] = {"ab", "010203", "xy", "12.34", "0", "2020-03-04", "1.5E0", "1"};
    char text[ODBC_TEXT_BYTES];
    SQLLEN length = 0;
    bool every = true;
    for (SQLUSMALLINT i = 0U; i < 8U; ++i) {
        bool read =
            SQLGetData(o.stmt, (SQLUSMALLINT)(i + 1U), SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS;
        every = every && read && strcmp(text, expected[i]) == 0;
        if (!read || strcmp(text, expected[i]) != 0) {
            (void)fprintf(stderr, "parameter %u read as %s\n", i + 1U, text);
        }
    }
    TRL_CHECK(test, every);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    e.text_length = SQL_DATA_AT_EXEC;
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HYC00") == 0);
    e.text_length = SQL_LEN_DATA_AT_EXEC(4);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR);
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 4, 0, NULL, 0, NULL);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HY009") == 0);
    odbc_close(&o);
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc-edges", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(attributes_read_back_as_set),  TRL_TEST_CASE(a_cursor_is_scrolled_past_its_ends),
        TRL_TEST_CASE(parameters_of_older_c_types),  TRL_TEST_CASE(values_given_at_their_edges),
        TRL_TEST_CASE(numbers_given_at_their_edges), TRL_TEST_CASE(diagnostics_read_each_field),
        TRL_TEST_CASE(statements_at_their_edges),    TRL_TEST_CASE(rowsets_at_their_edges),
        TRL_TEST_CASE(columns_bound_at_their_edges), TRL_TEST_CASE(parameters_given_at_their_edges),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
