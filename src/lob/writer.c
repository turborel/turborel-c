/**
 * @file writer.c
 * @brief A new large object written a piece at a time
 * [CLIENT Large objects]: made by a Lob create, sent in Appends of a
 * chunk, a character one's cut where a character ends, its octets checked
 * as UTF-8 by the server as each Append comes [PROTOCOL Lob 'L'], and
 * passed as a parameter by its locator.
 */
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "conn/conn.h"
#include "failure.h"
#include "lob/lob.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The bits of a UTF-8 continuation byte, and what they are. */
#define TRL_CONTINUATION_MASK 0xC0U
#define TRL_CONTINUATION 0x80U
/* The most octets of one UTF-8 character, and the lead bytes' widths. */
#define TRL_UTF8_MOST 4U
#define TRL_UTF8_THREE 3U
#define TRL_UTF8_TWO 2U
#define TRL_LEAD_TWO 0xC0U
#define TRL_LEAD_THREE 0xE0U
#define TRL_LEAD_FOUR 0xF0U
#define TRL_LEAD_FIVE 0xF8U

/* A writer: its connection, its large object's locator and kind, what is
   written and not yet sent, and its lengths. */
struct trl_lob_writer {
    trl_conn *conn;
    bool character;
    bool finished;
    uint32_t locator;
    trl_buffer pending;
    uint64_t octets;
    uint64_t characters;
};

/* Whether `byte` continues a UTF-8 character. */
static bool continues(uint8_t byte) {
    return (byte & TRL_CONTINUATION_MASK) == TRL_CONTINUATION;
}

/* The octets of the character led by `lead`. */
static size_t width_of(uint8_t lead) {
    if ((lead & TRL_LEAD_FIVE) == TRL_LEAD_FOUR) {
        return TRL_UTF8_MOST;
    }
    if ((lead & TRL_LEAD_FOUR) == TRL_LEAD_THREE) {
        return TRL_UTF8_THREE;
    }
    return (lead & TRL_LEAD_THREE) == TRL_LEAD_TWO ? TRL_UTF8_TWO : 1U;
}

/* The longest prefix of `length` octets, at most `most`, of whole UTF-8
   characters, as Turborel cuts a chunk of a character large object: a
   character the end splits left out, its rest yet to come. */
static size_t char_cut(const uint8_t *bytes, size_t length, size_t most) {
    size_t cut = most < length ? most : length;
    if (cut < length) {
        while (cut > 0U && continues(bytes[cut])) {
            --cut;
        }
        return cut;
    }
    for (size_t back = 1U; back <= TRL_UTF8_MOST && back <= cut; ++back) {
        size_t lead = cut - back;
        if (!continues(bytes[lead])) {
            return lead + width_of(bytes[lead]) > cut ? lead : cut;
        }
    }
    return cut;
}

trl_status trl_lob_writer_new(trl_conn *conn, bool character, trl_lob_writer **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_lob_writer *writer = trl_calloc(1U, sizeof *writer);
    if (writer == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_lob_request request;
    memset(&request, 0, sizeof request);
    request.op = TRL_LOB_CREATE;
    request.kind = (uint8_t)(character ? TRL_LOB_CHARACTER : TRL_LOB_BINARY);
    trl_reply reply;
    if (!trl_lob_ask(conn, &request, &reply)) {
        free(writer);
        return TRL_FAILED;
    }
    if (reply.as.lob.what != TRL_LOB_LOCATOR) {
        free(writer);
        trl_conn_lose_by(conn, "the server sent data for a Lob create");
        return TRL_FAILED;
    }
    writer->conn = conn;
    writer->character = character;
    writer->locator = reply.as.lob.locator;
    trl_buffer_init(&writer->pending);
    *out = writer;
    return TRL_OK;
}

/* Sends the first `count` octets pending as an Append, and keeps the rest. */
static bool append(trl_lob_writer *writer, size_t count) {
    trl_lob_request request;
    memset(&request, 0, sizeof request);
    request.op = TRL_LOB_APPEND;
    request.locator = writer->locator;
    request.bytes.bytes = writer->pending.data;
    request.bytes.length = count;
    if (!trl_lob_completed(writer->conn, &request)) {
        return false;
    }
    memmove(writer->pending.data, writer->pending.data + count, writer->pending.length - count);
    writer->pending.length -= count;
    return true;
}

/* Sends each whole chunk pending, a character one's cut where a character
   ends, and keeps the rest. */
static bool chunks_sent(trl_lob_writer *writer) {
    while (writer->pending.length >= TRL_LOB_CHUNK_BYTES) {
        size_t cut = writer->character ? char_cut(writer->pending.data, writer->pending.length, TRL_LOB_CHUNK_BYTES)
                                       : TRL_LOB_CHUNK_BYTES;
        if (!append(writer, cut == 0U ? 1U : cut)) {
            return false;
        }
    }
    return true;
}

trl_status trl_lob_write(trl_lob_writer *writer, const uint8_t *bytes, size_t length) {
    if (writer == NULL || (bytes == NULL && length > 0U) || writer->finished) {
        return TRL_INVALID_ARGUMENT;
    }
    if (length == 0U) {
        return TRL_OK;
    }
    trl_put_raw(&writer->pending, bytes, length);
    if (writer->pending.state != TRL_BUFFER_OK) {
        trl_failure_no_memory(&writer->conn->failure);
        return TRL_FAILED;
    }
    writer->octets += length;
    for (size_t i = 0U; writer->character && i < length; ++i) {
        writer->characters += continues(bytes[i]) ? 0U : 1U;
    }
    return chunks_sent(writer) ? TRL_OK : TRL_FAILED;
}

trl_status trl_lob_writer_finish(trl_lob_writer *writer, trl_params *params) {
    if (writer == NULL || params == NULL || writer->finished) {
        return TRL_INVALID_ARGUMENT;
    }
    writer->finished = true;
    if (writer->pending.length > 0U && !append(writer, writer->pending.length)) {
        return TRL_FAILED;
    }
    trl_value located;
    memset(&located, 0, sizeof located);
    located.tag = TRL_TAG_LOB_LOCATOR;
    located.as.locator.kind = (uint8_t)(writer->character ? TRL_LOB_CHARACTER : TRL_LOB_BINARY);
    located.as.locator.locator = writer->locator;
    located.as.locator.octets = writer->octets;
    located.as.locator.characters = writer->characters;
    return trl_params_add_value(params, &located);
}

void trl_lob_writer_free(trl_lob_writer *writer) {
    if (writer == NULL) {
        return;
    }
    trl_buffer_free(&writer->pending);
    free(writer);
}
