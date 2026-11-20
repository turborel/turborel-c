/**
 * @file handles.c
 * @brief The driver's handles made and freed (RULES.md D2): an
 * environment, a connection of it, a statement of a connected connection
 * with its four implicit descriptors, and a descriptor the application
 * allocates; each handle's first member says what it is, so a handle of
 * another kind is refused as invalid.
 */
#include "memory.h"
#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

trl_odbc_env *trl_odbc_env_of(SQLHANDLE handle) {
    trl_odbc_env *env = handle;
    return env != NULL && env->kind == SQL_HANDLE_ENV ? env : NULL;
}

trl_odbc_dbc *trl_odbc_dbc_of(SQLHANDLE handle) {
    trl_odbc_dbc *dbc = handle;
    return dbc != NULL && dbc->kind == SQL_HANDLE_DBC ? dbc : NULL;
}

trl_odbc_stmt *trl_odbc_stmt_of(SQLHANDLE handle) {
    trl_odbc_stmt *stmt = handle;
    return stmt != NULL && stmt->kind == SQL_HANDLE_STMT ? stmt : NULL;
}

trl_odbc_desc *trl_odbc_desc_of(SQLHANDLE handle) {
    trl_odbc_desc *desc = handle;
    return desc != NULL && desc->kind == SQL_HANDLE_DESC ? desc : NULL;
}

/* A new environment, its ODBC version not yet set. */
static SQLRETURN env_new(SQLHANDLE *out) {
    trl_odbc_env *env = trl_calloc(1U, sizeof *env);
    if (env == NULL) {
        return SQL_ERROR;
    }
    env->kind = SQL_HANDLE_ENV;
    *out = env;
    return SQL_SUCCESS;
}

/* A new connection of `env`, not connected; autocommit on, as ODBC's default. */
static SQLRETURN dbc_new(trl_odbc_env *env, SQLHANDLE *out) {
    trl_odbc_clear(&env->diag);
    if (env->version == 0) {
        return trl_odbc_fail(&env->diag, "HY010", "Function sequence error: SQL_ATTR_ODBC_VERSION is not set");
    }
    trl_odbc_dbc *dbc = trl_calloc(1U, sizeof *dbc);
    if (dbc == NULL) {
        return trl_odbc_no_memory(&env->diag);
    }
    dbc->kind = SQL_HANDLE_DBC;
    dbc->env = env;
    dbc->autocommit = true;
    dbc->next = env->connections;
    env->connections = dbc;
    *out = dbc;
    return SQL_SUCCESS;
}

/* A new statement of a connected `dbc`. */
static SQLRETURN stmt_new(trl_odbc_dbc *dbc, SQLHANDLE *out) {
    trl_odbc_clear(&dbc->diag);
    if (dbc->conn == NULL) {
        return trl_odbc_fail(&dbc->diag, "08003", "Connection not open");
    }
    trl_odbc_stmt *stmt = trl_calloc(1U, sizeof *stmt);
    if (stmt == NULL) {
        return trl_odbc_no_memory(&dbc->diag);
    }
    stmt->kind = SQL_HANDLE_STMT;
    stmt->dbc = dbc;
    for (int role = TRL_ODBC_ARD; role <= TRL_ODBC_IPD; ++role) {
        trl_odbc_desc_init(&stmt->implicit[role], dbc, stmt, (trl_odbc_role)role);
    }
    stmt->ard = &stmt->implicit[TRL_ODBC_ARD];
    stmt->apd = &stmt->implicit[TRL_ODBC_APD];
    stmt->row_count = -1;
    stmt->cursor_type = SQL_CURSOR_FORWARD_ONLY;
    stmt->metadata_id = dbc->metadata_id;
    stmt->next = dbc->statements;
    dbc->statements = stmt;
    *out = stmt;
    return SQL_SUCCESS;
}

/* A new descriptor the application allocates on `dbc`, to bind columns or parameters by. */
static SQLRETURN desc_new(trl_odbc_dbc *dbc, SQLHANDLE *out) {
    trl_odbc_clear(&dbc->diag);
    if (dbc->conn == NULL) {
        return trl_odbc_fail(&dbc->diag, "08003", "Connection not open");
    }
    trl_odbc_desc *desc = trl_calloc(1U, sizeof *desc);
    if (desc == NULL) {
        return trl_odbc_no_memory(&dbc->diag);
    }
    trl_odbc_desc_init(desc, dbc, NULL, TRL_ODBC_ARD);
    desc->next = dbc->descriptors;
    dbc->descriptors = desc;
    *out = desc;
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output) {
    if (output == NULL) {
        return SQL_ERROR;
    }
    *output = SQL_NULL_HANDLE;
    switch (type) {
    case SQL_HANDLE_ENV:
        return env_new(output);
    case SQL_HANDLE_DBC:
        return trl_odbc_env_of(input) == NULL ? SQL_INVALID_HANDLE : dbc_new(input, output);
    case SQL_HANDLE_STMT:
        return trl_odbc_dbc_of(input) == NULL ? SQL_INVALID_HANDLE : stmt_new(input, output);
    case SQL_HANDLE_DESC:
        return trl_odbc_dbc_of(input) == NULL ? SQL_INVALID_HANDLE : desc_new(input, output);
    default:
        return SQL_ERROR;
    }
}

/* Takes `stmt` off its connection's list. */
static void unlisted(trl_odbc_stmt *stmt) {
    for (trl_odbc_stmt **at = &stmt->dbc->statements; *at != NULL; at = &(*at)->next) {
        if (*at == stmt) {
            *at = stmt->next;
            return;
        }
    }
}

/* Frees a statement: its cursor closed, its prepared statement destroyed. */
static void stmt_free(trl_odbc_stmt *stmt) {
    trl_odbc_close_cursor(stmt);
    trl_odbc_unprepare(stmt);
    unlisted(stmt);
    for (int role = TRL_ODBC_ARD; role <= TRL_ODBC_IPD; ++role) {
        trl_odbc_desc_destroy(&stmt->implicit[role]);
    }
    trl_odbc_clear(&stmt->diag);
    free(stmt->cursor_name);
    stmt->kind = 0;
    free(stmt);
}

/* Frees a descriptor the application allocated: each statement using it
   uses its own again. */
static void desc_free(trl_odbc_desc *desc) {
    trl_odbc_dbc *dbc = desc->dbc;
    for (trl_odbc_stmt *stmt = dbc->statements; stmt != NULL; stmt = stmt->next) {
        stmt->ard = stmt->ard == desc ? &stmt->implicit[TRL_ODBC_ARD] : stmt->ard;
        stmt->apd = stmt->apd == desc ? &stmt->implicit[TRL_ODBC_APD] : stmt->apd;
    }
    for (trl_odbc_desc **at = &dbc->descriptors; *at != NULL; at = &(*at)->next) {
        if (*at == desc) {
            *at = desc->next;
            break;
        }
    }
    trl_odbc_desc_destroy(desc);
    desc->kind = 0;
    free(desc);
}

/* Frees a connection not connected. */
static SQLRETURN dbc_free(trl_odbc_dbc *dbc) {
    if (dbc->conn != NULL) {
        return trl_odbc_fail(&dbc->diag, "HY010", "Function sequence error: the connection is open");
    }
    for (trl_odbc_dbc **at = &dbc->env->connections; *at != NULL; at = &(*at)->next) {
        if (*at == dbc) {
            *at = dbc->next;
            break;
        }
    }
    trl_odbc_clear(&dbc->diag);
    free(dbc->dsn);
    dbc->kind = 0;
    free(dbc);
    return SQL_SUCCESS;
}

/* Frees an environment with no connection. */
static SQLRETURN env_free(trl_odbc_env *env) {
    if (env->connections != NULL) {
        return trl_odbc_fail(&env->diag, "HY010", "Function sequence error: a connection is allocated");
    }
    trl_odbc_clear(&env->diag);
    env->kind = 0;
    free(env);
    return SQL_SUCCESS;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE handle) {
    switch (type) {
    case SQL_HANDLE_ENV:
        return trl_odbc_env_of(handle) == NULL ? SQL_INVALID_HANDLE : env_free(handle);
    case SQL_HANDLE_DBC:
        return trl_odbc_dbc_of(handle) == NULL ? SQL_INVALID_HANDLE : dbc_free(handle);
    case SQL_HANDLE_STMT:
        if (trl_odbc_stmt_of(handle) == NULL) {
            return SQL_INVALID_HANDLE;
        }
        stmt_free(handle);
        return SQL_SUCCESS;
    case SQL_HANDLE_DESC: {
        trl_odbc_desc *desc = trl_odbc_desc_of(handle);
        if (desc == NULL || desc->stmt != NULL) {
            return desc == NULL ? SQL_INVALID_HANDLE
                                : trl_odbc_fail(&desc->diag, "HY017",
                                                "Invalid use of an "
                                                "automatically allocated "
                                                "descriptor handle");
        }
        desc_free(desc);
        return SQL_SUCCESS;
    }
    default:
        return SQL_ERROR;
    }
}

/* Frees every statement and descriptor of `dbc`, as disconnecting does. */
void trl_odbc_free_children(trl_odbc_dbc *dbc) {
    trl_odbc_stmt *next_stmt = NULL;
    for (trl_odbc_stmt *stmt = dbc->statements; stmt != NULL; stmt = next_stmt) {
        next_stmt = stmt->next;
        stmt_free(stmt);
    }
    trl_odbc_desc *next_desc = NULL;
    for (trl_odbc_desc *desc = dbc->descriptors; desc != NULL; desc = next_desc) {
        next_desc = desc->next;
        desc_free(desc);
    }
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT handle, SQLUSMALLINT option) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    switch (option) {
    case SQL_CLOSE:
        trl_odbc_close_cursor(stmt);
        return SQL_SUCCESS;
    case SQL_DROP:
        stmt_free(stmt);
        return SQL_SUCCESS;
    case SQL_UNBIND:
        stmt->ard->count = 0;
        return SQL_SUCCESS;
    case SQL_RESET_PARAMS:
        stmt->apd->count = 0;
        stmt->implicit[TRL_ODBC_IPD].count = 0;
        return SQL_SUCCESS;
    default:
        return trl_odbc_fail(&stmt->diag, "HY092", "Invalid attribute/option identifier");
    }
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV handle, SQLINTEGER attribute, SQLPOINTER value,
                                                SQLINTEGER length) {
    (void)length;
    trl_odbc_env *env = trl_odbc_env_of(handle);
    if (env == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&env->diag);
    SQLINTEGER asked = (SQLINTEGER)(intptr_t)value;
    switch (attribute) {
    case SQL_ATTR_ODBC_VERSION:
        if (asked != SQL_OV_ODBC3 && asked != SQL_OV_ODBC2 && asked != SQL_OV_ODBC3_80) {
            return trl_odbc_fail(&env->diag, "HY024", "Invalid attribute value");
        }
        env->version = asked;
        return SQL_SUCCESS;
    case SQL_ATTR_OUTPUT_NTS:
        return asked == SQL_TRUE ? SQL_SUCCESS : trl_odbc_fail(&env->diag, "HYC00", "Optional feature not implemented");
    case SQL_ATTR_CONNECTION_POOLING:
    case SQL_ATTR_CP_MATCH:
        /* The driver manager pools; the driver keeps no pool of its own. */
        return SQL_SUCCESS;
    default:
        return trl_odbc_fail(&env->diag, "HY092", "Invalid attribute/option identifier");
    }
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetEnvAttr(SQLHENV handle, SQLINTEGER attribute, SQLPOINTER value,
                                                SQLINTEGER capacity, SQLINTEGER *length) {
    (void)capacity;
    trl_odbc_env *env = trl_odbc_env_of(handle);
    if (env == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&env->diag);
    SQLINTEGER said = 0;
    switch (attribute) {
    case SQL_ATTR_ODBC_VERSION:
        said = env->version;
        break;
    case SQL_ATTR_OUTPUT_NTS:
        said = SQL_TRUE;
        break;
    case SQL_ATTR_CONNECTION_POOLING:
    case SQL_ATTR_CP_MATCH:
        /* SQL_CP_OFF and SQL_CP_STRICT_MATCH, both 0: the driver keeps no pool. */
        said = 0;
        break;
    default:
        return trl_odbc_fail(&env->diag, "HY092", "Invalid attribute/option identifier");
    }
    if (value != NULL) {
        *(SQLINTEGER *)value = said;
    }
    if (length != NULL) {
        *length = (SQLINTEGER)sizeof said;
    }
    return SQL_SUCCESS;
}
