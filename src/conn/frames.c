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

/* The SQLSTATE class of a malformed frame, which loses the connection. */
#define TRL_CONNECTION_CLASS "08"

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

/* Copies a reply's body into the arena of the result it outlives the next
   reply for, so what it decodes to is kept with it. */
static bool kept(trl_conn *conn, trl_slice *body) {
    if (body->length == 0U) {
        body->bytes = NULL;
        return true;
    }
    uint8_t *copy = trl_arena_alloc(conn->keep, body->length, 1U);
    if (copy == NULL) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    memcpy(copy, body->bytes, body->length);
    body->bytes = copy;
    return true;
}

/* Reads a reply's header, losing the connection if it is longer than taken. */
static bool read_header(trl_conn *conn, trl_frame_header *header) {
    uint8_t head[TRL_FRAME_HEADER_BYTES];
    taken got = take(conn, head, sizeof head);
    if (got != TAKEN) {
        if (got == TAKEN_END) {
            /* Said as Rust's read_exact says it. */
            trl_io_error_set(&conn->transport.error, "failed to fill whole buffer", false);
        }
        return io_lost(conn, "receive");
    }
    if (trl_frame_header_decode(head, sizeof head, conn->max_body, header) != TRL_FRAME_OK) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        /* Read again with no bound, for its length alone. */
        (void)trl_frame_header_decode(head, sizeof head, UINT32_MAX, header);
        trl_format(message, sizeof message, "a message of %lu bytes is longer than the %lu taken",
                   (unsigned long)header->length, (unsigned long)conn->max_body);
        trl_conn_lose_by(conn, message);
        return false;
    }
    return true;
}

/* Reads one frame, inflated if it came compressed and compression is
   granted: its kind and body. A frame that does not come whole, or does
   not inflate, loses the connection. */
static bool next_frame(trl_conn *conn, uint8_t *kind, trl_slice *body) {
    trl_frame_header header = {0U, 0U};
    if (!read_header(conn, &header) || !take_body(conn, header.length)) {
        return false;
    }
    body->bytes = conn->body.data;
    body->length = conn->body.length;
    *kind = header.kind;
    if (*kind == TRL_KIND_COMPRESSED && conn->compressed) {
        trl_codec_error error = {0, "", ""};
        trl_buffer_free(&conn->inflated);
        if (!trl_decompress_frame(*body, conn->max_body, kind, &conn->inflated, &error)) {
            trl_conn_lose_by(conn, error.message);
            return false;
        }
        body->bytes = conn->inflated.data;
        body->length = conn->inflated.length;
    }
    return true;
}

/* Decodes a frame, in the arena of the result that outlives the next
   reply, if one is kept. */
static bool decode_frame(trl_conn *conn, uint8_t kind, trl_slice body, trl_reply *out, trl_codec_error *error) {
    trl_arena *arena = &conn->arena;
    if (conn->keep == NULL) {
        trl_arena_free(&conn->arena);
    } else if (!kept(conn, &body)) {
        trl_codec_no_memory(error);
        return false;
    } else {
        arena = conn->keep;
    }
    return trl_decode_reply(kind, body, arena, out, error);
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
        trl_conn_completed(conn, &reply->as.complete);
        break;
    case TRL_KIND_ERROR:
        conn->active = reply->as.error.active;
        conn->warning[0] = '\0';
        break;
    case TRL_KIND_DESCRIPTION:
        conn->active = true;
        conn->warning[0] = '\0';
        break;
    case TRL_KIND_ROWS:
    case TRL_KIND_SUSPENDED:
    case TRL_KIND_LOB_REPLY:
        /* A locator is valid within a transaction alone. */
        conn->active = true;
        break;
    case TRL_KIND_READY:
        /* Ready ends a login or a ChangeUser, which renews the session as
           Reset does, its transaction rolled back. */
        conn->active = false;
        conn->warning[0] = '\0';
        break;
    default:
        break;
    }
}

/* Takes a Notice or a Status: 1 if `reply` was one, 0 if not, -1 if
   memory to keep it could not be had. */
static int took(trl_conn *conn, const trl_reply *reply) {
    if (reply->kind == TRL_KIND_STATUS) {
        if (!trl_conn_tell(conn, reply->as.status.name, reply->as.status.value)) {
            trl_failure_no_memory(&conn->failure);
            return -1;
        }
        return 1;
    }
    if (reply->kind == TRL_KIND_NOTICE) {
        trl_conn_notice(conn, reply);
        return 1;
    }
    return 0;
}

/* Takes a frame that did not decode (RULES.md D8): a malformed one, of
   class 08, loses the connection; of one holding a value the client
   cannot take, a Complete's head is noted as its whole would be. */
static bool skipped(trl_conn *conn, uint8_t kind, trl_slice body, const trl_codec_error *error) {
    if (strncmp(error->sqlstate, TRL_CONNECTION_CLASS, sizeof TRL_CONNECTION_CLASS - 1U) == 0) {
        trl_conn_lose_by(conn, error->message);
        return false;
    }
    trl_reply head;
    memset(&head, 0, sizeof head);
    head.kind = TRL_KIND_COMPLETE;
    if (trl_completed_head(kind, body, &conn->arena, &head.as.complete)) {
        noted(conn, &head);
    }
    return true;
}

/* Reads past the rest of a request's replies once one, of `kind` and
   `body`, held a value the client cannot take, up to the one that ends
   them, each taken as a reply is, or skipped as that one was. */
static bool drained(trl_conn *conn, uint8_t kind, trl_slice body) {
    bool ends = trl_reply_ends_request(kind, body, &conn->arena);
    trl_arena *keep = conn->keep;
    conn->keep = NULL;
    bool read = true;
    while (read && !ends) {
        trl_codec_error error = {0, "", ""};
        trl_reply reply;
        read = next_frame(conn, &kind, &body);
        if (read && decode_frame(conn, kind, body, &reply, &error)) {
            int said = took(conn, &reply);
            read = said >= 0;
            if (said == 0) {
                noted(conn, &reply);
            }
        } else if (read) {
            read = skipped(conn, kind, body, &error);
        }
        ends = read && trl_reply_ends_request(kind, body, &conn->arena);
    }
    conn->keep = keep;
    return read;
}

/* Fails the request of a frame that did not decode: a malformed one loses
   the connection; one holding a value the client cannot take has the rest
   of its request's replies read past, unless the request is answered by
   `one` reply, and fails with its value's failure. */
static bool undecoded(trl_conn *conn, uint8_t kind, trl_slice body, const trl_codec_error *error, bool one) {
    if (skipped(conn, kind, body, error) && (one || drained(conn, kind, body))) {
        trl_failure_from_codec(&conn->failure, error);
    }
    return false;
}

/* Receives the next reply but Notices and Statuses; one holding a value
   the client cannot take fails its request alone, read past with the rest
   of its replies unless the request is answered by `one` reply. */
static bool receive_as(trl_conn *conn, trl_reply *out, bool one) {
    if (conn->lost) {
        trl_failure_copy(&conn->failure, &conn->lost_by);
        return false;
    }
    for (;;) {
        uint8_t kind = 0U;
        trl_slice body = {NULL, 0U};
        if (!next_frame(conn, &kind, &body)) {
            return false;
        }
        trl_codec_error error = {0, "", ""};
        if (!decode_frame(conn, kind, body, out, &error)) {
            return undecoded(conn, kind, body, &error, one);
        }
        int said = took(conn, out);
        if (said <= 0) {
            if (said == 0) {
                noted(conn, out);
            }
            return said == 0;
        }
    }
}

bool trl_conn_receive(trl_conn *conn, trl_reply *out) {
    return receive_as(conn, out, false);
}

bool trl_conn_receive_one(trl_conn *conn, trl_reply *out) {
    return receive_as(conn, out, true);
}
