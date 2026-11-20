/**
 * @file attrs.c
 * @brief Connection and statement attributes (RULES.md D2), and
 * transactions: SQLSetConnectAttr and SQLGetConnectAttr, autocommit on
 * by default as ODBC's is; SQLSetStmtAttr and SQLGetStmtAttr, the
 * rowset's size and binding, the descriptors in use, the cursor's type;
 * SQLEndTran committing or rolling back a connection's transaction, or
 * each of an environment's. A value the driver takes another for is
 * changed, 01S02.
 */
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The session's setting that tells its catalog. */
#define TRL_CATALOG_SETTING "CATALOG"

/* Says a value was changed for one the driver takes. */
static SQLRETURN changed(trl_odbc_diag *diag) {
    return trl_odbc_warn(diag, "01S02", "Option value changed");
}

/* Turns autocommit on or off; turning it on commits a transaction begun. */
static SQLRETURN autocommit_set(trl_odbc_dbc *dbc, bool on) {
    if (dbc->conn != NULL && on && !dbc->autocommit && trl_conn_in_transaction(dbc->conn)) {
        trl_result *result = NULL;
        trl_status committed = trl_query(dbc->conn, "COMMIT", &result);
        trl_result_free(result);
        if (committed != TRL_OK) {
            return trl_odbc_failed(&dbc->diag, trl_conn_failure(dbc->conn));
        }
    }
    dbc->autocommit = on;
    if (dbc->conn != NULL) {
        (void)trl_conn_set_autocommit(dbc->conn, on);
    }
    return SQL_SUCCESS;
}

/* SQLSetConnectAttr's work. */
SQLRETURN trl_odbc_set_connect_attr(trl_odbc_dbc *dbc, SQLINTEGER attribute, SQLPOINTER value) {
    trl_odbc_clear(&dbc->diag);
    SQLULEN number = (SQLULEN)(uintptr_t)value;
    switch (attribute) {
    case SQL_ATTR_AUTOCOMMIT:
        return autocommit_set(dbc, number == SQL_AUTOCOMMIT_ON);
    case SQL_ATTR_LOGIN_TIMEOUT:
        dbc->login_timeout = (SQLUINTEGER)number;
        return SQL_SUCCESS;
    case SQL_ATTR_ACCESS_MODE:
        dbc->read_only = number == SQL_MODE_READ_ONLY;
        return SQL_SUCCESS;
    case SQL_ATTR_METADATA_ID:
        dbc->metadata_id = number == SQL_TRUE;
        return SQL_SUCCESS;
    case SQL_ATTR_TXN_ISOLATION:
        return number == SQL_TXN_SERIALIZABLE ? SQL_SUCCESS : changed(&dbc->diag);
    case SQL_ATTR_CONNECTION_TIMEOUT:
    case SQL_ATTR_PACKET_SIZE:
    case SQL_ATTR_QUIET_MODE:
    case SQL_ATTR_ASYNC_ENABLE:
    case SQL_ATTR_ODBC_CURSORS:
    case SQL_ATTR_TRACE:
    case SQL_ATTR_TRACEFILE:
    case SQL_ATTR_TRANSLATE_LIB:
    case SQL_ATTR_TRANSLATE_OPTION:
    case SQL_ATTR_CURRENT_CATALOG:
        /* The driver manager's own, or of what a server serving one
           database and one catalog has no other of. */
        return SQL_SUCCESS;
    default:
        return trl_odbc_fail(&dbc->diag, "HY092", "Invalid attribute/option identifier");
    }
}

/* A connection attribute that is a number; whether `attribute` is one. */
static bool connect_number(const trl_odbc_dbc *dbc, SQLINTEGER attribute, SQLUINTEGER *out) {
    switch (attribute) {
    case SQL_ATTR_AUTOCOMMIT:
        *out = dbc->autocommit ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF;
        return true;
    case SQL_ATTR_LOGIN_TIMEOUT:
        *out = dbc->login_timeout;
        return true;
    case SQL_ATTR_ACCESS_MODE:
        *out = dbc->read_only ? SQL_MODE_READ_ONLY : SQL_MODE_READ_WRITE;
        return true;
    case SQL_ATTR_TXN_ISOLATION:
        *out = SQL_TXN_SERIALIZABLE;
        return true;
    case SQL_ATTR_CONNECTION_DEAD:
        *out = dbc->conn == NULL || trl_conn_is_lost(dbc->conn) ? SQL_CD_TRUE : SQL_CD_FALSE;
        return true;
    case SQL_ATTR_METADATA_ID:
        *out = dbc->metadata_id ? SQL_TRUE : SQL_FALSE;
        return true;
    case SQL_ATTR_ASYNC_ENABLE:
        *out = SQL_ASYNC_ENABLE_OFF;
        return true;
    case SQL_ATTR_AUTO_IPD:
        *out = SQL_FALSE;
        return true;
    case SQL_ATTR_CONNECTION_TIMEOUT:
    case SQL_ATTR_PACKET_SIZE:
    case SQL_ATTR_QUIET_MODE:
        *out = 0U;
        return true;
    default:
        return false;
    }
}

/* SQLGetConnectAttr's work: a number into `out`'s buffer, text as it asks. */
SQLRETURN trl_odbc_get_connect_attr(trl_odbc_dbc *dbc, SQLINTEGER attribute, const trl_odbc_out *out) {
    trl_odbc_clear(&dbc->diag);
    SQLUINTEGER number = 0U;
    if (connect_number(dbc, attribute, &number)) {
        if (out->buffer != NULL) {
            *(SQLUINTEGER *)out->buffer = number;
        }
        if (out->length != NULL) {
            *out->length = (SQLINTEGER)sizeof number;
        }
        return SQL_SUCCESS;
    }
    if (attribute == SQL_ATTR_CURRENT_CATALOG) {
        const char *catalog = dbc->conn != NULL ? trl_conn_setting(dbc->conn, TRL_CATALOG_SETTING) : NULL;
        return trl_odbc_put(&dbc->diag, out, catalog != NULL ? catalog : "");
    }
    return trl_odbc_fail(&dbc->diag, "HY092", "Invalid attribute/option identifier");
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value,
                                                    SQLINTEGER length) {
    (void)length;
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    return dbc == NULL ? SQL_INVALID_HANDLE : trl_odbc_set_connect_attr(dbc, attribute, value);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value,
                                                    SQLINTEGER capacity, SQLINTEGER *length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_out out = {value, capacity, NULL, length, NULL, false, true};
    return trl_odbc_get_connect_attr(dbc, attribute, &out);
}

/* Sets the descriptor the application gives for `role`, or the implicit one for null. */
static SQLRETURN desc_use(trl_odbc_stmt *stmt, trl_odbc_role role, SQLPOINTER value) {
    trl_odbc_desc **in_use = role == TRL_ODBC_ARD ? &stmt->ard : &stmt->apd;
    if (value == SQL_NULL_HDESC) {
        *in_use = &stmt->implicit[role];
        return SQL_SUCCESS;
    }
    trl_odbc_desc *desc = trl_odbc_desc_of(value);
    if (desc == NULL || (desc->stmt != NULL && desc != &stmt->implicit[role]) || desc->dbc != stmt->dbc) {
        return trl_odbc_fail(&stmt->diag, "HY024", "Invalid attribute value");
    }
    *in_use = desc;
    return SQL_SUCCESS;
}

/* Sets an attribute of the rowset or the parameters; whether `attribute` is one. */
static bool binding_set(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLPOINTER value) {
    SQLULEN number = (SQLULEN)(uintptr_t)value;
    trl_odbc_desc *ird = &stmt->implicit[TRL_ODBC_IRD];
    trl_odbc_desc *ipd = &stmt->implicit[TRL_ODBC_IPD];
    switch (attribute) {
    case SQL_ATTR_ROW_ARRAY_SIZE:
    case SQL_ROWSET_SIZE:
        stmt->ard->array_size = number == 0U ? 1U : number;
        return true;
    case SQL_ATTR_ROW_BIND_TYPE:
        stmt->ard->bind_type = number;
        return true;
    case SQL_ATTR_ROW_BIND_OFFSET_PTR:
        stmt->ard->bind_offset = value;
        return true;
    case SQL_ATTR_ROW_STATUS_PTR:
        ird->array_status = value;
        return true;
    case SQL_ATTR_ROWS_FETCHED_PTR:
        ird->rows_processed = value;
        return true;
    case SQL_ATTR_PARAM_BIND_TYPE:
        stmt->apd->bind_type = number;
        return true;
    case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
        stmt->apd->bind_offset = value;
        return true;
    case SQL_ATTR_PARAM_STATUS_PTR:
        ipd->array_status = value;
        return true;
    case SQL_ATTR_PARAMS_PROCESSED_PTR:
        ipd->rows_processed = value;
        return true;
    case SQL_ATTR_PARAM_OPERATION_PTR:
        stmt->apd->array_status = value;
        return true;
    default:
        return false;
    }
}

/* Sets an attribute of how the statement runs; whether `attribute` is one. */
static bool running_set(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLULEN number) {
    switch (attribute) {
    case SQL_ATTR_MAX_ROWS:
        stmt->max_rows = number;
        return true;
    case SQL_ATTR_QUERY_TIMEOUT:
        stmt->query_timeout = number;
        return true;
    case SQL_ATTR_NOSCAN:
        stmt->noscan = number == SQL_NOSCAN_ON;
        return true;
    case SQL_ATTR_METADATA_ID:
        stmt->metadata_id = number == SQL_TRUE;
        return true;
    case SQL_ATTR_MAX_LENGTH:
    case SQL_ATTR_KEYSET_SIZE:
    case SQL_ATTR_ASYNC_ENABLE:
    case SQL_ATTR_RETRIEVE_DATA:
    case SQL_ATTR_SIMULATE_CURSOR:
    case SQL_ATTR_ENABLE_AUTO_IPD:
        /* Taken, of what the driver does one way alone. */
        return true;
    default:
        return false;
    }
}

/* Sets an attribute of the cursor; whether `attribute` is one, its result in `returned`. */
static bool cursor_set(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLULEN number, SQLRETURN *returned) {
    *returned = SQL_SUCCESS;
    switch (attribute) {
    case SQL_ATTR_CURSOR_TYPE:
        stmt->cursor_type = number == SQL_CURSOR_FORWARD_ONLY ? SQL_CURSOR_FORWARD_ONLY : SQL_CURSOR_STATIC;
        *returned = number == stmt->cursor_type ? SQL_SUCCESS : changed(&stmt->diag);
        return true;
    case SQL_ATTR_CURSOR_SCROLLABLE:
        stmt->cursor_type = number == SQL_SCROLLABLE ? SQL_CURSOR_STATIC : SQL_CURSOR_FORWARD_ONLY;
        return true;
    case SQL_ATTR_CONCURRENCY:
        *returned = number == SQL_CONCUR_READ_ONLY ? SQL_SUCCESS : changed(&stmt->diag);
        return true;
    case SQL_ATTR_CURSOR_SENSITIVITY:
        *returned = number == SQL_INSENSITIVE || number == SQL_UNSPECIFIED ? SQL_SUCCESS : changed(&stmt->diag);
        return true;
    case SQL_ATTR_USE_BOOKMARKS:
        *returned = number == SQL_UB_OFF ? SQL_SUCCESS : changed(&stmt->diag);
        return true;
    default:
        return false;
    }
}

/* SQLSetStmtAttr's work. */
SQLRETURN trl_odbc_set_stmt_attr(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLPOINTER value) {
    trl_odbc_clear(&stmt->diag);
    if (attribute == SQL_ATTR_APP_ROW_DESC || attribute == SQL_ATTR_APP_PARAM_DESC) {
        return desc_use(stmt, attribute == SQL_ATTR_APP_ROW_DESC ? TRL_ODBC_ARD : TRL_ODBC_APD, value);
    }
    if (attribute == SQL_ATTR_PARAMSET_SIZE && (SQLULEN)(uintptr_t)value > 1U) {
        return trl_odbc_fail(&stmt->diag, "HYC00", "Optional feature not implemented: arrays of parameters");
    }
    if (attribute == SQL_ATTR_PARAMSET_SIZE) {
        return SQL_SUCCESS;
    }
    if (attribute == SQL_ATTR_IMP_ROW_DESC || attribute == SQL_ATTR_IMP_PARAM_DESC) {
        return trl_odbc_fail(&stmt->diag, "HY017", "Invalid use of an automatically allocated descriptor handle");
    }
    if (binding_set(stmt, attribute, value) || running_set(stmt, attribute, (SQLULEN)(uintptr_t)value)) {
        return SQL_SUCCESS;
    }
    SQLRETURN returned = SQL_SUCCESS;
    if (cursor_set(stmt, attribute, (SQLULEN)(uintptr_t)value, &returned)) {
        return returned;
    }
    return trl_odbc_fail(&stmt->diag, "HY092", "Invalid attribute/option identifier");
}

/* A statement attribute that is a pointer; whether `attribute` is one. */
static bool stmt_pointer(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLPOINTER *out) {
    trl_odbc_desc *ird = &stmt->implicit[TRL_ODBC_IRD];
    trl_odbc_desc *ipd = &stmt->implicit[TRL_ODBC_IPD];
    switch (attribute) {
    case SQL_ATTR_APP_ROW_DESC:
        *out = stmt->ard;
        return true;
    case SQL_ATTR_APP_PARAM_DESC:
        *out = stmt->apd;
        return true;
    case SQL_ATTR_IMP_ROW_DESC:
        *out = ird;
        return true;
    case SQL_ATTR_IMP_PARAM_DESC:
        *out = ipd;
        return true;
    case SQL_ATTR_ROW_BIND_OFFSET_PTR:
        *out = stmt->ard->bind_offset;
        return true;
    case SQL_ATTR_ROW_STATUS_PTR:
        *out = ird->array_status;
        return true;
    case SQL_ATTR_ROWS_FETCHED_PTR:
        *out = ird->rows_processed;
        return true;
    case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
        *out = stmt->apd->bind_offset;
        return true;
    case SQL_ATTR_PARAM_STATUS_PTR:
        *out = ipd->array_status;
        return true;
    case SQL_ATTR_PARAMS_PROCESSED_PTR:
        *out = ipd->rows_processed;
        return true;
    case SQL_ATTR_PARAM_OPERATION_PTR:
        *out = stmt->apd->array_status;
        return true;
    default:
        return false;
    }
}

/* A number of the statement's rowset and parameters; whether `attribute` is one. */
static bool binding_number(const trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLULEN *out) {
    switch (attribute) {
    case SQL_ATTR_ROW_ARRAY_SIZE:
    case SQL_ROWSET_SIZE:
        *out = stmt->ard->array_size;
        return true;
    case SQL_ATTR_ROW_BIND_TYPE:
        *out = stmt->ard->bind_type;
        return true;
    case SQL_ATTR_PARAM_BIND_TYPE:
        *out = stmt->apd->bind_type;
        return true;
    case SQL_ATTR_ROW_NUMBER:
        *out = stmt->fetched && stmt->rowset_rows > 0U ? (SQLULEN)stmt->rowset + 1U : 0U;
        return true;
    case SQL_ATTR_MAX_ROWS:
        *out = stmt->max_rows;
        return true;
    case SQL_ATTR_QUERY_TIMEOUT:
        *out = stmt->query_timeout;
        return true;
    default:
        return false;
    }
}

/* A number of the statement's cursor and how it runs; whether `attribute` is one. */
static bool stmt_number(const trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLULEN *out) {
    if (binding_number(stmt, attribute, out)) {
        return true;
    }
    switch (attribute) {
    case SQL_ATTR_CURSOR_TYPE:
        *out = stmt->cursor_type;
        return true;
    case SQL_ATTR_CURSOR_SCROLLABLE:
        *out = stmt->cursor_type == SQL_CURSOR_STATIC ? SQL_SCROLLABLE : SQL_NONSCROLLABLE;
        return true;
    case SQL_ATTR_NOSCAN:
        *out = stmt->noscan ? SQL_NOSCAN_ON : SQL_NOSCAN_OFF;
        return true;
    case SQL_ATTR_METADATA_ID:
        *out = stmt->metadata_id ? SQL_TRUE : SQL_FALSE;
        return true;
    case SQL_ATTR_PARAMSET_SIZE:
    case SQL_ATTR_RETRIEVE_DATA:
    case SQL_ATTR_CONCURRENCY:
    case SQL_ATTR_CURSOR_SENSITIVITY:
        /* One set of parameters, data retrieved, read only and
           insensitive: the values of SQL_ATTR_PARAMSET_SIZE 1,
           SQL_RD_ON, SQL_CONCUR_READ_ONLY and SQL_INSENSITIVE, all 1. */
        *out = 1U;
        return true;
    case SQL_ATTR_USE_BOOKMARKS:
    case SQL_ATTR_MAX_LENGTH:
    case SQL_ATTR_KEYSET_SIZE:
    case SQL_ATTR_ASYNC_ENABLE:
    case SQL_ATTR_ENABLE_AUTO_IPD:
    case SQL_ATTR_SIMULATE_CURSOR:
        *out = 0U;
        return true;
    default:
        return false;
    }
}

/* SQLGetStmtAttr's work. */
SQLRETURN trl_odbc_get_stmt_attr(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER *length) {
    trl_odbc_clear(&stmt->diag);
    SQLPOINTER pointer = NULL;
    SQLULEN number = 0U;
    if (stmt_pointer(stmt, attribute, &pointer)) {
        if (value != NULL) {
            *(SQLPOINTER *)value = pointer;
        }
        if (length != NULL) {
            *length = (SQLINTEGER)sizeof pointer;
        }
        return SQL_SUCCESS;
    }
    if (stmt_number(stmt, attribute, &number)) {
        if (value != NULL) {
            *(SQLULEN *)value = number;
        }
        if (length != NULL) {
            *length = (SQLINTEGER)sizeof number;
        }
        return SQL_SUCCESS;
    }
    return trl_odbc_fail(&stmt->diag, "HY092", "Invalid attribute/option identifier");
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT handle, SQLINTEGER attribute, SQLPOINTER value,
                                                 SQLINTEGER length) {
    (void)length;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : trl_odbc_set_stmt_attr(stmt, attribute, value);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT handle, SQLINTEGER attribute, SQLPOINTER value,
                                                 SQLINTEGER capacity, SQLINTEGER *length) {
    (void)capacity;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : trl_odbc_get_stmt_attr(stmt, attribute, value, length);
}

/* Commits or rolls back `dbc`'s transaction, if one is begun under manual commit. */
static SQLRETURN ended(trl_odbc_dbc *dbc, SQLSMALLINT completion) {
    trl_odbc_clear(&dbc->diag);
    if (completion != SQL_COMMIT && completion != SQL_ROLLBACK) {
        return trl_odbc_fail(&dbc->diag, "HY012", "Invalid transaction operation code");
    }
    if (dbc->conn == NULL) {
        return trl_odbc_fail(&dbc->diag, "08003", "Connection not open");
    }
    if (dbc->autocommit) {
        return SQL_SUCCESS;
    }
    trl_result *result = NULL;
    trl_status ran = trl_query(dbc->conn, completion == SQL_COMMIT ? "COMMIT" : "ROLLBACK", &result);
    trl_result_free(result);
    return ran == TRL_OK ? SQL_SUCCESS : trl_odbc_failed(&dbc->diag, trl_conn_failure(dbc->conn));
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLEndTran(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT completion) {
    if (type == SQL_HANDLE_DBC) {
        trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
        return dbc == NULL ? SQL_INVALID_HANDLE : ended(dbc, completion);
    }
    trl_odbc_env *env = trl_odbc_env_of(handle);
    if (type != SQL_HANDLE_ENV || env == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&env->diag);
    SQLRETURN returned = SQL_SUCCESS;
    for (trl_odbc_dbc *dbc = env->connections; dbc != NULL; dbc = dbc->next) {
        if (dbc->conn != NULL && ended(dbc, completion) != SQL_SUCCESS) {
            returned = trl_odbc_fail(&env->diag, "25S01", "Transaction state unknown: a connection's failed");
        }
    }
    return returned;
}
