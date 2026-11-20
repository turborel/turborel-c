/**
 * @file lob.c
 * @brief Large objects over a connection [CLIENT Large objects]: one a
 * result sent whole read from the value, one sent by its locator read by
 * ranges [PROTOCOL Lob 'L'], and a locator freed or held.
 */
#include "lob/lob.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "conn/conn.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bits of a UTF-8 continuation byte, and what they are. */
#define TRL_CONTINUATION_MASK 0xC0U
#define TRL_CONTINUATION 0x80U
/* The most octets of one UTF-8 character. */
#define TRL_UTF8_MOST 4U

/* Whether `byte` begins a UTF-8 character. */
static bool leads(uint8_t byte) {
    return (byte & TRL_CONTINUATION_MASK) != TRL_CONTINUATION;
}

/* The characters of `length` octets of UTF-8, each counted at its lead. */
static uint64_t characters_of(const uint8_t *bytes, size_t length) {
    uint64_t count = 0U;
    for (size_t i = 0U; i < length; ++i) {
        count += leads(bytes[i]) ? 1U : 0U;
    }
    return count;
}

trl_status trl_value_lob(const trl_value *value, trl_lob_info *out) {
    if (value == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (value->tag == TRL_TAG_LOB_LOCATOR) {
        out->character = value->as.locator.kind == TRL_LOB_CHARACTER;
        out->located = true;
        out->octets = value->as.locator.octets;
        out->characters = value->as.locator.characters;
        return TRL_OK;
    }
    if (value->tag != TRL_TAG_LOB_WHOLE) {
        return TRL_INVALID_ARGUMENT;
    }
    out->character = value->as.lob.kind == TRL_LOB_CHARACTER;
    out->located = false;
    out->octets = value->as.lob.octets.length;
    out->characters = out->character ? characters_of(value->as.lob.octets.bytes, value->as.lob.octets.length) : 0U;
    return TRL_OK;
}

void trl_lob_misused(trl_conn *conn, const char *message) {
    trl_failure_set(&conn->failure, TRL_SQLCODE_DATA, TRL_SQLSTATE_DATA, message);
}

bool trl_lob_ask(trl_conn *conn, const trl_lob_request *request, trl_reply *out) {
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_lob(&conn->out, request, &error)) {
        return trl_conn_codec_failed(conn, &error);
    }
    if (!trl_conn_send(conn) || !trl_conn_receive(conn, out)) {
        return false;
    }
    if (out->kind == TRL_KIND_LOB_REPLY) {
        return true;
    }
    if (out->kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &out->as.error);
        return false;
    }
    trl_conn_lose_by(conn, "the server sent no Lob reply");
    return false;
}

bool trl_lob_completed(trl_conn *conn, const trl_lob_request *request) {
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_lob(&conn->out, request, &error)) {
        return trl_conn_codec_failed(conn, &error);
    }
    return trl_conn_send(conn) && trl_conn_completion(conn);
}

/* Where the `n`th character of `length` octets begins, or `length`. */
static size_t character_at(const uint8_t *bytes, size_t length, uint64_t n) {
    uint64_t seen = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (leads(bytes[i])) {
            if (seen == n) {
                return i;
            }
            ++seen;
        }
    }
    return length;
}

void trl_lob_held_range(const trl_value *lob, trl_lob_range range, uint8_t *out, size_t *length) {
    const uint8_t *bytes = lob->as.lob.octets.bytes;
    size_t held = lob->as.lob.octets.length;
    size_t capacity = *length;
    size_t start = 0U;
    size_t end = 0U;
    if (range.characters && lob->as.lob.kind == TRL_LOB_CHARACTER) {
        uint64_t most = range.count < capacity / TRL_UTF8_MOST ? range.count : capacity / TRL_UTF8_MOST;
        start = character_at(bytes, held, range.from);
        end = start + character_at(bytes + start, held - start, most);
    } else {
        start = range.from < held ? (size_t)range.from : held;
        uint64_t most = range.count < capacity ? range.count : capacity;
        end = held - start < most ? held : start + (size_t)most;
    }
    if (end > start) {
        memcpy(out, bytes + start, end - start);
    }
    *length = end - start;
}

/* The most of `range` a read asks the server for, to fit `capacity`
   octets and a chunk. */
static uint64_t asked(trl_lob_range range, size_t capacity) {
    uint64_t room = range.characters ? capacity / TRL_UTF8_MOST : capacity;
    uint64_t most = room < TRL_LOB_CHUNK_BYTES ? room : TRL_LOB_CHUNK_BYTES;
    return range.count < most ? range.count : most;
}

trl_status trl_lob_read(trl_conn *conn, const trl_value *lob, trl_lob_range range, uint8_t *out, size_t *length) {
    trl_lob_info info;
    if (conn == NULL || out == NULL || length == NULL || trl_value_lob(lob, &info) != TRL_OK) {
        return TRL_INVALID_ARGUMENT;
    }
    uint64_t count = asked(range, *length);
    if (count == 0U && range.count > 0U) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!info.located) {
        trl_lob_held_range(lob, range, out, length);
        return TRL_OK;
    }
    trl_lob_request request;
    memset(&request, 0, sizeof request);
    request.op = TRL_LOB_READ;
    request.locator = lob->as.locator.locator;
    request.from = range.from;
    request.count = count;
    request.characters = range.characters;
    trl_reply reply;
    memset(&reply, 0, sizeof reply);
    if (!trl_lob_ask(conn, &request, &reply)) {
        return TRL_FAILED;
    }
    if (reply.as.lob.what != TRL_LOB_DATA || reply.as.lob.data.length > *length) {
        trl_conn_lose_by(conn, "the server sent no data for a Lob read");
        return TRL_FAILED;
    }
    if (reply.as.lob.data.length > 0U) {
        memcpy(out, reply.as.lob.data.bytes, reply.as.lob.data.length);
    }
    *length = reply.as.lob.data.length;
    return TRL_OK;
}

/* Sends `op`, free or hold, of a large object's locator. */
static trl_status of_locator(trl_conn *conn, const trl_value *lob, uint8_t op) {
    trl_lob_info info;
    if (conn == NULL || trl_value_lob(lob, &info) != TRL_OK) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!info.located) {
        trl_lob_misused(conn, "the large object was sent whole: it has no locator");
        return TRL_FAILED;
    }
    trl_lob_request request;
    memset(&request, 0, sizeof request);
    request.op = op;
    request.locator = lob->as.locator.locator;
    return trl_lob_completed(conn, &request) ? TRL_OK : TRL_FAILED;
}

trl_status trl_lob_free_locator(trl_conn *conn, const trl_value *lob) {
    return of_locator(conn, lob, TRL_LOB_FREE);
}

trl_status trl_lob_hold(trl_conn *conn, const trl_value *lob) {
    return of_locator(conn, lob, TRL_LOB_HOLD);
}
