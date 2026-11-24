/**
 * @file outputs.c
 * @brief A statement's parameters in and out (RULES.md D2): the IPD
 * filled as SQLPrepare prepares, each `?`'s type and mode; and each `?`
 * of an OUT or INOUT parameter the application bound as SQL_PARAM_OUTPUT
 * or SQL_PARAM_INPUT_OUTPUT given the value of its column of the CALL's
 * row, as Prepared's tail 20 places it [PROTOCOL Parameter modes].
 */
#include "odbc/outputs.h"

#include "odbc/convert.h"
#include "odbc/odbc.h"
#include "odbc/types.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Where `at` points, past the bind offset if one is set. */
static void *offset(void *at, const SQLLEN *by) {
    return at == NULL || by == NULL ? at : (char *)at + *by;
}

/* Whether parameter `number` was bound to be given a value back. */
static bool bound_out(const trl_odbc_stmt *stmt, SQLSMALLINT number) {
    const trl_odbc_desc *ipd = &stmt->implicit[TRL_ODBC_IPD];
    if (number > ipd->count || number > stmt->apd->count) {
        return false;
    }
    SQLSMALLINT io = ipd->fields[number].parameter_type;
    const trl_odbc_field *app = &stmt->apd->fields[number];
    return (io == SQL_PARAM_OUTPUT || io == SQL_PARAM_INPUT_OUTPUT) &&
           (app->data != NULL || app->indicator_ptr != NULL);
}

/* Gives column `column` of the CALL's row to parameter `number`'s buffers. */
static SQLRETURN one_given(trl_odbc_stmt *stmt, SQLSMALLINT number, size_t column) {
    const trl_odbc_field *app = &stmt->apd->fields[number];
    SQLSMALLINT c_type = app->concise_type;
    if (c_type == SQL_C_DEFAULT) {
        c_type = trl_odbc_default_c_type(stmt->implicit[TRL_ODBC_IPD].fields[number].concise_type);
    }
    trl_odbc_target target = {
        c_type,
        offset(app->data, stmt->apd->bind_offset),
        app->octet_length,
        offset(app->length_ptr, stmt->apd->bind_offset),
        offset(app->indicator_ptr, stmt->apd->bind_offset),
        app->scale,
        app->precision,
    };
    trl_odbc_piece piece = {
        trl_result_value(stmt->result, 0U, column), trl_result_text(stmt->result, 0U, column), NULL, 0U, false,
    };
    SQLRETURN given = trl_odbc_give(&stmt->diag, stmt->dbc->conn, &piece, &target);
    free(piece.owned);
    return given;
}

/* SQL_DESC_PARAMETER_TYPE of a `?`'s mode. */
static SQLSMALLINT io_of(trl_parameter_mode mode) {
    switch (mode) {
    case TRL_PARAMETER_MODE_INOUT:
        return SQL_PARAM_INPUT_OUTPUT;
    case TRL_PARAMETER_MODE_OUT:
        return SQL_PARAM_OUTPUT;
    case TRL_PARAMETER_MODE_NONE:
    case TRL_PARAMETER_MODE_IN:
    default:
        return SQL_PARAM_INPUT;
    }
}

SQLRETURN trl_odbc_ipd_filled(trl_odbc_stmt *stmt) {
    if (stmt->ipd_manual || stmt->prepared == NULL) {
        return SQL_SUCCESS;
    }
    trl_odbc_desc *ipd = &stmt->implicit[TRL_ODBC_IPD];
    size_t count = trl_statement_marker_count(stmt->prepared);
    for (size_t i = 0U; i < count; ++i) {
        trl_odbc_field *field = trl_odbc_desc_field(ipd, (SQLSMALLINT)(i + 1U));
        if (field == NULL) {
            return trl_odbc_no_memory(&stmt->diag);
        }
        trl_odbc_type t;
        trl_odbc_type_of(trl_statement_marker_type(stmt->prepared, i), NULL, &t);
        trl_odbc_field_type(field, t.sql_type);
        field->length = t.column_size;
        field->precision = (SQLSMALLINT)(t.column_size > INT16_MAX ? INT16_MAX : t.column_size);
        field->scale = t.digits;
        field->parameter_type = io_of(trl_statement_marker_mode(stmt->prepared, i));
    }
    return SQL_SUCCESS;
}

SQLRETURN trl_odbc_outputs_given(trl_odbc_stmt *stmt, const trl_statement *statement) {
    if (stmt->result == NULL || trl_result_row_count(stmt->result) == 0U) {
        return SQL_SUCCESS;
    }
    SQLRETURN returned = SQL_SUCCESS;
    bool any = false;
    size_t count = trl_statement_marker_count(statement);
    for (size_t i = 0U; i < count && returned != SQL_ERROR; ++i) {
        size_t column = 0U;
        SQLSMALLINT number = (SQLSMALLINT)(i + 1U);
        if (!trl_statement_marker_column(statement, i, &column) || !bound_out(stmt, number)) {
            continue;
        }
        any = true;
        SQLRETURN given = one_given(stmt, number, column);
        returned = given == SQL_SUCCESS ? returned : given;
    }
    if (any) {
        /* Its row given to the parameters, the CALL has no result set. */
        trl_odbc_close_cursor(stmt);
    }
    return returned;
}
