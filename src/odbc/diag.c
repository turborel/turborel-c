/**
 * @file diag.c
 * @brief Each handle's diagnostics (RULES.md D2): the records the last
 * function called on it made, each failure's SQLSTATE, its SQLCODE as the
 * native error, and its message after the driver's prefix; read by
 * SQLGetDiagRec and SQLGetDiagField.
 */
#include "format.h"
#include "memory.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* What each message begins with: the vendor and the component, as
   ODBC's reference asks. */
#define TRL_ODBC_PREFIX "[Turborel][libturborelodbc]"
/* Room for a message. */
#define TRL_ODBC_MESSAGE_BYTES 1024U
/* The origins a SQLSTATE's class and subclass may have. */
#define TRL_ODBC_ISO "ISO 9075"
#define TRL_ODBC_ODBC "ODBC 3.0"
/* The characters of a SQLSTATE's class. */
#define TRL_CLASS_BYTES 2U
/* Where in a SQLSTATE an ODBC subclass has its S. */
#define TRL_SUBCLASS_AT 2U

void trl_odbc_clear(trl_odbc_diag *diag) {
    for (size_t i = 0U; i < diag->count; ++i) {
        free(diag->records[i].message);
    }
    free(diag->records);
    diag->records = NULL;
    diag->count = 0U;
    diag->returned = SQL_SUCCESS;
}

bool trl_odbc_record_add(trl_odbc_diag *diag, const char *sqlstate, SQLINTEGER native, const char *message) {
    trl_odbc_record *grown = trl_realloc(diag->records, (diag->count + 1U) * sizeof *grown);
    if (grown == NULL) {
        return false;
    }
    diag->records = grown;
    char text[TRL_ODBC_MESSAGE_BYTES];
    trl_format(text, sizeof text, "%s %s", TRL_ODBC_PREFIX, message);
    size_t length = strlen(text) + 1U;
    char *copy = trl_malloc(length);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, text, length);
    trl_odbc_record *record = &diag->records[diag->count++];
    trl_format(record->sqlstate, sizeof record->sqlstate, "%s", sqlstate);
    record->native = native;
    record->message = copy;
    return true;
}

SQLRETURN trl_odbc_fail(trl_odbc_diag *diag, const char *sqlstate, const char *message) {
    (void)trl_odbc_record_add(diag, sqlstate, 0, message);
    return trl_odbc_return(diag, SQL_ERROR);
}

SQLRETURN trl_odbc_warn(trl_odbc_diag *diag, const char *sqlstate, const char *message) {
    (void)trl_odbc_record_add(diag, sqlstate, 0, message);
    return trl_odbc_return(diag, SQL_SUCCESS_WITH_INFO);
}

SQLRETURN trl_odbc_failed(trl_odbc_diag *diag, const trl_failure *failure) {
    if (failure == NULL || trl_failure_sqlcode(failure) == 0) {
        return trl_odbc_fail(diag, "HY000", "the statement failed");
    }
    (void)trl_odbc_record_add(diag, trl_failure_sqlstate(failure), trl_failure_sqlcode(failure),
                              trl_failure_message(failure));
    return trl_odbc_return(diag, SQL_ERROR);
}

SQLRETURN trl_odbc_no_memory(trl_odbc_diag *diag) {
    return trl_odbc_fail(diag, "HY001", "Memory allocation error");
}

SQLRETURN trl_odbc_return(trl_odbc_diag *diag, SQLRETURN returned) {
    bool worse = returned == SQL_ERROR || diag->returned == SQL_SUCCESS;
    diag->returned = worse ? returned : diag->returned;
    return returned;
}

SQLRETURN trl_odbc_warned(trl_odbc_diag *diag, const trl_conn *conn) {
    const char *state = trl_conn_warning(conn);
    if (state == NULL) {
        return SQL_SUCCESS;
    }
    return trl_odbc_warn(diag, state, "the statement completed with a warning");
}

trl_odbc_diag *trl_odbc_diag_of(SQLSMALLINT type, SQLHANDLE handle) {
    switch (type) {
    case SQL_HANDLE_ENV: {
        trl_odbc_env *env = trl_odbc_env_of(handle);
        return env != NULL ? &env->diag : NULL;
    }
    case SQL_HANDLE_DBC: {
        trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
        return dbc != NULL ? &dbc->diag : NULL;
    }
    case SQL_HANDLE_STMT: {
        trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
        return stmt != NULL ? &stmt->diag : NULL;
    }
    case SQL_HANDLE_DESC: {
        trl_odbc_desc *desc = trl_odbc_desc_of(handle);
        return desc != NULL ? &desc->diag : NULL;
    }
    default:
        return NULL;
    }
}

/* Where a SQLSTATE's class is defined: by SQL, or by ODBC. */
static const char *class_origin(const char *sqlstate) {
    bool odbc = strncmp(sqlstate, "HY", TRL_CLASS_BYTES) == 0 || strncmp(sqlstate, "IM", TRL_CLASS_BYTES) == 0;
    return odbc ? TRL_ODBC_ODBC : TRL_ODBC_ISO;
}

/* Where its subclass is: ODBC's own end in S and digits, as 01S02. */
static const char *subclass_origin(const char *sqlstate) {
    bool odbc = sqlstate[TRL_SUBCLASS_AT] == 'S' || strcmp(class_origin(sqlstate), TRL_ODBC_ODBC) == 0;
    return odbc ? TRL_ODBC_ODBC : TRL_ODBC_ISO;
}

/* The data source's name of the handle's connection, or "". */
static const char *server_of(SQLSMALLINT type, SQLHANDLE handle) {
    const trl_odbc_dbc *dbc = NULL;
    if (type == SQL_HANDLE_DBC) {
        dbc = trl_odbc_dbc_of(handle);
    } else if (type == SQL_HANDLE_STMT) {
        dbc = trl_odbc_stmt_of(handle)->dbc;
    } else if (type == SQL_HANDLE_DESC) {
        dbc = trl_odbc_desc_of(handle)->dbc;
    }
    return dbc != NULL && dbc->dsn != NULL ? dbc->dsn : "";
}

/* A header field of the diagnostics; SQL_ERROR if `field` is none. */
static SQLRETURN header_field(const trl_odbc_diag *diag, SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT field,
                              SQLPOINTER info) {
    switch (field) {
    case SQL_DIAG_NUMBER:
        *(SQLINTEGER *)info = (SQLINTEGER)diag->count;
        return SQL_SUCCESS;
    case SQL_DIAG_RETURNCODE:
        *(SQLRETURN *)info = diag->returned;
        return SQL_SUCCESS;
    case SQL_DIAG_ROW_COUNT:
    case SQL_DIAG_CURSOR_ROW_COUNT: {
        if (type != SQL_HANDLE_STMT) {
            return SQL_ERROR;
        }
        const trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
        bool rows = field == SQL_DIAG_CURSOR_ROW_COUNT && stmt->result != NULL;
        *(SQLLEN *)info = rows ? (SQLLEN)trl_result_row_count(stmt->result) : stmt->row_count;
        return SQL_SUCCESS;
    }
    case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
        *(SQLINTEGER *)info = SQL_DIAG_UNKNOWN_STATEMENT;
        return type == SQL_HANDLE_STMT ? SQL_SUCCESS : SQL_ERROR;
    default:
        return SQL_ERROR;
    }
}

/* A record's field that is text; null if `field` is none of them. */
static const char *record_text(const trl_odbc_record *record, SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT field) {
    switch (field) {
    case SQL_DIAG_SQLSTATE:
        return record->sqlstate;
    case SQL_DIAG_MESSAGE_TEXT:
        return record->message;
    case SQL_DIAG_CLASS_ORIGIN:
        return class_origin(record->sqlstate);
    case SQL_DIAG_SUBCLASS_ORIGIN:
        return subclass_origin(record->sqlstate);
    case SQL_DIAG_CONNECTION_NAME:
        return "";
    case SQL_DIAG_SERVER_NAME:
        return server_of(type, handle);
    default:
        return NULL;
    }
}

SQLRETURN trl_odbc_diag_field(const trl_odbc_out *out, SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number,
                              SQLSMALLINT field) {
    trl_odbc_diag *diag = trl_odbc_diag_of(type, handle);
    if (diag == NULL) {
        return SQL_INVALID_HANDLE;
    }
    if (field == SQL_DIAG_DYNAMIC_FUNCTION) {
        return trl_odbc_put(NULL, out, "");
    }
    if (header_field(diag, type, handle, field, out->buffer) == SQL_SUCCESS) {
        return SQL_SUCCESS;
    }
    if (number < 1) {
        return SQL_ERROR;
    }
    if ((size_t)number > diag->count) {
        return SQL_NO_DATA;
    }
    const trl_odbc_record *record = &diag->records[number - 1];
    const char *text = record_text(record, type, handle, field);
    if (text != NULL) {
        return trl_odbc_put(NULL, out, text);
    }
    if (field == SQL_DIAG_NATIVE) {
        *(SQLINTEGER *)out->buffer = record->native;
    } else if (field == SQL_DIAG_COLUMN_NUMBER) {
        *(SQLINTEGER *)out->buffer = SQL_COLUMN_NUMBER_UNKNOWN;
    } else if (field == SQL_DIAG_ROW_NUMBER) {
        *(SQLLEN *)out->buffer = SQL_ROW_NUMBER_UNKNOWN;
    } else {
        return SQL_ERROR;
    }
    return SQL_SUCCESS;
}

SQLRETURN trl_odbc_diag_rec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, const trl_odbc_rec_out *out) {
    const trl_odbc_diag *diag = trl_odbc_diag_of(type, handle);
    if (diag == NULL) {
        return SQL_INVALID_HANDLE;
    }
    if (number < 1 || out->message.capacity < 0) {
        return SQL_ERROR;
    }
    if ((size_t)number > diag->count) {
        return SQL_NO_DATA;
    }
    const trl_odbc_record *record = &diag->records[number - 1];
    if (out->state != NULL) {
        trl_odbc_out state = {out->state, TRL_ODBC_STATE_BYTES, NULL, NULL, NULL, out->message.wide, false};
        (void)trl_odbc_put(NULL, &state, record->sqlstate);
    }
    if (out->native != NULL) {
        *out->native = record->native;
    }
    return trl_odbc_put(NULL, &out->message, record->message);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, SQLCHAR *state,
                                                SQLINTEGER *native, SQLCHAR *message, SQLSMALLINT capacity,
                                                SQLSMALLINT *length) {
    trl_odbc_rec_out out = {state, native, {message, capacity, length, NULL, NULL, false, false}};
    return trl_odbc_diag_rec(type, handle, number, &out);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetDiagField(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number,
                                                  SQLSMALLINT field, SQLPOINTER info, SQLSMALLINT capacity,
                                                  SQLSMALLINT *length) {
    trl_odbc_out out = {info, capacity, length, NULL, NULL, false, true};
    return trl_odbc_diag_field(&out, type, handle, number, field);
}
