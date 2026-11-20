/**
 * @file odbc_manager_test.c
 * @brief The ODBC driver loaded by a driver manager, unixODBC's or
 * iODBC's, against a real server (RULES.md R-TEST-8): registered in an
 * odbcinst.ini and a data source of it in an odbc.ini the test writes,
 * reached by SQLConnect of the data source's name and by SQLDriverConnect
 * of its path; its Unicode functions called as the manager calls them.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "format.h"
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

// NOLINTBEGIN(misc-include-cleaner)
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sqlucode.h>
// NOLINTEND(misc-include-cleaner)

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
#define TEXT_BYTES 512

/* The directory of the test's odbcinst.ini and odbc.ini. */
static char configured[PATH_BYTES];

/* `text` as the SQLCHAR an ODBC function takes, a copy in one of a few buffers used in turn. */
static SQLCHAR *sql_text(const char *text) {
    static SQLCHAR buffers[4][TEXT_BYTES];
    static size_t next;
    SQLCHAR *buffer = buffers[next++ % 4U];
    trl_format((char *)buffer, TEXT_BYTES, "%s", text);
    return buffer;
}

/* Writes the driver's registration and a data source of it. */
static bool registered(void) {
    char path[PATH_BYTES + 16U];
    char text[TEXT_BYTES * 2];
    trl_format(path, sizeof path, "%s/odbcinst.ini", configured);
    trl_format(text, sizeof text, "[Turborel]\nDescription = Turborel\nDriver = %s\nSetup = %s\n", TRL_ODBC_MODULE,
               TRL_ODBC_MODULE);
    bool written = trl_test_write_file(path, text);
    trl_format(path, sizeof path, "%s/odbc.ini", configured);
    trl_format(text, sizeof text,
               "[reports]\nDriver = Turborel\nSERVER = 127.0.0.1\nPORT = %u\nTLS_MODE = disable\n"
               "APPLICATION_NAME = managed\n",
               (unsigned int)SERVER_PORT);
    return written && trl_test_write_file(path, text);
}

/* An environment of the manager, ODBC 3. */
static SQLHENV env_new(void) {
    SQLHENV env = SQL_NULL_HANDLE;
    (void)SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    (void)SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    return env;
}

/* The first diagnostic's SQLSTATE and message, on standard error. */
static void told(SQLSMALLINT type, SQLHANDLE handle) {
    SQLCHAR state[6] = "";
    SQLCHAR message[TEXT_BYTES] = "";
    (void)SQLGetDiagRec(type, handle, 1, state, NULL, message, (SQLSMALLINT)sizeof message, NULL);
    (void)fprintf(stderr, "%s %s\n", (const char *)state, (const char *)message);
}

/**
 * Verifies: [DESIGN] D2 the driver registered in odbcinst.ini is loaded
 * by the driver manager by a data source's name, its attributes the
 * driver's settings, the user given by SQLConnect; a query's rows read.
 */
static void a_data_source_connects_through_the_manager(trl_test *test) {
    SQLHENV env = env_new();
    SQLHDBC dbc = SQL_NULL_HANDLE;
    SQLHSTMT stmt = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc) == SQL_SUCCESS);
    SQLRETURN connected =
        SQLConnect(dbc, sql_text("reports"), SQL_NTS, sql_text("bob"), SQL_NTS, sql_text(""), SQL_NTS);
    if (!SQL_SUCCEEDED(connected)) {
        told(SQL_HANDLE_DBC, dbc);
    }
    TRL_CHECK(test, SQL_SUCCEEDED(connected));
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLExecDirect(stmt, sql_text("VALUES CURRENT_USER"), SQL_NTS) == SQL_SUCCESS);
    SQLCHAR user[TEXT_BYTES];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLFetch(stmt) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetData(stmt, 1, SQL_C_CHAR, user, (SQLLEN)sizeof user, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp((const char *)user, "BOB") == 0);
    (void)SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    (void)SQLDisconnect(dbc);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
}

/**
 * Verifies: [DESIGN] D2 the driver reached by its path in a connection
 * string, its Unicode functions called through the manager: a statement
 * given as SQLWCHAR, a column's name and value given back as SQLWCHAR.
 */
static void the_unicode_functions_work_through_the_manager(trl_test *test) {
    SQLHENV env = env_new();
    SQLHDBC dbc = SQL_NULL_HANDLE;
    SQLHSTMT stmt = SQL_NULL_HANDLE;
    (void)SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    char string[TEXT_BYTES];
    trl_format(string, sizeof string, "DRIVER=%s;SERVER=127.0.0.1;PORT=%u;UID=ann;TLS_MODE=disable", TRL_ODBC_MODULE,
               (unsigned int)SERVER_PORT);
    SQLRETURN connected = SQLDriverConnect(dbc, NULL, sql_text(string), SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(connected)) {
        told(SQL_HANDLE_DBC, dbc);
    }
    TRL_CHECK(test, SQL_SUCCEEDED(connected));
    (void)SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    SQLWCHAR sql[] = {'V', 'A', 'L', 'U', 'E', 'S', ' ', '\'', 0x263A, '\'', 0};
    TRL_CHECK(test, SQLExecDirectW(stmt, sql, SQL_NTS) == SQL_SUCCESS && SQLFetch(stmt) == SQL_SUCCESS);
    SQLWCHAR value[TEXT_BYTES];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(stmt, 1, SQL_C_WCHAR, value, (SQLLEN)sizeof value, &length) == SQL_SUCCESS);
    TRL_CHECK(test, value[0] == 0x263A && value[1] == 0);
    SQLWCHAR name[TEXT_BYTES];
    SQLSMALLINT name_length = 0;
    SQLSMALLINT sql_type = 0;
    TRL_CHECK(test,
              SQLDescribeColW(stmt, 1, name, TEXT_BYTES, &name_length, &sql_type, NULL, NULL, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, sql_type == SQL_CHAR || sql_type == SQL_VARCHAR);
    (void)SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    (void)SQLDisconnect(dbc);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, env);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-manager"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-manager", cwd);
    trl_format(configured, sizeof configured, "%s/live-manager-ini", cwd);
    char ini[PATH_BYTES + sizeof "/odbc.ini"];
    trl_format(ini, sizeof ini, "%s/odbc.ini", configured);
    (void)mkdir(configured, S_IRWXU); // NOLINT(misc-include-cleaner): sys/stat.h's, by way of the system's own
    /* The manager reads the files the environment names, as each test's
       own; set before any thread starts. */
    // NOLINTBEGIN(concurrency-mt-unsafe,misc-include-cleaner)
    bool set = setenv("ODBCSYSINI", configured, 1) == 0 && setenv("ODBCINSTINI", "odbcinst.ini", 1) == 0 &&
               setenv("ODBCINI", ini, 1) == 0;
    // NOLINTEND(concurrency-mt-unsafe,misc-include-cleaner)
    const char *why = NULL;
    if (!set || !registered() || !server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why != NULL ? why : "the manager's files cannot be written");
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    trl_result *result = NULL;
    bool ready = owner != NULL && trl_query(owner, "COMMIT", &result) == TRL_OK;
    trl_result_free(result);
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_data_source_connects_through_the_manager),
        TRL_TEST_CASE(the_unicode_functions_work_through_the_manager),
    };
    int run_result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return run_result;
}
