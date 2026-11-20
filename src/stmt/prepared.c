/**
 * @file prepared.c
 * @brief Prepared statements [CLIENT Prepared statements]: Prepare
 * [PROTOCOL Prepare 'P'], Execute [PROTOCOL Execute 'E'] of all rows or of
 * a cursor, Fetch [PROTOCOL Fetch 'F'], Close [PROTOCOL Close 'C'],
 * Describe [PROTOCOL Describe 'Y'] and Deallocate
 * [PROTOCOL Deallocate 'D'].
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "conn/conn.h"
#include "data/params.h"
#include "failure.h"
#include "memory.h"
#include "stmt/result.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct trl_statement {
    trl_arena arena;
    uint32_t number;
    const trl_parameter *parameters;
    uint32_t parameter_count;
    /* A result of its columns and no row, for their names and types. */
    trl_result *columns;
};

/* Reads Prepared into a new statement. */
static bool read_prepared(trl_conn *conn, trl_statement **out) {
    trl_statement *statement = trl_calloc(1U, sizeof *statement);
    trl_result *columns = trl_result_make();
    if (statement == NULL || columns == NULL) {
        free(statement);
        trl_result_free(columns);
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    trl_arena_init(&statement->arena);
    statement->columns = columns;
    conn->keep = &columns->arena;
    trl_reply reply;
    bool received = trl_conn_receive(conn, &reply);
    conn->keep = NULL;
    bool made = false;
    if (received && reply.kind == TRL_KIND_PREPARED) {
        statement->number = reply.as.prepared.statement;
        statement->parameters = reply.as.prepared.parameters;
        statement->parameter_count = reply.as.prepared.parameter_count;
        made = trl_result_take_columns(columns, reply.as.prepared.columns, reply.as.prepared.column_count);
        if (!made) {
            trl_failure_no_memory(&conn->failure);
        }
    } else if (received && reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
    } else if (received) {
        trl_conn_lose_by(conn, "the server sent no Prepared");
    }
    if (!made) {
        trl_statement_free(statement);
        return false;
    }
    *out = statement;
    return true;
}

trl_status trl_prepare(trl_conn *conn, const char *sql, trl_statement **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (conn == NULL || sql == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    uint32_t number = conn->next_statement++;
    trl_slice text = {(const uint8_t *)sql, strlen(sql)};
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_prepare(&conn->out, number, text, &error)) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    return trl_conn_send(conn) && read_prepared(conn, out) ? TRL_OK : TRL_FAILED;
}

/* Sends Execute of `statement`'s cursor `number`, of `rows` at most (0
   for all), ending its transaction if `ends`. */
static bool send_execute(trl_conn *conn, trl_cursor cursor, const trl_params *params, uint32_t rows, bool ends) {
    trl_execute_request execute;
    memset(&execute, 0, sizeof execute);
    execute.statement = cursor.statement->number;
    execute.cursor = cursor.number;
    execute.values = trl_params_values(params);
    execute.value_count = trl_params_count(params);
    execute.rows = rows;
    execute.ends = ends;
    execute.attribute_count = trl_conn_query_attributes(conn, &execute.attributes);
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_execute(&conn->out, &execute, &error)) {
        return trl_conn_codec_failed(conn, &error);
    }
    return trl_conn_send(conn);
}

/* Reads a statement's completion into `result`. */
static bool read_completion(trl_conn *conn, trl_result *result) {
    trl_reply reply;
    if (!trl_conn_receive(conn, &reply)) {
        return false;
    }
    if (reply.kind == TRL_KIND_COMPLETE) {
        trl_result_complete(result, &reply.as.complete);
        return true;
    }
    if (reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
        return false;
    }
    trl_conn_lose_by(conn, "the server sent no completion");
    return false;
}

/* Reads an Execute's or a Fetch's replies into a new result: its rows, of
   the statement's columns, or its completion of a statement of none. */
static trl_status read_result(trl_conn *conn, const trl_statement *statement, bool rows_alone, trl_result **out) {
    trl_result *result = trl_result_make();
    if (result == NULL) {
        trl_failure_no_memory(&conn->failure);
        return TRL_FAILED;
    }
    bool read = false;
    conn->keep = &result->arena;
    if (!rows_alone && statement->columns->column_count == 0U) {
        read = read_completion(conn, result);
    } else if (trl_result_set_columns(result, statement->columns->columns, statement->columns->column_count)) {
        read = trl_read_rows(conn, result);
    } else {
        trl_failure_no_memory(&conn->failure);
    }
    conn->keep = NULL;
    if (!read) {
        trl_result_free(result);
        return TRL_FAILED;
    }
    *out = result;
    return TRL_OK;
}

/* Whether the arguments of a cursor's call are whole. */
static bool cursor_given(const trl_conn *conn, trl_cursor cursor, trl_result **out) {
    if (out != NULL) {
        *out = NULL;
    }
    return conn != NULL && cursor.statement != NULL && out != NULL;
}

trl_status trl_execute(trl_conn *conn, const trl_statement *statement, const trl_params *params, trl_result **out) {
    trl_cursor cursor = {statement, 0U};
    if (!cursor_given(conn, cursor, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!send_execute(conn, cursor, params, 0U, conn->autocommit)) {
        return TRL_FAILED;
    }
    return read_result(conn, statement, false, out);
}

trl_status trl_cursor_open(trl_conn *conn, trl_cursor cursor, const trl_params *params, uint32_t rows,
                           trl_result **out) {
    if (!cursor_given(conn, cursor, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!trl_conn_needs(conn, cursor.number == 0U, TRL_CAPABILITY_CURSORS)) {
        return TRL_FAILED;
    }
    bool ends = conn->autocommit && trl_conn_granted(conn, TRL_CAPABILITY_FETCH_ENDS);
    if (!send_execute(conn, cursor, params, rows, ends)) {
        return TRL_FAILED;
    }
    return read_result(conn, cursor.statement, true, out);
}

trl_status trl_cursor_fetch(trl_conn *conn, trl_cursor cursor, uint32_t rows, trl_result **out) {
    if (!cursor_given(conn, cursor, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!trl_conn_needs(conn, cursor.number == 0U, TRL_CAPABILITY_CURSORS)) {
        return TRL_FAILED;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_fetch(&conn->out, cursor.statement->number, rows, cursor.number, &error)) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    return trl_conn_send(conn) ? read_result(conn, cursor.statement, true, out) : TRL_FAILED;
}

trl_status trl_cursor_close(trl_conn *conn, trl_cursor cursor) {
    if (conn == NULL || cursor.statement == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!trl_conn_needs(conn, cursor.number == 0U, TRL_CAPABILITY_CURSORS)) {
        return TRL_FAILED;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_close(&conn->out, cursor.statement->number, cursor.number, &error)) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    return trl_conn_send(conn) && trl_conn_completion(conn) ? TRL_OK : TRL_FAILED;
}

/* Sends Describe of a statement, `what`, or of one of its cursors. */
static bool send_describe(trl_conn *conn, uint8_t what, trl_cursor cursor) {
    if (!trl_conn_needs(conn, false, TRL_CAPABILITY_DESCRIBE)) {
        return false;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_describe(&conn->out, what, cursor.statement->number, cursor.number, &error)) {
        return trl_conn_codec_failed(conn, &error);
    }
    return trl_conn_send(conn);
}

trl_status trl_describe(trl_conn *conn, const trl_statement *statement, trl_statement **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || statement == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_cursor whole = {statement, 0U};
    return send_describe(conn, TRL_DESCRIBE_STATEMENT, whole) && read_prepared(conn, out) ? TRL_OK : TRL_FAILED;
}

/* Reads Description into a new result of its columns and no row. */
static trl_status read_description(trl_conn *conn, trl_result **out) {
    trl_result *result = trl_result_make();
    if (result == NULL) {
        trl_failure_no_memory(&conn->failure);
        return TRL_FAILED;
    }
    conn->keep = &result->arena;
    trl_reply reply;
    bool received = trl_conn_receive(conn, &reply);
    conn->keep = NULL;
    bool made = false;
    if (received && reply.kind == TRL_KIND_DESCRIPTION) {
        made = trl_result_take_columns(result, reply.as.description.columns, reply.as.description.count);
        if (!made) {
            trl_failure_no_memory(&conn->failure);
        }
    } else if (received && reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
    } else if (received) {
        trl_conn_lose_by(conn, "the server sent no Description");
    }
    if (!made) {
        trl_result_free(result);
        return TRL_FAILED;
    }
    *out = result;
    return TRL_OK;
}

trl_status trl_describe_cursor(trl_conn *conn, trl_cursor cursor, trl_result **out) {
    if (!cursor_given(conn, cursor, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    return send_describe(conn, TRL_DESCRIBE_CURSOR, cursor) ? read_description(conn, out) : TRL_FAILED;
}

trl_status trl_deallocate(trl_conn *conn, const trl_statement *statement) {
    if (conn == NULL || statement == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_deallocate(&conn->out, statement->number, &error)) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    return trl_conn_send(conn) && trl_conn_completion(conn) ? TRL_OK : TRL_FAILED;
}

size_t trl_statement_parameter_count(const trl_statement *statement) {
    return statement == NULL ? 0U : statement->parameter_count;
}

const trl_data_type *trl_statement_parameter_type(const trl_statement *statement, size_t parameter) {
    if (statement == NULL || parameter >= statement->parameter_count) {
        return NULL;
    }
    return &statement->parameters[parameter].type;
}

size_t trl_statement_column_count(const trl_statement *statement) {
    return statement == NULL ? 0U : trl_result_column_count(statement->columns);
}

const char *trl_statement_column_name(const trl_statement *statement, size_t column) {
    return statement == NULL ? NULL : trl_result_column_name(statement->columns, column);
}

const trl_data_type *trl_statement_column_type(const trl_statement *statement, size_t column) {
    return statement == NULL ? NULL : trl_result_column_type(statement->columns, column);
}

void trl_statement_free(trl_statement *statement) {
    if (statement == NULL) {
        return;
    }
    trl_result_free(statement->columns);
    trl_arena_free(&statement->arena);
    free(statement);
}
