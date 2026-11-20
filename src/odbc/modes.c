/**
 * @file modes.c
 * @brief A connection's transaction modes and a statement's timeout
 * (RULES.md D2), as JDBC's and ADO.NET's drivers give them: the isolation
 * level, of ODBC's four, and the access mode, set by SET TRANSACTION as
 * each transaction begins, which lasts for that transaction alone; and
 * SQL_ATTR_QUERY_TIMEOUT, the session's STATEMENT.TIMEOUT_MS, so the
 * server cancels a statement that runs out of it.
 */
#include "format.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The session's setting of the isolation of a transaction SET TRANSACTION
   gives none, and of a statement's time. */
#define TRL_ISOLATION_SETTING "TRANSACTION.ISOLATION"
/* The milliseconds of a second, and the room of a statement setting them. */
#define TRL_MS_SECOND 1000U
#define TRL_SET_BYTES 96U
/* The SQLCODE of a statement cancelled, by SQLCancel or by its timeout,
   and what the server's message of the timeout's names. */
#define TRL_CANCELLED_SQLCODE (-920)
#define TRL_TIMEOUT_NAMED "STATEMENT.TIMEOUT_MS"

/* An isolation level: ODBC's number and SQL's name. */
typedef struct level {
    SQLUINTEGER number;
    const char *name;
} level;

static const level levels[] = {
    {SQL_TXN_READ_UNCOMMITTED, "READ UNCOMMITTED"},
    {SQL_TXN_READ_COMMITTED, "READ COMMITTED"},
    {SQL_TXN_REPEATABLE_READ, "REPEATABLE READ"},
    {SQL_TXN_SERIALIZABLE, "SERIALIZABLE"},
};

/* The level of ODBC's number `number`, or null. */
static const level *level_of(SQLULEN number) {
    for (size_t i = 0U; i < sizeof levels / sizeof levels[0]; ++i) {
        if (levels[i].number == number) {
            return &levels[i];
        }
    }
    return NULL;
}

SQLRETURN trl_odbc_isolation_set(trl_odbc_dbc *dbc, SQLULEN number) {
    if (level_of(number) == NULL) {
        return trl_odbc_fail(&dbc->diag, "HY024", "Invalid attribute value");
    }
    /* ODBC asks a transaction be ended first, as SET TRANSACTION would refuse. */
    if (dbc->conn != NULL && trl_conn_in_transaction(dbc->conn)) {
        return trl_odbc_fail(&dbc->diag, "HY011", "Attribute cannot be set now: a transaction is open");
    }
    dbc->isolation = (SQLUINTEGER)number;
    return SQL_SUCCESS;
}

SQLUINTEGER trl_odbc_isolation(const trl_odbc_dbc *dbc) {
    if (dbc->isolation != 0U) {
        return dbc->isolation;
    }
    const char *told = dbc->conn != NULL ? trl_conn_setting(dbc->conn, TRL_ISOLATION_SETTING) : NULL;
    for (size_t i = 0U; told != NULL && i < sizeof levels / sizeof levels[0]; ++i) {
        if (strcmp(told, levels[i].name) == 0) {
            return levels[i].number;
        }
    }
    return SQL_TXN_READ_COMMITTED;
}

/* Runs `sql`, a statement that begins no transaction, its result let go;
   not ended with a transaction, so what it sets is the next one's. */
static SQLRETURN ran(trl_odbc_stmt *stmt, const char *sql) {
    trl_conn *conn = stmt->dbc->conn;
    bool autocommit = trl_conn_autocommit(conn);
    (void)trl_conn_set_autocommit(conn, false);
    trl_result *result = NULL;
    trl_status said = trl_query(conn, sql, &result);
    (void)trl_conn_set_autocommit(conn, autocommit);
    trl_result_free(result);
    if (said == TRL_NO_MEMORY) {
        return trl_odbc_no_memory(&stmt->diag);
    }
    return said == TRL_OK ? SQL_SUCCESS : trl_odbc_failed(&stmt->diag, trl_conn_failure(conn));
}

/* The session's STATEMENT.TIMEOUT_MS as the statement's timeout says. */
static SQLRETURN timeout_applied(trl_odbc_stmt *stmt) {
    trl_odbc_dbc *dbc = stmt->dbc;
    uint64_t ms = (uint64_t)stmt->query_timeout * TRL_MS_SECOND;
    if (ms == dbc->timeout_ms) {
        return SQL_SUCCESS;
    }
    char sql[TRL_SET_BYTES];
    trl_format(sql, sizeof sql, "SET STATEMENT.TIMEOUT_MS = %llu", (unsigned long long)ms);
    SQLRETURN set = ran(stmt, sql);
    if (set == SQL_SUCCESS) {
        dbc->timeout_ms = ms;
    }
    return set;
}

SQLRETURN trl_odbc_modes_applied(trl_odbc_stmt *stmt) {
    trl_odbc_dbc *dbc = stmt->dbc;
    SQLRETURN timed = timeout_applied(stmt);
    if (timed != SQL_SUCCESS || (dbc->isolation == 0U && !dbc->read_only) || trl_conn_in_transaction(dbc->conn)) {
        return timed;
    }
    const level *chosen = level_of(dbc->isolation);
    char sql[TRL_SET_BYTES];
    trl_format(sql, sizeof sql, "SET TRANSACTION %s%s", chosen != NULL ? "ISOLATION LEVEL " : "",
               chosen != NULL ? chosen->name : "");
    if (dbc->read_only) {
        size_t at = strlen(sql);
        trl_format(sql + at, sizeof sql - at, "%s", chosen != NULL ? ", READ ONLY" : "READ ONLY");
    }
    return ran(stmt, sql);
}

SQLRETURN trl_odbc_run_failed(trl_odbc_stmt *stmt) {
    const trl_failure *failure = trl_conn_failure(stmt->dbc->conn);
    /* A statement SQLCancel stopped fails as cancelled; one its timeout
       stopped, the server says, ran past STATEMENT.TIMEOUT_MS. */
    bool timed_out = trl_failure_sqlcode(failure) == TRL_CANCELLED_SQLCODE &&
                     strstr(trl_failure_message(failure), TRL_TIMEOUT_NAMED) != NULL;
    if (stmt->query_timeout > 0U && timed_out) {
        return trl_odbc_fail(&stmt->diag, "HYT00", "Timeout expired: the statement ran past its query timeout");
    }
    return trl_odbc_failed(&stmt->diag, failure);
}
