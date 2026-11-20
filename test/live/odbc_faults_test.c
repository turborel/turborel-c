/**
 * @file odbc_faults_test.c
 * @brief Memory not had in the ODBC driver, against a real server
 * (RULES.md R-MEM-2, R-TEST-8): a session of the driver's, connecting,
 * running statements with parameters, fetching, describing and reading
 * the catalog, each allocation failing in turn, never crashing or
 * leaking. Built with TRL_FAULTS alone.
 */
#include "faults.h"
#include "harness.h"
#include "odbc_live.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKIPPED 77

/* Whether an ODBC function's outcome is one memory not had allows: any
   but SQL_INVALID_HANDLE, which would say a handle was lost. */
static bool fine(SQLRETURN returned) {
    return returned != SQL_INVALID_HANDLE;
}

/* A statement with a parameter run, its row fetched and described. */
static bool statement_session(SQLHSTMT stmt) {
    int32_t key = 7;
    char text[ODBC_TEXT_BYTES];
    SQLLEN length = 0;
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    (void)SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &key, 0, NULL);
    bool ok =
        fine(SQLExecDirect(stmt, sql_text("SELECT K, V FROM F WHERE K < CAST(? AS INTEGER) ORDER BY K"), SQL_NTS));
    (void)SQLBindCol(stmt, 1, SQL_C_SLONG, &key, 0, &length);
    ok = ok && fine(SQLFetch(stmt));
    ok = ok && fine(SQLGetData(stmt, 2, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length));
    ok = ok && fine(SQLGetData(stmt, 2, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length));
    SQLSMALLINT type = 0;
    ok = ok && fine(SQLDescribeCol(stmt, 2, (SQLCHAR *)text, (SQLSMALLINT)sizeof text, NULL, &type, NULL, NULL, NULL));
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    (void)SQLFreeStmt(stmt, SQL_UNBIND);
    (void)SQLFreeStmt(stmt, SQL_RESET_PARAMS);
    ok = ok && fine(SQLPrepare(stmt, sql_text("VALUES {fn UCASE('a')}"), SQL_NTS)) && fine(SQLExecute(stmt));
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    ok = ok && fine(SQLColumns(stmt, NULL, 0, sql_text("ANN"), SQL_NTS, sql_text("F"), SQL_NTS, NULL, 0));
    ok = ok && fine(SQLFetch(stmt));
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    return ok;
}

/* A session of the driver's: connected, a statement's work, its diagnostics read, disconnected. */
static bool driver_session(void) {
    SQLHENV env = SQL_NULL_HANDLE;
    SQLHDBC dbc = SQL_NULL_HANDLE;
    SQLHSTMT stmt = SQL_NULL_HANDLE;
    bool ok = true;
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env) != SQL_SUCCESS) {
        return true;
    }
    (void)SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_SUCCESS) {
        SQLRETURN connected =
            SQLDriverConnect(dbc, NULL, sql_text(odbc_connection_string), SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        SQLCHAR state[6];
        SQLCHAR message[ODBC_TEXT_BYTES];
        ok = fine(connected) && fine(SQLGetDiagRec(SQL_HANDLE_DBC, dbc, 1, state, NULL, message, 8, NULL));
        if (SQL_SUCCEEDED(connected) && SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) == SQL_SUCCESS) {
            ok = ok && statement_session(stmt);
            (void)SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
        (void)SQLDisconnect(dbc);
        (void)SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    }
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
    return ok;
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of the ODBC driver's session,
 * connecting, a statement with a parameter run, fetched and described, a
 * statement's escapes written, a catalog function, failing in turn fails
 * the function, and leaks nothing.
 */
static void the_driver_survives_each_fault(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o) && odbc_run(o.stmt, "CREATE TABLE F (K INTEGER, V VARCHAR(10))") &&
                        odbc_run(o.stmt, "INSERT INTO F VALUES (1, 'one'), (2, 'two')"));
    odbc_close(&o);
    TRL_CHECK(test, every_fault(driver_session));
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc-faults", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_driver_survives_each_fault),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
