/**
 * @file script.c
 * @brief A server of a test's own, over a program's stream.
 */
#include "script.h"

#include "codec/buffer.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The server's product, as a script tells it. */
#define SCRIPT_SERVER "turborel 0.0.0 (alpha)"
#define KEY_BYTES 16U
#define KEY_BYTE 7U
#define VARYING_LENGTH 20U

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

static bool script_bound(void *context, uint32_t ms) {
    script *s = context;
    s->bound_ms = ms;
    s->bounded = true;
    return true;
}

trl_stream script_stream(script *s) {
    trl_stream stream = {s, script_read, script_write, script_close, s->bounds ? script_bound : NULL};
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

/* Puts a column of CHARACTER VARYING(20), named C<n>. */
static void put_column(trl_buffer *body, uint32_t n) {
    char name[16];
    (void)snprintf(name, sizeof name, "C%u", (unsigned int)n);
    trl_put_flag(body, true);
    script_put_string(body, name);
    trl_put_u8(body, TRL_TAG_VARYING);
    trl_put_u32(body, VARYING_LENGTH);
    trl_put_u8(body, 0U);
    trl_put_flag(body, false);
}

void script_description(script *s, uint32_t count) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, count);
    for (uint32_t i = 0U; i < count; ++i) {
        put_column(&body, i);
    }
    script_reply(s, TRL_KIND_DESCRIPTION, &body);
    trl_buffer_free(&body);
}

void script_rows(script *s, uint32_t rows, uint32_t count) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, rows);
    for (uint32_t r = 0U; r < rows; ++r) {
        trl_put_u32(&body, count);
        for (uint32_t i = 0U; i < count; ++i) {
            trl_put_u8(&body, TRL_TAG_CHARACTER);
            script_put_string(&body, "v");
        }
    }
    script_reply(s, TRL_KIND_ROWS, &body);
    trl_buffer_free(&body);
}

void script_complete(script *s, int32_t sqlcode, const char *sqlstate, bool active, bool more) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, (uint32_t)sqlcode);
    script_put_string(&body, sqlstate);
    trl_put_flag(&body, active);
    if (more) {
        trl_put_u8(&body, TRL_TAIL_MORE);
    }
    script_reply(s, TRL_KIND_COMPLETE, &body);
    trl_buffer_free(&body);
}

void script_prepared(script *s, uint32_t number, uint32_t parameters, uint32_t columns) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, number);
    trl_put_u32(&body, parameters);
    for (uint32_t i = 0U; i < parameters; ++i) {
        trl_put_flag(&body, false);
        trl_put_u8(&body, TRL_TAG_VARYING);
        trl_put_u32(&body, VARYING_LENGTH);
        trl_put_u8(&body, 0U);
    }
    trl_put_u32(&body, columns);
    for (uint32_t i = 0U; i < columns; ++i) {
        put_column(&body, i);
    }
    script_reply(s, TRL_KIND_PREPARED, &body);
    trl_buffer_free(&body);
}

void script_notice(script *s, const char *sqlstate, const char *message) {
    trl_buffer body;
    trl_buffer_init(&body);
    script_put_string(&body, sqlstate);
    script_put_string(&body, message);
    script_reply(s, TRL_KIND_NOTICE, &body);
    trl_buffer_free(&body);
}

trl_status script_connect(script *s, trl_mechanism *mechanism, const trl_conn_options *options, trl_conn **out) {
    trl_stream stream = script_stream(s);
    return trl_connect_stream(&stream, "ann", mechanism, options, out);
}

/* Writes a Complete of every tail a completion may carry. */
void script_complete_tails(script *s) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    script_put_string(&body, "01003");
    trl_put_flag(&body, true);
    trl_put_u8(&body, TRL_TAIL_POSITION);
    trl_put_u64(&body, 42U);
    trl_put_u8(&body, TRL_TAIL_ROWS);
    trl_put_u64(&body, 2U);
    trl_put_u8(&body, TRL_TAIL_GENERATED);
    trl_put_u32(&body, 2U);
    script_put_string(&body, "7");
    script_put_string(&body, "8");
    trl_put_u8(&body, TRL_TAIL_CONDITIONS);
    trl_buffer failure;
    trl_buffer_init(&failure);
    trl_put_u32(&failure, 0U);
    script_put_string(&failure, "01003");
    script_put_string(&failure, "null eliminated");
    trl_put_flag(&failure, false);
    trl_put_flag(&failure, false);
    trl_put_flag(&failure, true);
    trl_put_u32(&body, 1U);
    trl_slice bytes = {failure.data, failure.length};
    trl_put_bytes(&body, bytes);
    trl_buffer_free(&failure);
    trl_put_u8(&body, TRL_TAIL_TIME);
    trl_put_u64(&body, 30U);
    trl_put_u64(&body, 2U);
    trl_put_u64(&body, 1U);
    script_reply(s, TRL_KIND_COMPLETE, &body);
    trl_buffer_free(&body);
}

void script_declared(script *s, uint8_t kind) {
    static const char *const told[] = {"INTEGER", "INTEGER", "USER-DEFINED", "S.MONEY"};
    trl_buffer body;
    trl_buffer_init(&body);
    if (kind == TRL_KIND_PREPARED) {
        trl_put_u32(&body, 0U);
        trl_put_u32(&body, 0U);
    }
    trl_put_u32(&body, 2U);
    for (uint32_t i = 0U; i < 2U; ++i) {
        trl_put_flag(&body, false);
        trl_put_u8(&body, TRL_TAG_EXACT);
        trl_put_u32(&body, i == 0U ? 10U : 9U);
        trl_put_u32(&body, i == 0U ? 0U : 2U);
        trl_put_flag(&body, false);
    }
    trl_put_u8(&body, TRL_TAIL_DECLARED);
    trl_put_u32(&body, 2U);
    for (size_t i = 0U; i < sizeof told / sizeof told[0]; ++i) {
        script_put_string(&body, told[i]);
    }
    script_reply(s, kind, &body);
    trl_buffer_free(&body);
}

void script_lob_data(script *s, const uint8_t *bytes, size_t length) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u8(&body, TRL_LOB_DATA);
    trl_slice data = {bytes, length};
    trl_put_bytes(&body, data);
    script_reply(s, TRL_KIND_LOB_REPLY, &body);
    trl_buffer_free(&body);
}

void script_lob_locator(script *s, uint32_t locator) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u8(&body, TRL_LOB_LOCATOR);
    trl_put_u32(&body, locator);
    script_reply(s, TRL_KIND_LOB_REPLY, &body);
    trl_buffer_free(&body);
}
