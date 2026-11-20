/**
 * @file result.c
 * @brief Results [CLIENT Running statements]: a query's columns and rows,
 * a statement's completion, read back by their functions.
 */
#include "stmt/result.h"

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/fields.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "conn/conn.h"
#include "data/params.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The rows a result's list holds before it grows. */
#define TRL_ROWS_FIRST 64U
/* How much the row list grows each time it is full. */
#define TRL_ROWS_GROWTH 2U

trl_result *trl_result_make(void) {
    trl_result *result = trl_calloc(1U, sizeof *result);
    if (result != NULL) {
        trl_arena_init(&result->arena);
    }
    return result;
}

bool trl_result_take_columns(trl_result *result, const trl_column *columns, uint32_t count) {
    result->has_rows = true;
    result->columns = columns;
    result->column_count = count;
    result->names = (char **)trl_arena_alloc(&result->arena, count == 0U ? 1U : count, sizeof(char *));
    for (uint32_t i = 0U; result->names != NULL && i < count; ++i) {
        result->names[i] = NULL;
        if (!columns[i].named) {
            continue;
        }
        char *name = trl_arena_alloc(&result->arena, columns[i].name.length + 1U, 1U);
        if (name == NULL) {
            return false;
        }
        if (columns[i].name.length > 0U) {
            memcpy(name, columns[i].name.bytes, columns[i].name.length);
        }
        name[columns[i].name.length] = '\0';
        result->names[i] = name;
    }
    return result->names != NULL;
}

bool trl_result_set_columns(trl_result *result, const trl_column *columns, uint32_t count) {
    const trl_column *copies = trl_columns_copy(&result->arena, columns, count);
    return copies != NULL && trl_result_take_columns(result, copies, count);
}

bool trl_result_add_rows(trl_result *result, const trl_row *rows, uint32_t count) {
    if (result->row_count + count > result->row_capacity) {
        size_t capacity = result->row_capacity == 0U ? TRL_ROWS_FIRST : result->row_capacity;
        while (capacity < result->row_count + count) {
            capacity *= TRL_ROWS_GROWTH;
        }
        trl_row *grown = trl_realloc(result->rows, capacity * sizeof *grown);
        if (grown == NULL) {
            return false;
        }
        result->rows = grown;
        result->row_capacity = capacity;
    }
    if (count > 0U) {
        memcpy(&result->rows[result->row_count], rows, count * sizeof *rows);
    }
    result->row_count += count;
    return true;
}

void trl_result_complete(trl_result *result, const trl_completed *completed) {
    result->sqlcode = completed->sqlcode;
    result->followed = completed->more;
    trl_format(result->sqlstate, sizeof result->sqlstate, "%.*s", (int)completed->sqlstate.length,
               (const char *)completed->sqlstate.bytes);
}

void trl_query_of(const trl_conn *conn, const char *sql, bool multi, trl_query_request *query) {
    memset(query, 0, sizeof *query);
    query->sql.bytes = (const uint8_t *)sql;
    query->sql.length = strlen(sql);
    query->ends = conn->autocommit;
    query->multi = multi;
    query->attribute_count = trl_conn_query_attributes(conn, &query->attributes);
}

bool trl_read_rows(trl_conn *conn, trl_result *result) {
    for (;;) {
        trl_reply reply;
        if (!trl_conn_receive(conn, &reply)) {
            return false;
        }
        switch (reply.kind) {
        case TRL_KIND_ROWS:
            if (!trl_result_add_rows(result, reply.as.rows.rows, reply.as.rows.count)) {
                trl_failure_no_memory(&conn->failure);
                return false;
            }
            break;
        case TRL_KIND_COMPLETE:
            trl_result_complete(result, &reply.as.complete);
            return true;
        case TRL_KIND_SUSPENDED:
            result->more = true;
            return true;
        case TRL_KIND_ERROR:
            trl_conn_failed_by(conn, &reply.as.error);
            return false;
        default:
            trl_conn_lose_by(conn, "the server broke off its rows");
            return false;
        }
    }
}

bool trl_result_has_rows(const trl_result *result) {
    return result != NULL && result->has_rows;
}

size_t trl_result_column_count(const trl_result *result) {
    return result == NULL ? 0U : result->column_count;
}

const char *trl_result_column_name(const trl_result *result, size_t column) {
    return result == NULL || column >= result->column_count ? NULL : result->names[column];
}

const trl_data_type *trl_result_column_type(const trl_result *result, size_t column) {
    return result == NULL || column >= result->column_count ? NULL : &result->columns[column].type;
}

size_t trl_result_row_count(const trl_result *result) {
    return result == NULL ? 0U : result->row_count;
}

const trl_value *trl_result_value(const trl_result *result, size_t row, size_t column) {
    if (result == NULL || row >= result->row_count || column >= result->rows[row].count) {
        return NULL;
    }
    return &result->rows[row].values[column];
}

bool trl_result_more(const trl_result *result) {
    return result != NULL && result->more;
}

int32_t trl_result_sqlcode(const trl_result *result) {
    return result == NULL ? 0 : result->sqlcode;
}

const char *trl_result_sqlstate(const trl_result *result) {
    return result == NULL ? "" : result->sqlstate;
}

void trl_result_free(trl_result *result) {
    if (result == NULL) {
        return;
    }
    trl_arena_free(&result->arena);
    free(result->rows);
    free(result);
}
