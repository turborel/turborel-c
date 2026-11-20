/**
 * @file odbc_live.c
 * @brief What the ODBC driver's live tests share.
 */
#include "odbc_live.h"

#include "format.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The texts a test gives at once, and room for a path. */
#define TEXTS_AT_ONCE 8
#define PATH_BYTES 1024U

char odbc_connection_string[ODBC_TEXT_BYTES];

SQLCHAR *sql_text(const char *text) {
    static SQLCHAR buffers[TEXTS_AT_ONCE][ODBC_TEXT_BYTES];
    static size_t next;
    SQLCHAR *buffer = buffers[next++ % TEXTS_AT_ONCE];
    trl_format((char *)buffer, ODBC_TEXT_BYTES, "%s", text);
    return buffer;
}

bool odbc_open(odbc_opened *o) {
    memset(o, 0, sizeof *o);
    bool made = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &o->env) == SQL_SUCCESS &&
                SQLSetEnvAttr(o->env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0) == SQL_SUCCESS &&
                SQLAllocHandle(SQL_HANDLE_DBC, o->env, &o->dbc) == SQL_SUCCESS;
    SQLCHAR out[ODBC_TEXT_BYTES];
    SQLSMALLINT out_length = 0;
    made = made && SQL_SUCCEEDED(SQLDriverConnect(o->dbc, NULL, sql_text(odbc_connection_string), SQL_NTS, out,
                                                  (SQLSMALLINT)sizeof out, &out_length, SQL_DRIVER_NOPROMPT));
    return made && SQLAllocHandle(SQL_HANDLE_STMT, o->dbc, &o->stmt) == SQL_SUCCESS;
}

void odbc_close(odbc_opened *o) {
    (void)SQLFreeHandle(SQL_HANDLE_STMT, o->stmt);
    (void)SQLDisconnect(o->dbc);
    (void)SQLFreeHandle(SQL_HANDLE_DBC, o->dbc);
    (void)SQLFreeHandle(SQL_HANDLE_ENV, o->env);
}

bool odbc_run(SQLHSTMT stmt, const char *sql) {
    SQLRETURN ran = SQLExecDirect(stmt, sql_text(sql), SQL_NTS);
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    return SQL_SUCCEEDED(ran) || ran == SQL_NO_DATA;
}

const char *odbc_state(SQLSMALLINT type, SQLHANDLE handle, SQLINTEGER *native) {
    static SQLCHAR state[6];
    SQLCHAR message[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    SQLINTEGER code = 0;
    state[0] = '\0';
    (void)SQLGetDiagRec(type, handle, 1, state, &code, message, (SQLSMALLINT)sizeof message, &length);
    if (native != NULL) {
        *native = code;
    }
    return (const char *)state;
}

SQLRETURN odbc_said(SQLHSTMT stmt, SQLRETURN returned) {
    if (!SQL_SUCCEEDED(returned) && returned != SQL_NO_DATA) {
        SQLCHAR state[6] = "";
        SQLCHAR message[ODBC_TEXT_BYTES] = "";
        (void)SQLGetDiagRec(SQL_HANDLE_STMT, stmt, 1, state, NULL, message, (SQLSMALLINT)sizeof message, NULL);
        (void)fprintf(stderr, "%s %s\n", (const char *)state, (const char *)message);
    }
    return returned;
}

SQLHDESC odbc_descriptor(SQLHSTMT stmt, SQLINTEGER attribute) {
    SQLHDESC desc = SQL_NULL_HANDLE;
    (void)SQLGetStmtAttr(stmt, attribute, (SQLPOINTER)&desc, 0, NULL);
    return desc;
}

bool odbc_start(const char *name, bool *skip) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + ODBC_TEXT_BYTES];
    *skip = false;
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return false;
    }
    trl_format(dir, sizeof dir, "%s/%s", cwd, name);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        *skip = getenv("TRL_TEST_SERVER") == NULL; // NOLINT(concurrency-mt-unsafe)
        return false;
    }
    trl_format(odbc_connection_string, sizeof odbc_connection_string,
               "DRIVER=turborel;SERVER=127.0.0.1;PORT=%u;UID=ann;TLS_MODE=disable;APPLICATION_NAME=odbc",
               (unsigned int)SERVER_PORT);
    trl_conn *owner = live_trusted(NULL);
    trl_result *result = NULL;
    bool ready = owner != NULL && trl_query(owner, "CREATE SCHEMA AUTHORIZATION ANN", &result) == TRL_OK;
    trl_result_free(result);
    result = NULL;
    ready = ready && trl_query(owner, "COMMIT", &result) == TRL_OK;
    trl_result_free(result);
    trl_conn_free(owner);
    return ready;
}

void odbc_stop(void) {
    server_stop(&live_server);
}
