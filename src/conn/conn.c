/**
 * @file conn.c
 * @brief Connections [CLIENT Connecting] [PROTOCOL Opening a connection]:
 * opened to a target or over a program's stream, logged in by Hello,
 * Challenge and Answer until Ready or Error, bounded by the connect
 * timeout until Ready and by the read timeout after [CLIENT Timeouts].
 */
#include "conn/conn.h"

#include "auth/mechanism.h"
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "codec/tail.h"
#include "conn/options.h"
#include "conn/target.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "net/tls.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Makes a connection over nothing yet; null if memory cannot be had. */
static trl_conn *unopened(void) {
    trl_conn *conn = trl_calloc(1U, sizeof *conn);
    uint8_t *input = trl_malloc(TRL_READ_BUFFER_BYTES);
    if (conn == NULL || input == NULL) {
        free(conn);
        free(input);
        return NULL;
    }
    conn->input = input;
    trl_failure_init(&conn->failure);
    trl_failure_init(&conn->lost_by);
    trl_buffer_init(&conn->body);
    trl_buffer_init(&conn->inflated);
    trl_buffer_init(&conn->out);
    trl_buffer_init(&conn->compressed_out);
    trl_arena_init(&conn->arena);
    conn->max_body = TRL_MAX_BODY_BYTES;
    return conn;
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

/* Fails as the codec failed. */
static bool codec_failed(trl_conn *conn, const trl_codec_error *error) {
    trl_failure_from_codec(&conn->failure, error);
    return false;
}

/* Sends Hello: the version, the user, the mechanism's name and first
   bytes, the capabilities asked for, and the connection's attributes. */
static bool send_hello(trl_conn *conn, const char *user, trl_mechanism *mechanism, const trl_conn_options *options) {
    trl_buffer initial;
    trl_buffer_init(&initial);
    trl_slice *asked = trl_calloc(options->capability_count + 1U, sizeof *asked);
    trl_attribute attributes[TRL_MAX_ATTRIBUTES];
    const char *name = trl_mechanism_name(mechanism);
    if (asked == NULL || !trl_mechanism_initial(mechanism, &initial, &conn->failure)) {
        if (asked == NULL) {
            trl_failure_no_memory(&conn->failure);
        }
        free((void *)asked);
        trl_buffer_free(&initial);
        return false;
    }
    size_t count = trl_conn_options_capabilities(options, asked, trl_compression_built());
    trl_conn_options_attributes(options, attributes);
    trl_hello hello = {
        TRL_PROTOCOL_VERSION,
        {(const uint8_t *)user, strlen(user)},
        {(const uint8_t *)name, strlen(name)},
        {initial.data, initial.length},
        asked,
        count,
        attributes,
        options->attribute_count,
    };
    trl_codec_error error = {0, "", ""};
    bool encoded = trl_encode_hello(&conn->out, &hello, &error);
    trl_buffer_free(&initial);
    free((void *)asked);
    if (!encoded) {
        return codec_failed(conn, &error);
    }
    return trl_conn_send(conn);
}

/* Answers a Challenge as the mechanism says. */
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
    return encoded ? trl_conn_send(conn) : codec_failed(conn, &error);
}

/* Keeps what Ready told: the authorization identifier, the cancel key,
   the capabilities granted, and the server's product. */
static bool ready(trl_conn *conn, const trl_reply *reply, size_t compress_from) {
    conn->authorization = copy_of(reply->as.ready.authorization);
    conn->server = copy_of(reply->as.ready.server);
    const trl_strings *granted = &reply->as.ready.capabilities;
    conn->granted = (char **)trl_calloc((size_t)granted->count + 1U, sizeof(char *));
    if (conn->authorization == NULL || conn->server == NULL || conn->granted == NULL) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    for (uint32_t i = 0U; i < granted->count; ++i) {
        conn->granted[i] = copy_of(granted->items[i]);
        if (conn->granted[i] == NULL) {
            trl_failure_no_memory(&conn->failure);
            return false;
        }
        conn->granted_count = (size_t)i + 1U;
        conn->compressed = conn->compressed || strcmp(conn->granted[i], TRL_CAPABILITY_COMPRESSION) == 0;
    }
    if (reply->as.ready.key.length == sizeof conn->key) {
        memcpy(conn->key, reply->as.ready.key.bytes, sizeof conn->key);
        conn->has_key = true;
    }
    conn->compress_from = compress_from;
    return true;
}

/* Logs in: Hello, then each Challenge answered, until Ready or Error. */
static bool log_in(trl_conn *conn, const char *user, trl_mechanism *mechanism, const trl_conn_options *options) {
    if (!send_hello(conn, user, mechanism, options)) {
        return false;
    }
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
            return trl_mechanism_ready(mechanism, &conn->failure) && ready(conn, &reply, options->compress_from);
        case TRL_KIND_ERROR:
            trl_mechanism_refused(mechanism);
            trl_failure_from_wire(&conn->failure, &reply.as.error);
            return false;
        default:
            trl_conn_lose_by(conn, "the server sent no Ready");
            return false;
        }
    }
}

/* Bounds each read and write by `ms`, as the transport allows. */
static bool bound(trl_conn *conn, uint32_t ms) {
    if (conn->transport.ops->bound == NULL || conn->transport.ops->bound(&conn->transport, ms)) {
        return true;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "cannot bound how long the connection waits: %s", conn->transport.error.text);
    trl_failure_broken(&conn->failure, message);
    return false;
}

/* Ends a connect: TRL_OK, or the connection lost and closed, holding why. */
static trl_status opened(trl_conn *conn, bool logged_in, trl_conn **out) {
    *out = conn;
    if (logged_in) {
        return TRL_OK;
    }
    trl_conn_lose(conn, &conn->failure);
    trl_transport_close(&conn->transport);
    return TRL_FAILED;
}

/* Refuses a secret over plain TCP, before connecting (SQLSTATE 28000). */
static bool in_the_clear(trl_conn *conn, const trl_target *target, const trl_mechanism *mechanism) {
    if (!trl_target_is_plain_tcp(target) || !trl_mechanism_secret(mechanism)) {
        return true;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "%s is sent only over TLS, a Unix socket or a named pipe, never over plain TCP",
               trl_mechanism_name(mechanism));
    trl_failure_set(&conn->failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_INVALID_AUTHORIZATION, message);
    return false;
}

/* Opens the target, binds SCRAM to a TLS channel, and logs in. */
static bool open_and_log_in(trl_conn *conn, const trl_target *target, const char *user, trl_mechanism *mechanism,
                            const trl_conn_options *options) {
    trl_tls_channel channel;
    if (!trl_target_open(target, options->connect_ms, &conn->transport, &channel, &conn->failure)) {
        return false;
    }
    if (channel.secured) {
        (void)trl_mechanism_bind(mechanism, channel.certificate, channel.certificate_length,
                                 channel.has_exporter ? channel.exporter : NULL,
                                 channel.has_exporter ? sizeof channel.exporter : 0U);
    }
    trl_tls_channel_free(&channel);
    conn->read_ms = options->read_ms;
    return log_in(conn, user, mechanism, options) && bound(conn, options->read_ms);
}

trl_status trl_connect(const trl_target *target, const char *user, trl_mechanism *mechanism,
                       const trl_conn_options *options, trl_conn **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (target == NULL || user == NULL || mechanism == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_conn_options *defaults = NULL;
    if (options == NULL && trl_conn_options_new(&defaults) != TRL_OK) {
        return TRL_NO_MEMORY;
    }
    trl_conn *conn = unopened();
    if (conn == NULL) {
        trl_conn_options_free(defaults);
        return TRL_NO_MEMORY;
    }
    conn->reach = trl_target_clone(target);
    bool logged_in = false;
    if (conn->reach == NULL) {
        trl_failure_no_memory(&conn->failure);
    } else if (in_the_clear(conn, target, mechanism)) {
        logged_in = open_and_log_in(conn, target, user, mechanism, options == NULL ? defaults : options);
    }
    trl_conn_options_free(defaults);
    return opened(conn, logged_in, out);
}

/* A program's stream: its functions, which the transport calls. */
static int64_t stream_read(trl_transport *transport, uint8_t *into, size_t capacity) {
    const trl_stream *stream = transport->state;
    int64_t got = stream->read(stream->context, into, capacity);
    if (got < 0) {
        trl_io_error_set(&transport->error, "the program's stream failed to read", false);
        return -1;
    }
    return got;
}

static int64_t stream_write(trl_transport *transport, const uint8_t *bytes, size_t length) {
    const trl_stream *stream = transport->state;
    int64_t wrote = stream->write(stream->context, bytes, length);
    if (wrote <= 0) {
        trl_io_error_set(&transport->error, "the program's stream failed to write", false);
        return -1;
    }
    return wrote;
}

static void stream_close(trl_transport *transport) {
    trl_stream *stream = transport->state;
    if (stream->close != NULL) {
        stream->close(stream->context);
    }
    free(stream);
}

static const trl_transport_ops stream_ops = {stream_read, stream_write, NULL, stream_close};

trl_status trl_connect_stream(const trl_stream *stream, const char *user, trl_mechanism *mechanism,
                              const trl_conn_options *options, trl_conn **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (stream == NULL || stream->read == NULL || stream->write == NULL || user == NULL || mechanism == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_conn_options *defaults = NULL;
    trl_stream *held = trl_malloc(sizeof *held);
    trl_conn *conn = held == NULL ? NULL : unopened();
    if (conn == NULL || (options == NULL && trl_conn_options_new(&defaults) != TRL_OK)) {
        free(held);
        trl_conn_free(conn);
        if (stream->close != NULL) {
            stream->close(stream->context);
        }
        return TRL_NO_MEMORY;
    }
    *held = *stream;
    conn->transport.ops = &stream_ops;
    conn->transport.state = held;
    bool logged_in = log_in(conn, user, mechanism, options == NULL ? defaults : options);
    trl_conn_options_free(defaults);
    return opened(conn, logged_in, out);
}
