/**
 * @file control.c
 * @brief Controlling the session [CLIENT Controlling the session]: Reset
 * [PROTOCOL Reset 'W'], ChangeUser [PROTOCOL ChangeUser 'U'], and the
 * query attributes every later statement carries.
 */
#include "auth/mechanism.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "codec/tail.h"
#include "codec/utf8.h"
#include "conn/conn.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool trl_conn_completion(trl_conn *conn) {
    trl_reply reply;
    if (!trl_conn_receive(conn, &reply)) {
        return false;
    }
    if (reply.kind == TRL_KIND_COMPLETE) {
        return true;
    }
    if (reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
        return false;
    }
    trl_conn_lose_by(conn, "the server sent no completion");
    return false;
}

trl_status trl_conn_reset(trl_conn *conn) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_codec_error error = {0, "", ""};
    if (!trl_conn_needs(conn, false, TRL_CAPABILITY_RESET)) {
        return TRL_FAILED;
    }
    if (!trl_encode_bare(&conn->out, TRL_KIND_RESET, &error)) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    return trl_conn_send(conn) && trl_conn_completion(conn) ? TRL_OK : TRL_FAILED;
}

/* Keeps what Ready told of the renewed session: its authorization
   identifier, product and cancel key. */
static bool renewed(trl_conn *conn, const trl_reply *reply) {
    char *authorization = trl_malloc(reply->as.ready.authorization.length + 1U);
    char *server = trl_malloc(reply->as.ready.server.length + 1U);
    if (authorization == NULL || server == NULL) {
        free(authorization);
        free(server);
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    memcpy(authorization, reply->as.ready.authorization.bytes, reply->as.ready.authorization.length);
    authorization[reply->as.ready.authorization.length] = '\0';
    memcpy(server, reply->as.ready.server.bytes, reply->as.ready.server.length);
    server[reply->as.ready.server.length] = '\0';
    free(conn->authorization);
    free(conn->server);
    conn->authorization = authorization;
    conn->server = server;
    if (reply->as.ready.key.length == sizeof conn->key) {
        memcpy(conn->key, reply->as.ready.key.bytes, sizeof conn->key);
        conn->has_key = true;
    }
    return true;
}

/* Answers a Challenge of ChangeUser's exchange. */
static bool answer(trl_conn *conn, trl_mechanism *mechanism, trl_slice challenge) {
    trl_buffer reply;
    trl_buffer_init(&reply);
    if (!trl_mechanism_answer(mechanism, challenge, &reply, &conn->failure)) {
        trl_buffer_free(&reply);
        return false;
    }
    trl_codec_error error = {0, "", ""};
    trl_slice bytes = {reply.data, reply.length};
    bool encoded = trl_encode_answer(&conn->out, bytes, &error);
    trl_buffer_free(&reply);
    return encoded ? trl_conn_send(conn) : trl_conn_codec_failed(conn, &error);
}

/* ChangeUser's exchange, to Ready or Error. */
static bool exchange(trl_conn *conn, trl_mechanism *mechanism) {
    for (;;) {
        trl_reply reply;
        if (!trl_conn_receive(conn, &reply)) {
            return false;
        }
        switch (reply.kind) {
        case TRL_KIND_CHALLENGE:
            if (!answer(conn, mechanism, reply.as.challenge)) {
                return false;
            }
            break;
        case TRL_KIND_READY:
            return trl_mechanism_ready(mechanism, &conn->failure) && renewed(conn, &reply);
        case TRL_KIND_ERROR:
            trl_conn_failed_by(conn, &reply.as.error);
            return false;
        default:
            trl_conn_lose_by(conn, "the server sent no Ready");
            return false;
        }
    }
}

trl_status trl_conn_change_user(trl_conn *conn, const char *user, trl_mechanism *mechanism) {
    if (conn == NULL || user == NULL || mechanism == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!trl_conn_needs(conn, false, TRL_CAPABILITY_CHANGE_USER)) {
        return TRL_FAILED;
    }
    trl_buffer initial;
    trl_buffer_init(&initial);
    if (!trl_mechanism_initial(mechanism, &initial, &conn->failure)) {
        trl_buffer_free(&initial);
        return TRL_FAILED;
    }
    const char *name = trl_mechanism_name(mechanism);
    trl_slice named = {(const uint8_t *)user, strlen(user)};
    trl_slice called = {(const uint8_t *)name, strlen(name)};
    trl_slice first = {initial.data, initial.length};
    trl_codec_error error = {0, "", ""};
    bool encoded = trl_encode_change_user(&conn->out, named, called, first, &error);
    trl_buffer_free(&initial);
    if (!encoded) {
        (void)trl_conn_codec_failed(conn, &error);
        return TRL_FAILED;
    }
    return trl_conn_send(conn) && exchange(conn, mechanism) ? TRL_OK : TRL_FAILED;
}

/* Whether an attribute fits the protocol's bounds: a name of 1 to 64
   bytes, a value of at most 1,024, each UTF-8. */
static bool fits(const char *name, const char *value) {
    size_t named = strlen(name);
    size_t valued = strlen(value);
    return named > 0U && named <= TRL_MAX_ATTRIBUTE_NAME && valued <= TRL_MAX_ATTRIBUTE_VALUE &&
           trl_utf8_valid((const uint8_t *)name, named) && trl_utf8_valid((const uint8_t *)value, valued);
}

/* A copy of `text` as a slice, its bytes its own; false if memory cannot be had. */
static bool copy_slice(const char *text, trl_slice *out) {
    size_t length = strlen(text);
    uint8_t *copy = trl_malloc(length + 1U);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, text, length + 1U);
    out->bytes = copy;
    out->length = length;
    return true;
}

/* Checks the attributes asked for: granted, and within bounds. */
static bool allowed(trl_conn *conn, const char *const *names, const char *const *values, size_t count) {
    if (!trl_conn_needs(conn, count == 0U, TRL_CAPABILITY_QUERY_ATTRIBUTES)) {
        return false;
    }
    bool within = count <= TRL_MAX_ATTRIBUTES;
    for (size_t i = 0U; within && i < count; ++i) {
        within = fits(names[i], values[i]);
    }
    if (!within) {
        /* Refused before it is sent, with the code the server would fail
           the connection with; the connection kept. */
        trl_failure_broken(&conn->failure,
                           "a statement takes at most 32 attributes, each a name of 1 to 64 bytes and a value of at "
                           "most 1024, UTF-8");
    }
    return within;
}

/* Frees `count` attributes' copies. */
static void free_attributes(trl_attribute *attributes, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        trl_free_const(attributes[i].name.bytes);
        trl_free_const(attributes[i].value.bytes);
    }
}

/* Whether a name or value among `count` is null. */
static bool any_null(const char *const *names, const char *const *values, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        if (names[i] == NULL || values[i] == NULL) {
            return true;
        }
    }
    return false;
}

trl_status trl_conn_set_query_attributes(trl_conn *conn, const char *const *names, const char *const *values,
                                         size_t count) {
    if (conn == NULL || (count > 0U && (names == NULL || values == NULL)) || any_null(names, values, count)) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!allowed(conn, names, values, count)) {
        return TRL_FAILED;
    }
    trl_attribute made[TRL_MAX_ATTRIBUTES];
    memset(made, 0, sizeof made);
    for (size_t i = 0U; i < count; ++i) {
        if (!copy_slice(names[i], &made[i].name) || !copy_slice(values[i], &made[i].value)) {
            free_attributes(made, i + 1U);
            return TRL_NO_MEMORY;
        }
    }
    free_attributes(conn->attributes, conn->attribute_count);
    memcpy(conn->attributes, made, count * sizeof made[0]);
    conn->attribute_count = count;
    return TRL_OK;
}
