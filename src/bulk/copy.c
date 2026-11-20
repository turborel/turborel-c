/**
 * @file copy.c
 * @brief Bulk copy in [CLIENT Copying and exporting]: CopyIn names the
 * table and its columns [PROTOCOL CopyIn 'I'], CopyData sends rows a chunk
 * at a time with no reply [PROTOCOL CopyData 'J'], and CopyEnd ends it,
 * its reply the rows inserted, or the copy's failure, every row of it
 * undone [PROTOCOL CopyEnd 'N'].
 */
#include "bulk/copy.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/requests.h"
#include "codec/values.h"
#include "conn/conn.h"
#include "data/params.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A copy: its connection, the rows written and not yet sent, and whether
   it ended. */
struct trl_copy {
    trl_conn *conn;
    trl_buffer rows;
    size_t row_count;
    bool ended;
};

/* Sends CopyIn of `target`, its transaction ended with it if `ends`. */
static bool begin(trl_conn *conn, const trl_copy_target *target, bool ends) {
    trl_slice *columns = trl_calloc(target->column_count == 0U ? 1U : target->column_count, sizeof *columns);
    if (columns == NULL) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    for (size_t i = 0U; i < target->column_count; ++i) {
        columns[i].bytes = (const uint8_t *)target->columns[i];
        columns[i].length = strlen(target->columns[i]);
    }
    trl_copy_in_request copy_in;
    memset(&copy_in, 0, sizeof copy_in);
    copy_in.table.bytes = (const uint8_t *)target->table;
    copy_in.table.length = strlen(target->table);
    copy_in.columns = columns;
    copy_in.column_count = target->column_count;
    copy_in.ends = ends;
    trl_codec_error error = {0, "", ""};
    bool encoded = trl_encode_copy_in(&conn->out, &copy_in, &error);
    free(columns);
    return encoded ? trl_conn_send(conn) : trl_conn_codec_failed(conn, &error);
}

/* Whether `target` names a table, and each column it names. */
static bool target_given(const trl_copy_target *target) {
    if (target == NULL || target->table == NULL || (target->column_count > 0U && target->columns == NULL)) {
        return false;
    }
    for (size_t i = 0U; i < target->column_count; ++i) {
        if (target->columns[i] == NULL) {
            return false;
        }
    }
    return true;
}

trl_status trl_copy_in(trl_conn *conn, const trl_copy_target *target, trl_copy **out) {
    return trl_copy_begin(conn, target, conn != NULL && conn->autocommit, out);
}

trl_status trl_copy_begin(trl_conn *conn, const trl_copy_target *target, bool ends, trl_copy **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || out == NULL || !target_given(target)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_copy *copy = trl_calloc(1U, sizeof *copy);
    if (copy == NULL) {
        return TRL_NO_MEMORY;
    }
    if (!trl_conn_needs(conn, false, TRL_CAPABILITY_COPY) || !begin(conn, target, ends)) {
        free(copy);
        return TRL_FAILED;
    }
    conn->copying = true;
    copy->conn = conn;
    trl_buffer_init(&copy->rows);
    *out = copy;
    return TRL_OK;
}

/* Sends the rows written and not yet sent. */
static bool flush(trl_copy *copy) {
    if (copy->row_count == 0U) {
        return true;
    }
    trl_codec_error error = {0, "", ""};
    trl_slice rows = {copy->rows.data, copy->rows.length};
    bool encoded = trl_encode_copy_rows(&copy->conn->out, rows, copy->row_count, &error);
    trl_buffer_free(&copy->rows);
    copy->row_count = 0U;
    return encoded ? trl_conn_send(copy->conn) : trl_conn_codec_failed(copy->conn, &error);
}

trl_status trl_copy_send(trl_copy *copy, const trl_params *row) {
    if (copy == NULL || row == NULL || copy->ended) {
        return TRL_INVALID_ARGUMENT;
    }
    size_t before = copy->rows.length;
    trl_codec_error error = {0, "", ""};
    if (!trl_write_row(&copy->rows, trl_params_values(row), trl_params_count(row), &error)) {
        if (copy->rows.state == TRL_BUFFER_OK) {
            copy->rows.length = before;
        }
        (void)trl_conn_codec_failed(copy->conn, &error);
        return TRL_FAILED;
    }
    ++copy->row_count;
    if (copy->rows.length >= TRL_COPY_CHUNK_BYTES && !flush(copy)) {
        return TRL_FAILED;
    }
    return TRL_OK;
}

/* Sends CopyEnd, abandoned if `abandon`, and reads its completion. */
static bool end(trl_copy *copy, bool abandon) {
    copy->ended = true;
    copy->conn->copying = false;
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_copy_end(&copy->conn->out, abandon, &error)) {
        return trl_conn_codec_failed(copy->conn, &error);
    }
    return trl_conn_send(copy->conn) && trl_conn_completion(copy->conn);
}

trl_status trl_copy_finish(trl_copy *copy, uint64_t *rows) {
    if (copy == NULL || copy->ended) {
        return TRL_INVALID_ARGUMENT;
    }
    if (rows != NULL) {
        *rows = 0U;
    }
    bool flushed = flush(copy);
    if (!flushed && !trl_conn_is_lost(copy->conn)) {
        /* Rows that could not be sent: the copy abandoned so the connection
           takes requests again, as turborel-client's drop abandons it, the
           failure kept the rows'. */
        trl_failure kept;
        trl_failure_init(&kept);
        trl_failure_copy(&kept, &copy->conn->failure);
        (void)end(copy, true);
        trl_failure_copy(&copy->conn->failure, &kept);
        trl_failure_clear(&kept);
        return TRL_FAILED;
    }
    if (!flushed) {
        copy->ended = true;
        copy->conn->copying = false;
        return TRL_FAILED;
    }
    if (!end(copy, false)) {
        return TRL_FAILED;
    }
    if (rows != NULL) {
        (void)trl_conn_rows_affected(copy->conn, rows);
    }
    return TRL_OK;
}

trl_status trl_copy_abandon(trl_copy *copy) {
    if (copy == NULL || copy->ended) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_buffer_free(&copy->rows);
    copy->row_count = 0U;
    return end(copy, true) ? TRL_OK : TRL_FAILED;
}

void trl_copy_free(trl_copy *copy) {
    if (copy == NULL) {
        return;
    }
    if (!copy->ended && !trl_conn_is_lost(copy->conn)) {
        /* Abandoned so the connection takes requests again; a failure that
           lost it says so at its next request. */
        (void)trl_copy_abandon(copy);
    }
    copy->conn->copying = false;
    trl_buffer_free(&copy->rows);
    free(copy);
}

trl_status trl_copy_rows(trl_conn *conn, const trl_copy_target *target, trl_row_source source, void *context,
                         uint64_t *rows) {
    if (source == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_params *row = NULL;
    if (trl_params_new(&row) != TRL_OK) {
        return TRL_NO_MEMORY;
    }
    trl_copy *copy = NULL;
    trl_status status = trl_copy_in(conn, target, &copy);
    while (status == TRL_OK) {
        trl_params_clear(row);
        trl_source_status given = source(context, row);
        if (given == TRL_SOURCE_END) {
            status = trl_copy_finish(copy, rows);
            break;
        }
        if (given != TRL_SOURCE_ROW) {
            (void)trl_copy_abandon(copy);
            trl_failure_set(&conn->failure, TRL_SQLCODE_API, TRL_SQLSTATE_API, "the program's source of rows failed");
            status = TRL_FAILED;
            break;
        }
        status = trl_copy_send(copy, row);
    }
    trl_copy_free(copy);
    trl_params_free(row);
    return status;
}
