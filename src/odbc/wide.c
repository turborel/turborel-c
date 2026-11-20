/**
 * @file wide.c
 * @brief The Unicode entry points (RULES.md D2), the `W` functions a
 * driver manager calls of a Unicode application: each takes and gives
 * text as SQLWCHAR, turned to and from UTF-8, and does its ANSI
 * function's work. The catalog functions' are `wide_catalog.c`'s.
 */
#include "odbc/describe.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Fails as text given cannot be taken: null where it may not be, or memory not had. */
static SQLRETURN not_taken(trl_odbc_diag *diag, const void *given) {
    trl_odbc_clear(diag);
    return given == NULL ? trl_odbc_fail(diag, "HY009", "Invalid use of null pointer") : trl_odbc_no_memory(diag);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLConnectW(SQLHDBC handle, SQLWCHAR *dsn, SQLSMALLINT dsn_length, SQLWCHAR *user,
                                              SQLSMALLINT user_length, SQLWCHAR *password,
                                              SQLSMALLINT password_length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *texts[TRL_ODBC_CREDENTIALS] = {
        trl_odbc_wide_text(dsn, dsn_length),
        trl_odbc_wide_text(user, user_length),
        trl_odbc_wide_text(password, password_length),
    };
    bool had = (dsn == NULL || texts[TRL_ODBC_DSN] != NULL) && (user == NULL || texts[TRL_ODBC_USER] != NULL) &&
               (password == NULL || texts[TRL_ODBC_PASSWORD] != NULL);
    const char *const credentials[TRL_ODBC_CREDENTIALS] = {
        texts[TRL_ODBC_DSN],
        texts[TRL_ODBC_USER],
        texts[TRL_ODBC_PASSWORD],
    };
    SQLRETURN returned = had ? trl_odbc_connect(dbc, NULL, credentials) : trl_odbc_no_memory(&dbc->diag);
    for (size_t i = 0U; i < TRL_ODBC_CREDENTIALS; ++i) {
        free(texts[i]);
    }
    return returned;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLDriverConnectW(SQLHDBC handle, SQLHWND window, SQLWCHAR *in, SQLSMALLINT in_length,
                                                    SQLWCHAR *out, SQLSMALLINT capacity, SQLSMALLINT *out_length,
                                                    SQLUSMALLINT completion) {
    (void)window;
    (void)completion;
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *string = trl_odbc_wide_text(in, in_length);
    if (string == NULL) {
        return not_taken(&dbc->diag, in);
    }
    const char *const none[TRL_ODBC_CREDENTIALS] = {NULL, NULL, NULL};
    SQLRETURN returned = trl_odbc_connect(dbc, string, none);
    if (SQL_SUCCEEDED(returned)) {
        trl_odbc_out given = {out, capacity, out_length, NULL, NULL, true, false};
        returned = trl_odbc_return(&dbc->diag, trl_odbc_put(&dbc->diag, &given, string));
    }
    free(string);
    return returned;
}

/* Runs `work` of a statement's text given as SQLWCHAR. */
static SQLRETURN with_sql(SQLHSTMT handle, const SQLWCHAR *sql, SQLINTEGER length,
                          SQLRETURN (*work)(trl_odbc_stmt *, const char *)) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *text = trl_odbc_wide_text(sql, length);
    if (text == NULL) {
        return not_taken(&stmt->diag, sql);
    }
    SQLRETURN returned = work(stmt, text);
    free(text);
    return returned;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT handle, SQLWCHAR *sql, SQLINTEGER length) {
    return with_sql(handle, sql, length, trl_odbc_exec_direct);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLPrepareW(SQLHSTMT handle, SQLWCHAR *sql, SQLINTEGER length) {
    return with_sql(handle, sql, length, trl_odbc_prepare);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetCursorNameW(SQLHSTMT handle, SQLWCHAR *name, SQLSMALLINT length) {
    return with_sql(handle, name, length, trl_odbc_set_cursor_name);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetCursorNameW(SQLHSTMT handle, SQLWCHAR *name, SQLSMALLINT capacity,
                                                    SQLSMALLINT *length) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {name, capacity, length, NULL, NULL, true, false};
    return trl_odbc_get_cursor_name(stmt, &out);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLNativeSqlW(SQLHDBC handle, SQLWCHAR *in, SQLINTEGER in_length, SQLWCHAR *out,
                                                SQLINTEGER capacity, SQLINTEGER *out_length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *text = trl_odbc_wide_text(in, in_length);
    if (text == NULL) {
        return not_taken(&dbc->diag, in);
    }
    trl_odbc_out given = {out, capacity, NULL, out_length, NULL, true, false};
    SQLRETURN returned = trl_odbc_native_sql(dbc, text, &given);
    free(text);
    return returned;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT handle, SQLUSMALLINT column, SQLWCHAR *name,
                                                  SQLSMALLINT capacity, SQLSMALLINT *name_length, SQLSMALLINT *sql_type,
                                                  SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out named = {name, capacity, name_length, NULL, NULL, true, false};
    trl_odbc_described out = {sql_type, size, digits, nullable};
    return trl_odbc_describe_col(stmt, column, &named, &out);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLColAttributeW(SQLHSTMT handle, SQLUSMALLINT column, SQLUSMALLINT id,
                                                   SQLPOINTER text, SQLSMALLINT capacity, SQLSMALLINT *length,
                                                   SQLLEN *number) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {text, capacity, length, NULL, NULL, true, true};
    return trl_odbc_col_attribute(stmt, column, id, &out, number);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDiagRecW(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number,
                                                 SQLWCHAR *state, SQLINTEGER *native, SQLWCHAR *message,
                                                 SQLSMALLINT capacity, SQLSMALLINT *length) {
    trl_odbc_rec_out out = {state, native, {message, capacity, length, NULL, NULL, true, false}};
    return trl_odbc_diag_rec(type, handle, number, &out);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDiagFieldW(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number,
                                                   SQLSMALLINT field, SQLPOINTER info, SQLSMALLINT capacity,
                                                   SQLSMALLINT *length) {
    trl_odbc_out out = {info, capacity, length, NULL, NULL, true, true};
    return trl_odbc_diag_field(&out, type, handle, number, field);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetInfoW(SQLHDBC handle, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT capacity,
                                              SQLSMALLINT *length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {value, capacity, length, NULL, NULL, true, true};
    return trl_odbc_get_info(dbc, type, &out);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetConnectAttrW(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value,
                                                     SQLINTEGER capacity, SQLINTEGER *length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {value, capacity, NULL, length, NULL, true, true};
    return trl_odbc_get_connect_attr(dbc, attribute, &out);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetConnectAttrW(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value,
                                                     SQLINTEGER length) {
    (void)length;
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    return dbc == NULL ? SQL_INVALID_HANDLE : trl_odbc_set_connect_attr(dbc, attribute, value);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT handle, SQLINTEGER attribute, SQLPOINTER value,
                                                  SQLINTEGER capacity, SQLINTEGER *length) {
    (void)capacity;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : trl_odbc_get_stmt_attr(stmt, attribute, value, length);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT handle, SQLINTEGER attribute, SQLPOINTER value,
                                                  SQLINTEGER length) {
    (void)length;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : trl_odbc_set_stmt_attr(stmt, attribute, value);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDescFieldW(SQLHDESC handle, SQLSMALLINT number, SQLSMALLINT id,
                                                   SQLPOINTER value, SQLINTEGER capacity, SQLINTEGER *length) {
    trl_odbc_desc *desc = trl_odbc_desc_of(handle);
    if (desc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {value, capacity, NULL, length, NULL, true, true};
    return trl_odbc_desc_get(desc, number, id, &out);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetDescFieldW(SQLHDESC handle, SQLSMALLINT number, SQLSMALLINT id,
                                                   SQLPOINTER value, SQLINTEGER length) {
    return SQLSetDescField(handle, number, id, value, length);
}
