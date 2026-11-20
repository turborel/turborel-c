/**
 * @file native.c
 * @brief Native rows read back [CLIENT Copying and exporting]: the frames
 * an export wrote, a Description, Rows and a Complete, decoded as replies
 * are, every value as it was, of whatever type.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "failure.h"
#include "format.h"
#include "stmt/result.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A reading of native rows: its reader. */
typedef struct reading {
    trl_native_read read;
    void *context;
} reading;

/* Fails the result: the input is not native rows, and why. */
static bool not_native(trl_result *result, const char *why) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "not native rows: %s", why);
    trl_failure_broken(&result->failure, message);
    result->failed = true;
    return false;
}

/* Reads exactly `length` bytes into `into`. */
static bool read_exactly(reading *r, uint8_t *into, size_t length) {
    size_t got = 0U;
    while (got < length) {
        size_t n = r->read(r->context, into + got, length - got);
        if (n == 0U || n > length - got) {
            return false;
        }
        got += n;
    }
    return true;
}

/* Reads the next frame and decodes it into `out`, in the result's arena. */
static bool next(reading *r, trl_result *result, trl_reply *out) {
    uint8_t head[TRL_FRAME_HEADER_BYTES];
    if (!read_exactly(r, head, sizeof head)) {
        return not_native(result, "the input ends before a frame");
    }
    trl_frame_header header = {0U, 0U};
    if (trl_frame_header_decode(head, sizeof head, TRL_FRAME_MAX_BODY, &header) != TRL_FRAME_OK) {
        return not_native(result, "a frame is longer than the protocol's longest");
    }
    uint8_t *body = header.length == 0U ? NULL : trl_arena_alloc(&result->arena, header.length, 1U);
    if (header.length > 0U && body == NULL) {
        trl_failure_no_memory(&result->failure);
        result->failed = true;
        return false;
    }
    if (header.length > 0U && !read_exactly(r, body, header.length)) {
        return not_native(result, "the input ends within a frame");
    }
    trl_codec_error error = {0, "", ""};
    trl_slice bytes = {body, header.length};
    if (!trl_decode_reply(header.kind, bytes, &result->arena, out, &error)) {
        trl_failure_from_codec(&result->failure, &error);
        result->failed = true;
        return false;
    }
    return true;
}

/* Reads the frames after the Description: Rows, to the Complete. */
static bool rows(reading *r, trl_result *result) {
    for (;;) {
        trl_reply reply;
        if (!next(r, result, &reply)) {
            return false;
        }
        if (reply.kind == TRL_KIND_COMPLETE) {
            trl_result_complete(result, &reply.as.complete);
            return true;
        }
        if (reply.kind != TRL_KIND_ROWS) {
            return not_native(result, "a frame other than Rows before the Complete");
        }
        if (!trl_result_add_rows(result, reply.as.rows.rows, reply.as.rows.count)) {
            trl_failure_no_memory(&result->failure);
            result->failed = true;
            return false;
        }
    }
}

trl_status trl_read_native(trl_native_read read, void *context, trl_result **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (read == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_result *result = trl_result_make();
    if (result == NULL) {
        return TRL_NO_MEMORY;
    }
    reading r = {read, context};
    trl_reply first;
    bool read_all = next(&r, result, &first);
    if (read_all && first.kind != TRL_KIND_DESCRIPTION) {
        read_all = not_native(result, "no Description first");
    }
    if (read_all && !trl_result_take_columns(result, first.as.description.columns, first.as.description.count)) {
        trl_failure_no_memory(&result->failure);
        result->failed = true;
        read_all = false;
    }
    read_all = read_all && rows(&r, result);
    *out = result;
    return read_all ? TRL_OK : TRL_FAILED;
}
