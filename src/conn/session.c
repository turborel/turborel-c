/**
 * @file session.c
 * @brief What a connection says of its session
 * [CLIENT What a connection asks for] [CLIENT What the server tells], its timeouts changed
 * [CLIENT Timeouts], Ping [PROTOCOL Ping 'G'], and how it ends
 * [PROTOCOL Terminate 'X'].
 */
#include "auth/crypto.h"
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "conn/conn.h"
#include "conn/target.h"
#include "failure.h"
#include "format.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const trl_failure *trl_conn_failure(const trl_conn *conn) {
    return conn == NULL ? NULL : &conn->failure;
}

bool trl_conn_is_lost(const trl_conn *conn) {
    return conn == NULL || conn->lost;
}

const char *trl_conn_server(const trl_conn *conn) {
    return conn == NULL || conn->server == NULL ? "" : conn->server;
}

const char *trl_conn_authorization(const trl_conn *conn) {
    return conn == NULL || conn->authorization == NULL ? "" : conn->authorization;
}

bool trl_conn_granted(const trl_conn *conn, const char *capability) {
    if (conn == NULL || capability == NULL) {
        return false;
    }
    for (size_t i = 0U; i < conn->granted_count; ++i) {
        if (strcmp(conn->granted[i], capability) == 0) {
            return true;
        }
    }
    return false;
}

size_t trl_conn_granted_count(const trl_conn *conn) {
    return conn == NULL ? 0U : conn->granted_count;
}

const char *trl_conn_granted_at(const trl_conn *conn, size_t index) {
    return conn == NULL || index >= conn->granted_count ? NULL : conn->granted[index];
}

const char *trl_conn_setting(const trl_conn *conn, const char *name) {
    if (conn == NULL || name == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < conn->setting_count; ++i) {
        if (strcmp(conn->settings[i].name, name) == 0) {
            return conn->settings[i].value;
        }
    }
    return NULL;
}

size_t trl_conn_setting_count(const trl_conn *conn) {
    return conn == NULL ? 0U : conn->setting_count;
}

bool trl_conn_setting_at(const trl_conn *conn, size_t index, const char **name, const char **value) {
    if (conn == NULL || index >= conn->setting_count) {
        return false;
    }
    if (name != NULL) {
        *name = conn->settings[index].name;
    }
    if (value != NULL) {
        *value = conn->settings[index].value;
    }
    return true;
}

trl_status trl_conn_set_read_timeout(trl_conn *conn, uint32_t read_ms) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (conn->transport.ops == NULL || conn->transport.ops->bound == NULL) {
        trl_failure_broken(&conn->failure, conn->transport.ops == NULL
                                               ? "the connection is closed"
                                               : "a stream given to open is bounded by its maker");
        return TRL_FAILED;
    }
    if (!conn->transport.ops->bound(&conn->transport, read_ms)) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "cannot bound how long the connection waits: %s",
                   conn->transport.error.text);
        trl_failure_broken(&conn->failure, message);
        return TRL_FAILED;
    }
    conn->read_ms = read_ms;
    return TRL_OK;
}

uint32_t trl_conn_read_timeout(const trl_conn *conn) {
    return conn == NULL ? TRL_NO_LIMIT : conn->read_ms;
}

trl_status trl_conn_set_max_body(trl_conn *conn, uint32_t bytes) {
    if (conn == NULL || bytes == 0U || bytes > TRL_MAX_BODY_BYTES) {
        return TRL_INVALID_ARGUMENT;
    }
    conn->max_body = bytes;
    return TRL_OK;
}

/* Fails, sending nothing, unless the server granted `capability`. */
static bool needs(trl_conn *conn, const char *capability) {
    if (trl_conn_granted(conn, capability)) {
        return true;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "the server did not grant %s", capability);
    trl_failure_broken(&conn->failure, message);
    return false;
}

/* Fails as the codec failed. */
static trl_status codec_failed(trl_conn *conn, const trl_codec_error *error) {
    trl_failure_from_codec(&conn->failure, error);
    return TRL_FAILED;
}

trl_status trl_conn_ping(trl_conn *conn) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!needs(conn, TRL_CAPABILITY_PING)) {
        return TRL_FAILED;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_bare(&conn->out, TRL_KIND_PING, &error)) {
        return codec_failed(conn, &error);
    }
    trl_reply reply;
    if (!trl_conn_send(conn) || !trl_conn_receive(conn, &reply)) {
        return TRL_FAILED;
    }
    if (reply.kind == TRL_KIND_PONG) {
        return TRL_OK;
    }
    if (reply.kind == TRL_KIND_ERROR) {
        trl_failure failure;
        trl_failure_init(&failure);
        trl_failure_from_wire(&failure, &reply.as.error);
        trl_conn_failed(conn, &failure);
        trl_failure_clear(&failure);
        return TRL_FAILED;
    }
    trl_conn_lose_by(conn, "the server sent no Pong");
    return TRL_FAILED;
}

/* Reads what the server sends until it closes, at most `most` bytes, the
   bytes read and not yet taken first. */
static bool read_rest(trl_conn *conn, trl_buffer *rest, size_t most) {
    size_t ready = conn->input_end - conn->input_at;
    trl_put_raw(rest, conn->input + conn->input_at, ready < most ? ready : most);
    conn->input_at = conn->input_end;
    uint8_t chunk[TRL_READ_BUFFER_BYTES];
    while (rest->length < most && rest->state == TRL_BUFFER_OK) {
        size_t wanted = most - rest->length < sizeof chunk ? most - rest->length : sizeof chunk;
        int64_t got = conn->transport.ops->read(&conn->transport, chunk, wanted);
        if (got < 0) {
            trl_io_failed(&conn->failure, "receive", &conn->transport.error);
            return false;
        }
        if (got == 0) {
            break;
        }
        trl_put_raw(rest, chunk, (size_t)got);
    }
    if (rest->state != TRL_BUFFER_OK) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    return true;
}

/* What the server said as it closed: an Error, which `terminate` gives,
   or nothing it need. */
static trl_status closing_said(trl_conn *conn, const trl_buffer *rest) {
    if (rest->length < TRL_FRAME_HEADER_BYTES) {
        return TRL_OK;
    }
    trl_frame_header header = {0U, 0U};
    if (trl_frame_header_decode(rest->data, rest->length, conn->max_body, &header) != TRL_FRAME_OK) {
        trl_failure_broken(&conn->failure, "a message is longer than the bytes taken");
        return TRL_FAILED;
    }
    trl_slice body = {rest->data + TRL_FRAME_HEADER_BYTES, rest->length - TRL_FRAME_HEADER_BYTES};
    trl_codec_error error = {0, "", ""};
    trl_reply reply;
    trl_arena_free(&conn->arena);
    if (!trl_decode_reply(header.kind, body, &conn->arena, &reply, &error)) {
        return codec_failed(conn, &error);
    }
    switch (reply.kind) {
    case TRL_KIND_ERROR:
        trl_failure_from_wire(&conn->failure, &reply.as.error);
        return TRL_FAILED;
    case TRL_KIND_NOTICE:
    case TRL_KIND_STATUS:
    case TRL_KIND_PONG:
        return TRL_OK;
    default:
        trl_failure_broken(&conn->failure, "the server answered Terminate");
        return TRL_FAILED;
    }
}

/* Sends Terminate, and reads what the server sends until it closes. */
static trl_status terminate(trl_conn *conn) {
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_bare(&conn->out, TRL_KIND_TERMINATE, &error)) {
        return codec_failed(conn, &error);
    }
    if (!trl_conn_send(conn)) {
        return TRL_FAILED;
    }
    uint32_t within = conn->read_ms == TRL_NO_LIMIT ? TRL_TERMINATE_WITHIN_MS : conn->read_ms;
    if (conn->transport.ops->bound != NULL && !conn->transport.ops->bound(&conn->transport, within)) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "cannot bound how long the connection waits: %s",
                   conn->transport.error.text);
        trl_failure_broken(&conn->failure, message);
        return TRL_FAILED;
    }
    trl_buffer rest;
    trl_buffer_init(&rest);
    trl_status said = read_rest(conn, &rest, (size_t)conn->max_body + TRL_FRAME_HEADER_BYTES)
                          ? closing_said(conn, &rest)
                          : TRL_FAILED;
    trl_buffer_free(&rest);
    return said;
}

trl_status trl_conn_terminate(trl_conn *conn) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status ended = terminate(conn);
    trl_transport_close(&conn->transport);
    if (!conn->lost) {
        trl_failure_broken(&conn->lost_by, "the connection was terminated");
        conn->lost = true;
    }
    return ended;
}

void trl_conn_free(trl_conn *conn) {
    if (conn == NULL) {
        return;
    }
    trl_transport_close(&conn->transport);
    trl_target_free(conn->reach);
    free(conn->authorization);
    free(conn->server);
    for (size_t i = 0U; i < conn->granted_count; ++i) {
        free(conn->granted[i]);
    }
    free((void *)conn->granted);
    for (size_t i = 0U; i < conn->setting_count; ++i) {
        free(conn->settings[i].name);
        free(conn->settings[i].value);
    }
    free(conn->settings);
    free(conn->input);
    trl_buffer_free(&conn->body);
    trl_buffer_free(&conn->inflated);
    trl_buffer_free(&conn->out);
    trl_buffer_free(&conn->compressed_out);
    trl_arena_free(&conn->arena);
    trl_failure_clear(&conn->failure);
    trl_failure_clear(&conn->lost_by);
    trl_cleanse(conn->key, sizeof conn->key);
    free(conn);
}
