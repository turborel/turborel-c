/**
 * @file export.c
 * @brief Export [CLIENT Copying and exporting]: a query's rows written as
 * they come, a batch at a time, so memory holds a batch whatever the
 * result's size. As CSV (RFC 4180): a header of the columns' names, then a
 * record for each row. As native rows: the protocol's own frames, a
 * Description, then Rows, then a Complete.
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "conn/conn.h"
#include "data/text.h"
#include "failure.h"
#include "stmt/result.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What ends a CSV record (RFC 4180 §2 rule 1). */
#define TRL_CRLF "\r\n"

/* An export under way: where it writes, the bytes of a batch, and whether
   a write failed, which ends it. */
typedef struct exporting {
    const trl_export_sink *sink;
    trl_buffer bytes;
    bool failed;
} exporting;

/* Writes what is gathered, unless a write failed before. */
static void write_out(exporting *e) {
    if (!e->failed && e->bytes.length > 0U) {
        e->failed = !e->sink->write(e->sink->context, e->bytes.data, e->bytes.length);
    }
    e->bytes.length = 0U;
}

/* Whether `byte` is white space at a field's end, as Rust's
   `char::is_whitespace` takes an ASCII one. */
static bool white(uint8_t byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' || byte == '\f';
}

/* Whether a field must be quoted: empty, or holding a comma, a quote or a
   line break, or with white space at either end. */
static bool must_quote(const uint8_t *text, size_t length) {
    if (length == 0U || white(text[0]) || white(text[length - 1U])) {
        return true;
    }
    for (size_t i = 0U; i < length; ++i) {
        if (text[i] == ',' || text[i] == '"' || text[i] == '\n' || text[i] == '\r') {
            return true;
        }
    }
    return false;
}

/* Appends a CSV field, quoted where it must be, a quote in it twice. */
static void put_field(trl_buffer *out, const uint8_t *text, size_t length) {
    if (!must_quote(text, length)) {
        trl_put_raw(out, text, length);
        return;
    }
    trl_put_u8(out, (uint8_t)'"');
    for (size_t i = 0U; i < length; ++i) {
        trl_put_u8(out, text[i]);
        if (text[i] == '"') {
            trl_put_u8(out, (uint8_t)'"');
        }
    }
    trl_put_u8(out, (uint8_t)'"');
}

/* Appends a value as a CSV field: a null empty and unquoted. */
static void put_value(trl_buffer *out, const trl_value *value) {
    if (value->tag == TRL_TAG_NULL) {
        return;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    trl_value_put_text(&text, value);
    put_field(out, text.data, text.length);
    trl_buffer_free(&text);
}

/* Writes the CSV header of the columns' names. */
static void csv_header(exporting *e, const trl_column *columns, uint32_t count) {
    for (uint32_t i = 0U; i < count; ++i) {
        if (i > 0U) {
            trl_put_u8(&e->bytes, (uint8_t)',');
        }
        put_field(&e->bytes, columns[i].named ? columns[i].name.bytes : NULL,
                  columns[i].named ? columns[i].name.length : 0U);
    }
    trl_put_raw(&e->bytes, (const uint8_t *)TRL_CRLF, sizeof TRL_CRLF - 1U);
    write_out(e);
}

/* Writes a batch of rows as CSV records. */
static void csv_rows(exporting *e, const trl_row *rows, uint32_t count) {
    for (uint32_t r = 0U; r < count; ++r) {
        for (uint32_t c = 0U; c < rows[r].count; ++c) {
            if (c > 0U) {
                trl_put_u8(&e->bytes, (uint8_t)',');
            }
            put_value(&e->bytes, &rows[r].values[c]);
        }
        trl_put_raw(&e->bytes, (const uint8_t *)TRL_CRLF, sizeof TRL_CRLF - 1U);
    }
    write_out(e);
}

/* Writes a frame of `kind` and `body`. */
static void frame(exporting *e, uint8_t kind, trl_slice body) {
    uint8_t head[TRL_FRAME_HEADER_BYTES];
    trl_frame_header header = {kind, (uint32_t)body.length};
    (void)trl_frame_header_encode(header, head, sizeof head);
    trl_put_raw(&e->bytes, head, sizeof head);
    trl_put_raw(&e->bytes, body.bytes, body.length);
    write_out(e);
}

/* Writes the reply just read: CSV of what it decoded to, or native, the
   body the server sent. */
static void written(exporting *e, const trl_conn *conn, const trl_reply *reply) {
    if (e->sink->format == TRL_EXPORT_NATIVE) {
        frame(e, reply->kind, conn->last_body);
    } else if (reply->kind == TRL_KIND_DESCRIPTION) {
        csv_header(e, reply->as.description.columns, reply->as.description.count);
    } else {
        csv_rows(e, reply->as.rows.rows, reply->as.rows.count);
    }
}

/* Writes the Complete native rows end with. */
static void native_end(exporting *e) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    trl_slice state = {(const uint8_t *)"00000", sizeof "00000" - 1U};
    trl_put_bytes(&body, state);
    trl_put_flag(&body, false);
    trl_slice bytes = {body.data, body.length};
    frame(e, TRL_KIND_COMPLETE, bytes);
    trl_buffer_free(&body);
}

/* Reads the query's replies to its end, each Description and Rows
   written; the rows read, or false of a failure. */
static bool read_rows(trl_conn *conn, exporting *e, uint64_t *rows) {
    trl_reply reply;
    if (!trl_conn_receive(conn, &reply)) {
        return false;
    }
    if (reply.kind == TRL_KIND_COMPLETE) {
        trl_failure_broken(&conn->failure, "an export runs a query");
        return false;
    }
    if (reply.kind != TRL_KIND_DESCRIPTION) {
        if (reply.kind == TRL_KIND_ERROR) {
            trl_conn_failed_by(conn, &reply.as.error);
        } else {
            trl_conn_lose_by(conn, "the server sent no result");
        }
        return false;
    }
    written(e, conn, &reply);
    for (;;) {
        if (!trl_conn_receive(conn, &reply)) {
            return false;
        }
        if (reply.kind == TRL_KIND_ROWS) {
            *rows += reply.as.rows.count;
            written(e, conn, &reply);
        } else if (reply.kind == TRL_KIND_COMPLETE) {
            return true;
        } else if (reply.kind == TRL_KIND_ERROR) {
            trl_conn_failed_by(conn, &reply.as.error);
            return false;
        } else {
            trl_conn_lose_by(conn, "the server broke off its rows");
            return false;
        }
    }
}

trl_status trl_export(trl_conn *conn, const char *sql, const trl_export_sink *sink, uint64_t *rows) {
    if (rows != NULL) {
        *rows = 0U;
    }
    if (conn == NULL || sql == NULL || sink == NULL || sink->write == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_query_request query;
    trl_query_of(conn, sql, false, &query);
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_query(&conn->out, &query, &error)) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    exporting e;
    memset(&e, 0, sizeof e);
    e.sink = sink;
    trl_buffer_init(&e.bytes);
    uint64_t count = 0U;
    bool read = trl_conn_send(conn) && read_rows(conn, &e, &count);
    if (read && sink->format == TRL_EXPORT_NATIVE) {
        native_end(&e);
    }
    bool memory = e.bytes.state == TRL_BUFFER_OK;
    trl_buffer_free(&e.bytes);
    if (read && !memory) {
        trl_failure_no_memory(&conn->failure);
        return TRL_FAILED;
    }
    if (read && e.failed) {
        trl_failure_broken(&conn->failure, "cannot write the export");
        return TRL_FAILED;
    }
    if (rows != NULL) {
        *rows = count;
    }
    return read ? TRL_OK : TRL_FAILED;
}
