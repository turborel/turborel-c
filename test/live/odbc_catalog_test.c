/**
 * @file odbc_catalog_test.c
 * @brief The ODBC driver's catalog functions, connecting and scrolling
 * against a real server (RULES.md R-TEST-8): each catalog function by its
 * ANSI and its Unicode entry point, names as identifiers; a connection by
 * a data source's name, a URI and SQLWCHAR; rowsets bound row-wise and
 * scrolled each way.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "format.h"
#include "harness.h"
#include "odbc_live.h"
#include "server.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U

/* The rows a catalog function gave, its cursor closed after. */
static long rows_of(SQLHSTMT stmt, SQLRETURN returned) {
    long rows = SQL_SUCCEEDED(odbc_said(stmt, returned)) ? 0L : -1L;
    while (rows >= 0L && SQL_SUCCEEDED(SQLFetch(stmt))) {
        ++rows;
    }
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    return rows;
}

/* `text` as SQLWCHAR, in one of a few buffers used in turn. */
static SQLWCHAR *wide_text(const char *text) {
    static SQLWCHAR buffers[4][ODBC_TEXT_BYTES];
    static size_t next;
    SQLWCHAR *buffer = buffers[next++ % 4U];
    size_t i = 0U;
    for (; text[i] != '\0' && i + 1U < ODBC_TEXT_BYTES; ++i) {
        buffer[i] = (SQLWCHAR)(unsigned char)text[i];
    }
    buffer[i] = 0;
    return buffer;
}

/**
 * Verifies: [DESIGN] D2 each catalog function by its Unicode entry point,
 * and with SQL_ATTR_METADATA_ID its names identifiers, a quoted one as it
 * is: tables, columns, keys, statistics, special columns, procedures,
 * their columns and privileges; a table's types listed, TABLE among them.
 */
static void catalog_functions_take_sqlwchar_and_identifiers(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE P (K INTEGER PRIMARY KEY)"));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE \"f\" (K INTEGER, P INTEGER REFERENCES P (K))"));
    TRL_CHECK(test, rows_of(o.stmt, SQLTablesW(o.stmt, NULL, 0, wide_text("ANN"), SQL_NTS, NULL, 0,
                                               wide_text("TABLE, 'VIEW'"), SQL_NTS)) == 2L);
    TRL_CHECK(test, rows_of(o.stmt, SQLColumnsW(o.stmt, NULL, 0, wide_text("ANN"), SQL_NTS, wide_text("P"), SQL_NTS,
                                                NULL, 0)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLPrimaryKeysW(o.stmt, NULL, 0, NULL, 0, wide_text("P"), SQL_NTS)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLForeignKeysW(o.stmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, wide_text("f"),
                                                    SQL_NTS)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLStatisticsW(o.stmt, NULL, 0, NULL, 0, wide_text("P"), SQL_NTS, SQL_INDEX_ALL,
                                                   SQL_QUICK)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLSpecialColumnsW(o.stmt, SQL_ROWVER, NULL, 0, NULL, 0, wide_text("P"), SQL_NTS,
                                                       SQL_SCOPE_SESSION, SQL_NULLABLE)) == 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLSpecialColumnsW(o.stmt, SQL_BEST_ROWID, NULL, 0, NULL, 0, wide_text("P"),
                                                       SQL_NTS, SQL_SCOPE_SESSION, SQL_NULLABLE)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLProceduresW(o.stmt, NULL, 0, wide_text("ANN"), SQL_NTS, NULL, 0)) == 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLProcedureColumnsW(o.stmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)) == 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLTablePrivilegesW(o.stmt, NULL, 0, NULL, 0, wide_text("P"), SQL_NTS)) > 0L);
    TRL_CHECK(test,
              rows_of(o.stmt, SQLColumnPrivilegesW(o.stmt, NULL, 0, NULL, 0, wide_text("P"), SQL_NTS, NULL, 0)) > 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLGetTypeInfoW(o.stmt, SQL_TYPE_DATE)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLGetTypeInfo(o.stmt, SQL_DATE)) == 1L);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0) == SQL_SUCCESS);
    TRL_CHECK(test, rows_of(o.stmt, SQLTables(o.stmt, NULL, 0, sql_text("ann"), SQL_NTS, sql_text("\"f\""), SQL_NTS,
                                              NULL, 0)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLTables(o.stmt, NULL, 0, sql_text("ann"), SQL_NTS, sql_text("p"), SQL_NTS, NULL,
                                              0)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLProcedures(o.stmt, NULL, 0, NULL, 0, sql_text("NONE"), SQL_NTS)) == 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLProcedureColumns(o.stmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)) == 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLTablePrivileges(o.stmt, NULL, 0, NULL, 0, sql_text("P"), SQL_NTS)) > 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLColumnPrivileges(o.stmt, NULL, 0, NULL, 0, sql_text("P"), SQL_NTS, sql_text("K"),
                                                        SQL_NTS)) > 0L);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_FALSE, 0);
    TRL_CHECK(test, rows_of(o.stmt, SQLTables(o.stmt, sql_text(SQL_ALL_CATALOGS), SQL_NTS, sql_text(""), 0,
                                              sql_text(""), 0, NULL, 0)) == 1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLTables(o.stmt, sql_text(""), 0, sql_text(SQL_ALL_SCHEMAS), SQL_NTS, sql_text(""),
                                              0, NULL, 0)) > 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLTables(o.stmt, NULL, 0, NULL, 0, NULL, 0, sql_text(""), 0)) > 0L);
    TRL_CHECK(test, rows_of(o.stmt, SQLStatistics(o.stmt, NULL, 0, NULL, 0, NULL, 0, SQL_INDEX_ALL, SQL_QUICK)) == -1L);
    TRL_CHECK(test,
              rows_of(o.stmt, SQLForeignKeys(o.stmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0)) == -1L);
    TRL_CHECK(test, rows_of(o.stmt, SQLSpecialColumns(o.stmt, SQL_BEST_ROWID, NULL, 0, NULL, 0, NULL, 0,
                                                      SQL_SCOPE_SESSION, SQL_NULLABLE)) == -1L);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE \"f\" CASCADE") && odbc_run(o.stmt, "DROP TABLE P CASCADE"));
    odbc_close(&o);
}

/* Connects `dbc` by `string`; what SQLDriverConnect returned. */
static SQLRETURN connected_by(SQLHDBC dbc, const char *string) {
    SQLCHAR out[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    return SQLDriverConnect(dbc, NULL, sql_text(string), SQL_NTS, out, 4, &length, SQL_DRIVER_NOPROMPT);
}

/**
 * Verifies: [DESIGN] D2 a connection by a data source's name, its
 * attributes from odbc.ini, by SQLConnect and SQLConnectW; by a
 * connection string of a URI, of a value in braces, of keys of other
 * drivers, its copy given back cut short (01004); by SQLWCHAR; a second
 * connection of one connected refused (08002); a URI that cannot be read
 * refused (08001).
 */
static void connections_are_made_each_way(trl_test *test) {
    SQLHENV env = SQL_NULL_HANDLE;
    SQLHDBC dbc = SQL_NULL_HANDLE;
    (void)SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    (void)SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    (void)SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    TRL_CHECK(test, SQLSetConnectAttr(dbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)5, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLConnect(dbc, sql_text("reports"), SQL_NTS, sql_text("bob"), SQL_NTS, NULL, 0) == SQL_SUCCESS);
    TRL_CHECK(test, connected_by(dbc, "DSN=reports") == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_DBC, dbc, NULL), "08002") == 0);
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    TRL_CHECK(test, SQLGetInfo(dbc, SQL_DATA_SOURCE_NAME, text, (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)text, "reports") == 0);
    TRL_CHECK(test, SQLGetInfo(dbc, SQL_USER_NAME, text, (SQLSMALLINT)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)text, "BOB") == 0);
    TRL_CHECK(test, SQLDisconnect(dbc) == SQL_SUCCESS);
    TRL_CHECK(test, SQLConnectW(dbc, wide_text("reports"), SQL_NTS, NULL, 0, NULL, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLDisconnect(dbc) == SQL_SUCCESS);
    char string[ODBC_TEXT_BYTES];
    trl_format(string, sizeof string, "URI=turborel://ann@127.0.0.1:%u/?tls_mode=disable;DATABASE=x;Port={%u}",
               (unsigned int)SERVER_PORT, (unsigned int)SERVER_PORT);
    TRL_CHECK(test, connected_by(dbc, string) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLDisconnect(dbc) == SQL_SUCCESS);
    trl_format(string, sizeof string, "SERVER=127.0.0.1;PORT=%u;UID=ann;TLS_MODE=disable", (unsigned int)SERVER_PORT);
    SQLWCHAR out[ODBC_TEXT_BYTES];
    SQLSMALLINT out_length = 0;
    TRL_CHECK(test, SQLDriverConnectW(dbc, NULL, wide_text(string), SQL_NTS, out, ODBC_TEXT_BYTES, &out_length,
                                      SQL_DRIVER_NOPROMPT) == SQL_SUCCESS);
    TRL_CHECK(test, SQLDisconnect(dbc) == SQL_SUCCESS);
    TRL_CHECK(test, connected_by(dbc, "URI=postgres://x") == SQL_ERROR);
    TRL_CHECK(test, SQL_SUCCEEDED(connected_by(dbc, "DSN=uri")));
    TRL_CHECK(test, SQLDisconnect(dbc) == SQL_SUCCESS);
    TRL_CHECK(test, connected_by(dbc, "A_KEY_LONGER_THAN_ANY_KEY_OF_THE_DRIVER_S_OWN_SETTINGS_OR_OF_ODBC_S_OWN_EITHER="
                                      "1;;=x;SERVER={127.0.0.1") == SQL_ERROR);
    TRL_CHECK(test, SQLDriverConnectW(dbc, NULL, wide_text("SERVER=127.0.0.1;PORT=1;UID=ann;TLS_MODE=disable"), SQL_NTS,
                                      NULL, 0, NULL, SQL_DRIVER_NOPROMPT) == SQL_ERROR);
    TRL_CHECK(test, SQLConnect(dbc, sql_text("nowhere"), SQL_NTS, sql_text("ann"), SQL_NTS, sql_text("pw"), SQL_NTS) ==
                        SQL_ERROR);
    TRL_CHECK(test, connected_by(dbc, "TLS_MODE=sometimes;SERVER=127.0.0.1") == SQL_ERROR);
    TRL_CHECK(test, SQLDriverConnect(dbc, NULL, NULL, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT) == SQL_ERROR);
    TRL_CHECK(test, SQLDriverConnectW(dbc, NULL, NULL, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT) == SQL_ERROR);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
}

/**
 * Verifies: [DESIGN] D2 a rowset bound row-wise at an offset, scrolled
 * forward, back and to rows by their number; a row a column of which
 * cannot be given its status an error, the rowset's SQL_SUCCESS_WITH_INFO,
 * a rowset of no row but such SQL_ERROR; a scroll of a forward-only cursor
 * refused (HY106), as one of no kind.
 */
static void rowsets_are_bound_row_wise_and_scrolled(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_STATIC, 0);
    struct row {
        int16_t key;
        SQLLEN key_length;
    } rows[2];
    SQLLEN offset = 0;
    SQLUSMALLINT statuses[2];
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)2, 0);
    // NOLINTNEXTLINE(performance-no-int-to-ptr): ODBC passes an attribute's number as a pointer.
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_BIND_TYPE, (SQLPOINTER)sizeof rows[0], 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_BIND_OFFSET_PTR, &offset, 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_ROW_STATUS_PTR, statuses, 0);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1, 2, 3, 100000, 100001"), SQL_NTS) == SQL_SUCCESS);
    (void)SQLBindCol(o.stmt, 1, SQL_C_SSHORT, &rows[0].key, 0, &rows[0].key_length);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_RELATIVE, 2) == SQL_SUCCESS && rows[0].key == 2);
    TRL_CHECK(test, rows[1].key == 3 && statuses[0] == SQL_ROW_SUCCESS);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_PRIOR, 0) == SQL_SUCCESS && rows[0].key == 1);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_PRIOR, 0) == SQL_NO_DATA);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_ABSOLUTE, -3) == SQL_SUCCESS_WITH_INFO && rows[0].key == 3);
    TRL_CHECK(test, statuses[1] == SQL_ROW_ERROR);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_ABSOLUTE, 4) == SQL_ERROR);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_ABSOLUTE, 0) == SQL_NO_DATA);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_BOOKMARK, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLBindCol(o.stmt, 0, SQL_C_SSHORT, NULL, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLBindCol(o.stmt, 1, SQL_C_SSHORT, NULL, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLBindCol(o.stmt, 3, SQL_C_SSHORT, NULL, 0, NULL) == SQL_SUCCESS);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_ERROR);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_FORWARD_ONLY, 0);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("VALUES 1"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetchScroll(o.stmt, SQL_FETCH_FIRST, 0) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HY106") == 0);
    odbc_close(&o);
}

/* Writes the driver's registration and a data source of it, and names them to the environment. */
static bool registered(void) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + 32U];
    char path[PATH_BYTES + 64U];
    char text[ODBC_TEXT_BYTES * 2];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return false;
    }
    trl_format(dir, sizeof dir, "%s/live-odbc-catalog-ini", cwd);
    (void)mkdir(dir, S_IRWXU); // NOLINT(misc-include-cleaner): sys/stat.h's, by way of the system's own
    trl_format(path, sizeof path, "%s/odbc.ini", dir);
    trl_format(text, sizeof text,
               "[reports]\nDriver = Turborel\nSERVER = 127.0.0.1\nPORT = %u\nTLS_MODE = disable\nUID = ann\n"
               "[uri]\nURI = turborel://ann@127.0.0.1:%u/?tls_mode=disable\n",
               (unsigned int)SERVER_PORT, (unsigned int)SERVER_PORT);
    /* The installer library reads the files the environment names; set
       before any thread starts. */
    // NOLINTBEGIN(concurrency-mt-unsafe,misc-include-cleaner)
    return trl_test_write_file(path, text) && setenv("ODBCSYSINI", dir, 1) == 0 && setenv("ODBCINI", path, 1) == 0;
    // NOLINTEND(concurrency-mt-unsafe,misc-include-cleaner)
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!registered() || !odbc_start("live-odbc-catalog", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(catalog_functions_take_sqlwchar_and_identifiers),
        TRL_TEST_CASE(connections_are_made_each_way),
        TRL_TEST_CASE(rowsets_are_bound_row_wise_and_scrolled),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
