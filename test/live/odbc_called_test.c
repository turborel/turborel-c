/**
 * @file odbc_called_test.c
 * @brief A CALL's parameters through the ODBC driver against a real
 * server (RULES.md R-TEST-8): every `?` numbered, OUT ones too, described,
 * its mode told by the IPD SQLPrepare fills, and an OUT or INOUT one's
 * value given back to the buffer bound [PROTOCOL Parameter modes].
 */
#include "harness.h"
#include "odbc_live.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKIPPED 77

/* Whether IPD record `number` of `stmt` is of SQL_DESC_PARAMETER_TYPE `io`. */
static bool ipd_says(SQLHSTMT stmt, SQLSMALLINT number, SQLSMALLINT io) {
    SQLHDESC ipd = SQL_NULL_HANDLE;
    SQLSMALLINT told = 0;
    return SQLGetStmtAttr(stmt, SQL_ATTR_IMP_PARAM_DESC, (SQLPOINTER)&ipd, 0, NULL) == SQL_SUCCESS &&
           SQLGetDescField(ipd, number, SQL_DESC_PARAMETER_TYPE, &told, 0, NULL) == SQL_SUCCESS && told == io;
}

/* A CALL's three values: IN, OUT and INOUT, with the OUT one's indicator. */
typedef struct moved {
    SQLINTEGER a;
    SQLINTEGER b;
    SQLINTEGER c;
    SQLLEN b_length;
} moved;

/* Binds `m`: the first `?` an input, the second an output, the third both. */
static void moved_bound(SQLHSTMT stmt, moved *m) {
    (void)SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &m->a, 0, NULL);
    (void)SQLBindParameter(stmt, 2, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &m->b, 0, &m->b_length);
    (void)SQLBindParameter(stmt, 3, SQL_PARAM_INPUT_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &m->c, 0, NULL);
}

/**
 * Verifies: [DESIGN] D2 a CALL of IN, OUT and INOUT parameters prepared:
 * SQLNumParams counting every `?`, OUT ones too, SQLDescribeParam
 * describing each by its type's tag, as a parameter has no declared type, the IPD SQLPrepare fills telling each one's
 * SQL_DESC_PARAMETER_TYPE, SQL_ATTR_AUTO_IPD and SQL_ATTR_ENABLE_AUTO_IPD
 * on; executed, the OUT and INOUT values given back to their buffers and
 * no result set left; through SQLExecDirect of the CALL's escape too.
 */
static void a_calls_outputs_are_given_back(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE PROCEDURE MOVED (IN A INTEGER, OUT B INTEGER, INOUT C INTEGER) "
                                     "MODIFIES SQL DATA BEGIN SET B = A * 2; SET C = C + A; END"));
    SQLUINTEGER automatic = 0U;
    TRL_CHECK(test,
              SQLGetConnectAttr(o.dbc, SQL_ATTR_AUTO_IPD, &automatic, 0, NULL) == SQL_SUCCESS && automatic == SQL_TRUE);
    SQLULEN enabled = 0U;
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ATTR_ENABLE_AUTO_IPD, &enabled, 0, NULL) == SQL_SUCCESS &&
                        enabled == SQL_TRUE);
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("CALL MOVED(?, ?, ?)"), SQL_NTS) == SQL_SUCCESS);
    SQLSMALLINT count = 0;
    SQLSMALLINT type = 0;
    SQLULEN size = 0U;
    TRL_CHECK(test, SQLNumParams(o.stmt, &count) == SQL_SUCCESS && count == 3);
    TRL_CHECK(test, SQLDescribeParam(o.stmt, 2, &type, &size, NULL, NULL) == SQL_SUCCESS && type == SQL_NUMERIC &&
                        size == 10U);
    TRL_CHECK(test, ipd_says(o.stmt, 1, SQL_PARAM_INPUT) && ipd_says(o.stmt, 2, SQL_PARAM_OUTPUT) &&
                        ipd_says(o.stmt, 3, SQL_PARAM_INPUT_OUTPUT));
    moved m = {5, 0, 7, 0};
    moved_bound(o.stmt, &m);
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecute(o.stmt)) == SQL_SUCCESS);
    TRL_CHECK(test, m.b == 10 && m.b_length == (SQLLEN)sizeof m.b && m.c == 12);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "24000") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    m.a = 1;
    m.c = 1;
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("{call MOVED(?, ?, ?)}"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, m.b == 2 && m.c == 2);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 an OUT value given into a buffer too short for
 * its text, cut (01004); an OUT `?` bound only as an input, its value left
 * in the CALL's row to fetch; a statement other than a CALL, its `?`s
 * inputs; SQL_ATTR_ENABLE_AUTO_IPD set off and read back.
 */
static void outputs_at_their_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE PROCEDURE TENFOLD (IN A INTEGER, OUT B INTEGER) "
                                     "MODIFIES SQL DATA SET B = A * 10"));
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("CALL TENFOLD(?, ?)"), SQL_NTS) == SQL_SUCCESS);
    SQLINTEGER a = 12;
    char text[2] = "";
    SQLLEN length = 0;
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &a, 0, NULL);
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_OUTPUT, SQL_C_CHAR, SQL_VARCHAR, 4, 0, text, (SQLLEN)sizeof text,
                           &length);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_SUCCESS_WITH_INFO &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "01004") == 0);
    TRL_CHECK(test, strcmp(text, "1") == 0 && length == 3);
    SQLINTEGER ignored = 0;
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &ignored, 0, NULL);
    SQLINTEGER b = 0;
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS &&
                        SQLGetData(o.stmt, 1, SQL_C_SLONG, &b, 0, NULL) == SQL_SUCCESS && b == 120);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("VALUES CAST(? AS INTEGER)"), SQL_NTS) == SQL_SUCCESS);
    SQLSMALLINT count = 0;
    TRL_CHECK(test, SQLNumParams(o.stmt, &count) == SQL_SUCCESS && count == 1 && ipd_says(o.stmt, 1, SQL_PARAM_INPUT));
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_ENABLE_AUTO_IPD, (SQLPOINTER)SQL_FALSE, 0) == SQL_SUCCESS);
    SQLULEN enabled = 9U;
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ATTR_ENABLE_AUTO_IPD, &enabled, 0, NULL) == SQL_SUCCESS &&
                        enabled == SQL_FALSE);
    odbc_close(&o);
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc-called", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_calls_outputs_are_given_back),
        TRL_TEST_CASE(outputs_at_their_edges),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
