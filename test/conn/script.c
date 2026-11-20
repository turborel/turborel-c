/**
 * @file script.c
 * @brief A server of a test's own, over a program's stream.
 */
#include "script.h"

#include "codec/buffer.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The server's product, as a script tells it. */
#define SCRIPT_SERVER "turborel 0.0.0 (alpha)"
#define KEY_BYTES 16U
#define KEY_BYTE 7U

void script_init(script *s, script_respond respond, void *data) {
    memset(s, 0, sizeof *s);
    trl_buffer_init(&s->to_client);
    trl_buffer_init(&s->from_client);
    s->respond = respond;
    s->data = data;
}

void script_free(script *s) {
    trl_buffer_free(&s->to_client);
    trl_buffer_free(&s->from_client);
}

static int64_t script_read(void *context, uint8_t *into, size_t capacity) {
    script *s = context;
    size_t ready = s->to_client.length - s->read_at;
    if (ready == 0U) {
        return s->fail_reads ? -1 : 0;
    }
    size_t step = ready < capacity ? ready : capacity;
    memcpy(into, s->to_client.data + s->read_at, step);
    s->read_at += step;
    return (int64_t)step;
}

/* Hands each request written whole to the test's function. */
static void hand_on(script *s) {
    while (s->from_client.length - s->handed >= TRL_FRAME_HEADER_BYTES) {
        trl_frame_header header = {0U, 0U};
        const uint8_t *at = s->from_client.data + s->handed;
        if (trl_frame_header_decode(at, TRL_FRAME_HEADER_BYTES, TRL_FRAME_MAX_BODY, &header) != TRL_FRAME_OK ||
            s->from_client.length - s->handed - TRL_FRAME_HEADER_BYTES < header.length) {
            return;
        }
        if (s->requests < SCRIPT_REQUESTS) {
            s->kinds[s->requests++] = header.kind;
        }
        trl_slice body = {at + TRL_FRAME_HEADER_BYTES, header.length};
        s->handed += TRL_FRAME_HEADER_BYTES + header.length;
        if (s->respond != NULL) {
            s->respond(s, header.kind, body);
        }
    }
}

static int64_t script_write(void *context, const uint8_t *bytes, size_t length) {
    script *s = context;
    if (s->fail_writes) {
        return -1;
    }
    trl_put_raw(&s->from_client, bytes, length);
    hand_on(s);
    return (int64_t)length;
}

static void script_close(void *context) {
    script *s = context;
    s->closed = true;
}

trl_stream script_stream(script *s) {
    trl_stream stream = {s, script_read, script_write, script_close};
    return stream;
}

void script_reply(script *s, uint8_t kind, const trl_buffer *body) {
    size_t start = trl_frame_start(&s->to_client, kind);
    trl_put_raw(&s->to_client, body->data, body->length);
    trl_frame_finish(&s->to_client, start);
}

void script_put_string(trl_buffer *body, const char *text) {
    trl_slice bytes = {(const uint8_t *)text, strlen(text)};
    trl_put_bytes(body, bytes);
}

void script_challenge(script *s, const uint8_t *bytes, size_t length) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_slice challenge = {bytes, length};
    trl_put_bytes(&body, challenge);
    script_reply(s, TRL_KIND_CHALLENGE, &body);
    trl_buffer_free(&body);
}

void script_ready(script *s, const char *authorization, const char *const *capabilities, size_t count) {
    trl_buffer body;
    trl_buffer_init(&body);
    script_put_string(&body, authorization);
    for (size_t i = 0U; i < KEY_BYTES; ++i) {
        trl_put_u8(&body, KEY_BYTE);
    }
    trl_put_u32(&body, (uint32_t)count);
    for (size_t i = 0U; i < count; ++i) {
        script_put_string(&body, capabilities[i]);
    }
    script_put_string(&body, SCRIPT_SERVER);
    script_reply(s, TRL_KIND_READY, &body);
    trl_buffer_free(&body);
}

void script_error(script *s, int32_t sqlcode, const char *sqlstate, const char *message, const char *rule) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, (uint32_t)sqlcode);
    script_put_string(&body, sqlstate);
    script_put_string(&body, message);
    trl_put_flag(&body, rule != NULL);
    if (rule != NULL) {
        script_put_string(&body, rule);
    }
    trl_put_flag(&body, false);
    trl_put_flag(&body, false);
    script_reply(s, TRL_KIND_ERROR, &body);
    trl_buffer_free(&body);
}

void script_status(script *s, const char *name, const char *value) {
    trl_buffer body;
    trl_buffer_init(&body);
    script_put_string(&body, name);
    script_put_string(&body, value);
    script_reply(s, TRL_KIND_STATUS, &body);
    trl_buffer_free(&body);
}

void script_bare(script *s, uint8_t kind) {
    trl_buffer body;
    trl_buffer_init(&body);
    script_reply(s, kind, &body);
    trl_buffer_free(&body);
}

trl_status script_connect(script *s, trl_mechanism *mechanism, const trl_conn_options *options, trl_conn **out) {
    trl_stream stream = script_stream(s);
    return trl_connect_stream(&stream, "ann", mechanism, options, out);
}
