/**
 * @file execute.c
 * @brief Statements run (RULES.md D2): SQLExecDirect, SQLPrepare and
 * SQLExecute, their parameters bound by SQLBindParameter and described by
 * SQLNumParams and SQLDescribeParam, and what they came to, SQLRowCount
 * and SQLMoreResults. Each statement's escape sequences are written as
 * Turborel's SQL first (`escapes.c`). A query's rows are read whole as it
 * runs, its cursor then on the client, so a cursor may scroll and the
 * connection runs another statement while one is open. A searched UPDATE
 * or DELETE of no row returns SQL_NO_DATA, as ODBC 3 asks.
 */
#include "format.h"
#include "odbc/escapes.h"
#include "odbc/odbc.h"
#include "odbc/params.h"
#include "odbc/types.h"
#include "stmt/stmt.h"
#include "turborel/turborel.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void trl_odbc_close_cursor(trl_odbc_stmt *stmt) {
    trl_result_free(stmt->result);
    stmt->result = NULL;
    stmt->rowset = 0U;
    stmt->rowset_rows = 0U;
    stmt->fetched = false;
    stmt->got_column = 0U;
    free(stmt->got.owned);
    memset(&stmt->got, 0, sizeof stmt->got);
}

void trl_odbc_unprepare(trl_odbc_stmt *stmt) {
    if (stmt->prepared == NULL) {
        return;
    }
    if (stmt->dbc->conn != NULL && !trl_conn_is_lost(stmt->dbc->conn)) {
        (void)trl_deallocate(stmt->dbc->conn, stmt->prepared);
    }
    trl_statement_free(stmt->prepared);
    stmt->prepared = NULL;
}

/* Whether `sql` is a searched UPDATE or DELETE, by its first word. */
static bool searched(const char *sql) {
    while (isspace((unsigned char)*sql) || *sql == '(') {
        ++sql;
    }
    static const char *const words[] = {"UPDATE", "DELETE"};
    for (size_t i = 0U; i < sizeof words / sizeof words[0]; ++i) {
        size_t length = strlen(words[i]);
        bool same = true;
        for (size_t k = 0U; k < length && same; ++k) {
            same = toupper((unsigned char)sql[k]) == words[i][k];
        }
        if (same && !isalnum((unsigned char)sql[length])) {
            return true;
        }
    }
    return false;
}

/* What a statement run came to: its rows kept as the cursor, or the rows
   it changed counted; its warning, or its failure. */
static SQLRETURN finished(trl_odbc_stmt *stmt, trl_status ran, trl_result *result, const char *sql) {
    trl_conn *conn = stmt->dbc->conn;
    if (ran == TRL_NO_MEMORY) {
        return trl_odbc_no_memory(&stmt->diag);
    }
    if (ran != TRL_OK) {
        return trl_odbc_run_failed(stmt);
    }
    uint64_t rows = 0U;
    bool counted = trl_conn_rows_affected(conn, &rows);
    if (trl_result_has_rows(result)) {
        stmt->result = result;
        stmt->row_count = (SQLLEN)trl_result_row_count(result);
    } else {
        trl_result_free(result);
        stmt->row_count = counted ? (SQLLEN)rows : -1;
    }
    SQLRETURN said = trl_odbc_warned(&stmt->diag, conn);
    if (said == SQL_SUCCESS && counted && rows == 0U && stmt->result == NULL && sql != NULL && searched(sql)) {
        return SQL_NO_DATA;
    }
    return said;
}

/* The statement's text, its escapes written as SQL, a new string; null
   with the statement's diagnostics if it cannot be. */
static char *escaped(trl_odbc_stmt *stmt, const char *sql) {
    if (stmt->noscan) {
        char *copy = trl_odbc_text((const SQLCHAR *)sql, SQL_NTS);
        if (copy == NULL) {
            (void)trl_odbc_no_memory(&stmt->diag);
        }
        return copy;
    }
    bool malformed = false;
    char *text = trl_odbc_escaped(sql, &malformed);
    if (text == NULL) {
        if (malformed) {
            (void)trl_odbc_fail(&stmt->diag, "42000",
                                "Syntax error or access violation: an escape sequence is "
                                "not closed, or of no kind ODBC has");
        } else {
            (void)trl_odbc_no_memory(&stmt->diag);
        }
    }
    return text;
}

/* Executes `statement` with the parameters bound, then gives up its result. */
static SQLRETURN executed(trl_odbc_stmt *stmt, const trl_statement *statement, const char *sql) {
    trl_params *params = NULL;
    SQLRETURN bound = trl_odbc_params(stmt, trl_statement_parameter_count(statement), &params);
    if (bound != SQL_SUCCESS) {
        return bound;
    }
    SQLRETURN ready = trl_odbc_modes_applied(stmt);
    if (ready != SQL_SUCCESS) {
        trl_params_free(params);
        return ready;
    }
    trl_result *result = NULL;
    trl_status ran = trl_execute(stmt->dbc->conn, statement, params, &result);
    trl_params_free(params);
    return finished(stmt, ran, result, sql);
}

SQLRETURN trl_odbc_exec_direct(trl_odbc_stmt *stmt, const char *sql) {
    trl_odbc_clear(&stmt->diag);
    trl_odbc_close_cursor(stmt);
    trl_odbc_unprepare(stmt);
    stmt->row_count = -1;
    char *text = escaped(stmt, sql);
    if (text == NULL) {
        return stmt->diag.returned;
    }
    trl_conn *conn = stmt->dbc->conn;
    SQLRETURN returned = SQL_SUCCESS;
    if (stmt->apd->count == 0) {
        returned = trl_odbc_modes_applied(stmt);
        trl_result *result = NULL;
        trl_status ran = returned == SQL_SUCCESS ? trl_query(conn, text, &result) : TRL_OK;
        returned = returned == SQL_SUCCESS ? finished(stmt, ran, result, text) : returned;
    } else {
        trl_statement *statement = NULL;
        trl_status prepared = trl_prepare(conn, text, &statement);
        returned = prepared == TRL_OK ? executed(stmt, statement, text) : finished(stmt, prepared, NULL, text);
        trl_deallocate_after_run(conn, statement);
        trl_statement_free(statement);
    }
    free(text);
    return returned;
}

SQLRETURN trl_odbc_prepare(trl_odbc_stmt *stmt, const char *sql) {
    trl_odbc_clear(&stmt->diag);
    trl_odbc_close_cursor(stmt);
    trl_odbc_unprepare(stmt);
    stmt->row_count = -1;
    char *text = escaped(stmt, sql);
    if (text == NULL) {
        return stmt->diag.returned;
    }
    trl_conn *conn = stmt->dbc->conn;
    trl_status prepared = trl_prepare(conn, text, &stmt->prepared);
    free(text);
    if (prepared != TRL_OK) {
        trl_statement_free(stmt->prepared);
        stmt->prepared = NULL;
        return prepared == TRL_NO_MEMORY ? trl_odbc_no_memory(&stmt->diag)
                                         : trl_odbc_failed(&stmt->diag, trl_conn_failure(conn));
    }
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLExecDirect(SQLHSTMT handle, SQLCHAR *sql, SQLINTEGER length) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *text = trl_odbc_text(sql, length);
    if (text == NULL) {
        trl_odbc_clear(&stmt->diag);
        return sql == NULL ? trl_odbc_fail(&stmt->diag, "HY009", "Invalid use of null pointer")
                           : trl_odbc_no_memory(&stmt->diag);
    }
    SQLRETURN returned = trl_odbc_exec_direct(stmt, text);
    free(text);
    return returned;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLPrepare(SQLHSTMT handle, SQLCHAR *sql, SQLINTEGER length) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *text = trl_odbc_text(sql, length);
    if (text == NULL) {
        trl_odbc_clear(&stmt->diag);
        return sql == NULL ? trl_odbc_fail(&stmt->diag, "HY009", "Invalid use of null pointer")
                           : trl_odbc_no_memory(&stmt->diag);
    }
    SQLRETURN returned = trl_odbc_prepare(stmt, text);
    free(text);
    return returned;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLExecute(SQLHSTMT handle) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (stmt->prepared == NULL) {
        return trl_odbc_fail(&stmt->diag, "HY010", "Function sequence error: no statement is prepared");
    }
    if (stmt->result != NULL) {
        return trl_odbc_fail(&stmt->diag, "24000", "Invalid cursor state");
    }
    stmt->row_count = -1;
    return executed(stmt, stmt->prepared, NULL);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLNumParams(SQLHSTMT handle, SQLSMALLINT *count) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (stmt->prepared == NULL) {
        return trl_odbc_fail(&stmt->diag, "HY010", "Function sequence error: no statement is prepared");
    }
    if (count != NULL) {
        *count = (SQLSMALLINT)trl_statement_parameter_count(stmt->prepared);
    }
    return SQL_SUCCESS;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLDescribeParam(SQLHSTMT handle, SQLUSMALLINT number, SQLSMALLINT *sql_type,
                                                   SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (stmt->prepared == NULL) {
        return trl_odbc_fail(&stmt->diag, "HY010", "Function sequence error: no statement is prepared");
    }
    if (number < 1 || number > trl_statement_parameter_count(stmt->prepared)) {
        return trl_odbc_fail(&stmt->diag, "07009", "Invalid descriptor index");
    }
    trl_odbc_type t;
    trl_odbc_type_of(trl_statement_parameter_type(stmt->prepared, (size_t)number - 1U), NULL, &t);
    if (sql_type != NULL) {
        *sql_type = t.sql_type;
    }
    if (size != NULL) {
        *size = t.column_size;
    }
    if (digits != NULL) {
        *digits = t.digits;
    }
    if (nullable != NULL) {
        *nullable = SQL_NULLABLE_UNKNOWN;
    }
    return SQL_SUCCESS;
}

/* A parameter's binding: of the application's C value, and of its SQL type. */
typedef struct binding {
    SQLSMALLINT io;
    SQLSMALLINT c_type;
    SQLSMALLINT sql_type;
    SQLULEN size;
    SQLSMALLINT digits;
    SQLPOINTER data;
    SQLLEN capacity;
    SQLLEN *indicator;
} binding;

/* SQLBindParameter's work. */
static SQLRETURN parameter_bound(trl_odbc_stmt *stmt, SQLUSMALLINT number, const binding *b) {
    trl_odbc_clear(&stmt->diag);
    if (number < 1) {
        return trl_odbc_fail(&stmt->diag, "07009", "Invalid descriptor index");
    }
    if (b->io != SQL_PARAM_INPUT) {
        return trl_odbc_fail(&stmt->diag, "HYC00", "Optional feature not implemented: output parameters");
    }
    trl_odbc_field *app = trl_odbc_desc_field(stmt->apd, (SQLSMALLINT)number);
    trl_odbc_field *sql = trl_odbc_desc_field(&stmt->implicit[TRL_ODBC_IPD], (SQLSMALLINT)number);
    if (app == NULL || sql == NULL) {
        return trl_odbc_no_memory(&stmt->diag);
    }
    trl_odbc_field_type(app, b->c_type);
    app->data = b->data;
    app->octet_length = b->capacity;
    app->length_ptr = b->indicator;
    app->indicator_ptr = b->indicator;
    trl_odbc_field_type(sql, b->sql_type);
    sql->length = b->size;
    sql->precision = (SQLSMALLINT)(b->size > INT16_MAX ? INT16_MAX : b->size);
    sql->scale = b->digits;
    sql->parameter_type = b->io;
    return SQL_SUCCESS;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLBindParameter(SQLHSTMT handle, SQLUSMALLINT number, SQLSMALLINT io,
                                                   SQLSMALLINT c_type, SQLSMALLINT sql_type, SQLULEN size,
                                                   SQLSMALLINT digits, SQLPOINTER data, SQLLEN capacity,
                                                   SQLLEN *indicator) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    binding b = {io, c_type, sql_type, size, digits, data, capacity, indicator};
    return parameter_bound(stmt, number, &b);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLRowCount(SQLHSTMT handle, SQLLEN *count) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (count == NULL) {
        return trl_odbc_fail(&stmt->diag, "HY009", "Invalid use of null pointer");
    }
    *count = stmt->row_count;
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLMoreResults(SQLHSTMT handle) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    /* A statement has one result: there is no other after it. */
    trl_odbc_close_cursor(stmt);
    return SQL_NO_DATA;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT handle) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (stmt->result == NULL) {
        return trl_odbc_fail(&stmt->diag, "24000", "Invalid cursor state");
    }
    trl_odbc_close_cursor(stmt);
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLCancel(SQLHSTMT handle) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    /* Called from another thread while the statement runs, it asks the
       server to cancel it; else there is nothing to cancel. */
    trl_canceller *canceller = stmt->dbc->canceller;
    if (canceller != NULL && trl_cancel(canceller) != TRL_OK) {
        return trl_odbc_failed(&stmt->diag, trl_canceller_failure(canceller));
    }
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLParamData(SQLHSTMT handle, SQLPOINTER *value) {
    (void)value;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    return trl_odbc_fail(&stmt->diag, "HY010", "Function sequence error: no data at execution is asked for");
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLPutData(SQLHSTMT handle, SQLPOINTER data, SQLLEN length) {
    (void)data;
    (void)length;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    return trl_odbc_fail(&stmt->diag, "HY010", "Function sequence error: no data at execution is asked for");
}

/* SQLNativeSql's work: the statement as the driver would send it. */
SQLRETURN trl_odbc_native_sql(trl_odbc_dbc *dbc, const char *sql, const trl_odbc_out *out) {
    trl_odbc_clear(&dbc->diag);
    bool malformed = false;
    char *text = trl_odbc_escaped(sql, &malformed);
    if (text == NULL) {
        return malformed ? trl_odbc_fail(&dbc->diag, "42000", "Syntax error or access violation")
                         : trl_odbc_no_memory(&dbc->diag);
    }
    SQLRETURN put = trl_odbc_put(&dbc->diag, out, text);
    free(text);
    return put;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLNativeSql(SQLHDBC handle, SQLCHAR *in, SQLINTEGER in_length, SQLCHAR *out,
                                               SQLINTEGER capacity, SQLINTEGER *out_length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *text = trl_odbc_text(in, in_length);
    if (text == NULL) {
        trl_odbc_clear(&dbc->diag);
        return in == NULL ? trl_odbc_fail(&dbc->diag, "HY009", "Invalid use of null pointer")
                          : trl_odbc_no_memory(&dbc->diag);
    }
    trl_odbc_out given = {out, capacity, NULL, out_length, NULL, false, false};
    SQLRETURN returned = trl_odbc_native_sql(dbc, text, &given);
    free(text);
    return returned;
}

/* The most characters of a cursor's name, and room for one the driver makes. */
#define TRL_CURSOR_NAME_MOST 128U
#define TRL_CURSOR_NAME_BYTES 32U

SQLRETURN trl_odbc_set_cursor_name(trl_odbc_stmt *stmt, const char *name) {
    trl_odbc_clear(&stmt->diag);
    size_t length = strlen(name);
    if (length == 0U || length > TRL_CURSOR_NAME_MOST) {
        return trl_odbc_fail(&stmt->diag, "34000", "Invalid cursor name");
    }
    char *copy = trl_odbc_text((const SQLCHAR *)name, SQL_NTS);
    if (copy == NULL) {
        return trl_odbc_no_memory(&stmt->diag);
    }
    free(stmt->cursor_name);
    stmt->cursor_name = copy;
    return SQL_SUCCESS;
}

SQLRETURN trl_odbc_get_cursor_name(trl_odbc_stmt *stmt, const trl_odbc_out *out) {
    trl_odbc_clear(&stmt->diag);
    char made[TRL_CURSOR_NAME_BYTES];
    trl_format(made, sizeof made, "SQL_CUR%p", (void *)stmt);
    return trl_odbc_put(&stmt->diag, out, stmt->cursor_name != NULL ? stmt->cursor_name : made);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetCursorName(SQLHSTMT handle, SQLCHAR *name, SQLSMALLINT length) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *text = trl_odbc_text(name, length);
    if (text == NULL) {
        trl_odbc_clear(&stmt->diag);
        return name == NULL ? trl_odbc_fail(&stmt->diag, "HY009", "Invalid use of null pointer")
                            : trl_odbc_no_memory(&stmt->diag);
    }
    SQLRETURN set = trl_odbc_set_cursor_name(stmt, text);
    free(text);
    return set;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetCursorName(SQLHSTMT handle, SQLCHAR *name, SQLSMALLINT capacity,
                                                   SQLSMALLINT *length) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {name, capacity, length, NULL, NULL, false, false};
    return trl_odbc_get_cursor_name(stmt, &out);
}
