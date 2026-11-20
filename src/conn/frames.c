/**
 * @file frames.c
 * @brief A connection's requests sent and replies received
 * [PROTOCOL Frames] [PROTOCOL Compressed frames]: compressed once
 * `compression` is granted, each reply taken no longer than the longest
 * body taken, and a connection that fails lost, every call after failing
 * as it was lost (RULES.md R-ERR-4).
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/error.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "conn/conn.h"
#include "conn/target.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void trl_conn_lose(trl_conn *conn, const trl_failure *failure) {
    if (failure != &conn->failure) {
        trl_failure_copy(&conn->failure, failure);
    }
    trl_failure_copy(&conn->lost_by, &conn->failure);
    conn->lost = true;
}

void trl_conn_lose_by(trl_conn *conn, const char *message) {
    trl_failure_broken(&conn->failure, message);
    trl_conn_lose(conn, &conn->failure);
}

void trl_conn_failed(trl_conn *conn, const trl_failure *failure) {
    if (failure != &conn->failure) {
        trl_failure_copy(&conn->failure, failure);
    }
    if (trl_failure_ends_connection(&conn->failure)) {
        trl_conn_lose(conn, &conn->failure);
    }
}

/* Loses the connection as an open, read or write failed. */
static bool io_lost(trl_conn *conn, const char *what) {
    trl_io_failed(&conn->failure, what, &conn->transport.error);
    trl_conn_lose(conn, &conn->failure);
    return false;
}

bool trl_conn_send(trl_conn *conn) {
    if (conn->lost) {
        trl_failure_copy(&conn->failure, &conn->lost_by);
        trl_buffer_free(&conn->out);
        return false;
    }
    trl_slice frames = {conn->out.data, conn->out.length};
    if (conn->compressed) {
        trl_codec_error error = {0, "", ""};
        bool made = trl_compress_frames(&conn->compressed_out, frames, conn->compress_from, &error);
        trl_buffer_free(&conn->out);
        if (!made) {
            trl_buffer_free(&conn->compressed_out);
            trl_failure_from_codec(&conn->failure, &error);
            return false;
        }
        frames.bytes = conn->compressed_out.data;
        frames.length = conn->compressed_out.length;
    }
    bool sent = trl_transport_write_all(&conn->transport, frames.bytes, frames.length);
    trl_buffer_free(&conn->out);
    trl_buffer_free(&conn->compressed_out);
    return sent || io_lost(conn, "send");
}

/* Reads more bytes into the input; 0 at the stream's end. */
static int64_t fill(trl_conn *conn) {
    conn->input_at = 0U;
    conn->input_end = 0U;
    int64_t got = conn->transport.ops->read(&conn->transport, conn->input, TRL_READ_BUFFER_BYTES);
    if (got > 0) {
        conn->input_end = (size_t)got;
    }
    return got;
}

/* What taking bytes came to. */
typedef enum taken {
    TAKEN = 0,
    TAKEN_END,
    TAKEN_FAILED,
} taken;

/* Takes exactly `length` bytes into `into`: all, the stream's end before
   them, or a read that failed. */
static taken take(trl_conn *conn, uint8_t *into, size_t length) {
    size_t had = 0U;
    while (had < length) {
        if (conn->input_at == conn->input_end) {
            int64_t got = fill(conn);
            if (got <= 0) {
                return got == 0 ? TAKEN_END : TAKEN_FAILED;
            }
        }
        size_t ready = conn->input_end - conn->input_at;
        size_t step = ready < length - had ? ready : length - had;
        memcpy(into + had, conn->input + conn->input_at, step);
        conn->input_at += step;
        had += step;
    }
    return TAKEN;
}

/* Takes a body of `length` bytes into `conn->body`, holding no more than
   TRL_RESERVED_BYTES ahead of what has arrived (RULES.md R-SAFE-1). */
static bool take_body(trl_conn *conn, size_t length) {
    trl_buffer_free(&conn->body);
    while (conn->body.length < length) {
        size_t step = length - conn->body.length;
        step = step < TRL_RESERVED_BYTES ? step : TRL_RESERVED_BYTES;
        uint8_t *into = trl_put_space(&conn->body, step);
        if (into == NULL) {
            trl_failure_no_memory(&conn->failure);
            return false;
        }
        taken got = take(conn, into, step);
        if (got == TAKEN_END) {
            trl_conn_lose_by(conn, "cannot receive: the connection closed within a reply");
            return false;
        }
        if (got == TAKEN_FAILED) {
            return io_lost(conn, "receive");
        }
    }
    return true;
}

/* Fails as the codec failed, losing the connection if that ends it. */
static bool codec_failed(trl_conn *conn, const trl_codec_error *error) {
    trl_failure failure;
    trl_failure_init(&failure);
    trl_failure_from_codec(&failure, error);
    trl_conn_failed(conn, &failure);
    trl_failure_clear(&failure);
    return false;
}

/* Reads one reply, of any kind, inflating a compressed one if granted. */
static bool next_reply(trl_conn *conn, trl_reply *out) {
    uint8_t head[TRL_FRAME_HEADER_BYTES];
    taken got = take(conn, head, sizeof head);
    if (got != TAKEN) {
        if (got == TAKEN_END) {
            /* Said as Rust's read_exact says it. */
            trl_io_error_set(&conn->transport.error, "failed to fill whole buffer", false);
        }
        return io_lost(conn, "receive");
    }
    trl_frame_header header = {0U, 0U};
    if (trl_frame_header_decode(head, sizeof head, conn->max_body, &header) != TRL_FRAME_OK) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        /* Read again with no bound, for its length alone. */
        (void)trl_frame_header_decode(head, sizeof head, UINT32_MAX, &header);
        trl_format(message, sizeof message, "a message of %lu bytes is longer than the %lu taken",
                   (unsigned long)header.length, (unsigned long)conn->max_body);
        trl_conn_lose_by(conn, message);
        return false;
    }
    if (!take_body(conn, header.length)) {
        return false;
    }
    trl_codec_error error = {0, "", ""};
    trl_slice body = {conn->body.data, conn->body.length};
    uint8_t kind = header.kind;
    if (kind == TRL_KIND_COMPRESSED && conn->compressed) {
        trl_buffer_free(&conn->inflated);
        if (!trl_decompress_frame(body, conn->max_body, &kind, &conn->inflated, &error)) {
            return codec_failed(conn, &error);
        }
        body.bytes = conn->inflated.data;
        body.length = conn->inflated.length;
    }
    trl_arena_free(&conn->arena);
    return trl_decode_reply(kind, body, &conn->arena, out, &error) || codec_failed(conn, &error);
}

/* A copy of `text`, null-ended, or null if memory cannot be had. */
static char *copy_of(trl_slice text) {
    char *copy = trl_malloc(text.length + 1U);
    if (copy != NULL) {
        if (text.length > 0U) {
            memcpy(copy, text.bytes, text.length);
        }
        copy[text.length] = '\0';
    }
    return copy;
}

bool trl_conn_tell(trl_conn *conn, trl_slice name, trl_slice value) {
    char *told = copy_of(value);
    if (told == NULL) {
        return false;
    }
    for (size_t i = 0U; i < conn->setting_count; ++i) {
        trl_setting *at = &conn->settings[i];
        if (strlen(at->name) == name.length && (name.length == 0U || memcmp(at->name, name.bytes, name.length) == 0)) {
            free(at->value);
            at->value = told;
            return true;
        }
    }
    char *named = copy_of(name);
    trl_setting *grown = named == NULL ? NULL : trl_realloc(conn->settings, (conn->setting_count + 1U) * sizeof *grown);
    if (grown == NULL) {
        free(named);
        free(told);
        return false;
    }
    conn->settings = grown;
    conn->settings[conn->setting_count].name = named;
    conn->settings[conn->setting_count].value = told;
    ++conn->setting_count;
    return true;
}

/* Notes what a reply says of the session's transaction. */
static void noted(trl_conn *conn, const trl_reply *reply) {
    switch (reply->kind) {
    case TRL_KIND_COMPLETE:
        conn->active = reply->as.complete.active;
        break;
    case TRL_KIND_ERROR:
        conn->active = reply->as.error.active;
        break;
    case TRL_KIND_DESCRIPTION:
    case TRL_KIND_ROWS:
    case TRL_KIND_SUSPENDED:
    case TRL_KIND_LOB_REPLY:
        conn->active = true;
        break;
    default:
        break;
    }
}

bool trl_conn_receive(trl_conn *conn, trl_reply *out) {
    if (conn->lost) {
        trl_failure_copy(&conn->failure, &conn->lost_by);
        return false;
    }
    for (;;) {
        if (!next_reply(conn, out)) {
            return false;
        }
        if (out->kind == TRL_KIND_STATUS) {
            if (!trl_conn_tell(conn, out->as.status.name, out->as.status.value)) {
                trl_failure_no_memory(&conn->failure);
                return false;
            }
            continue;
        }
        if (out->kind == TRL_KIND_NOTICE) {
            continue;
        }
        noted(conn, out);
        return true;
    }
}
