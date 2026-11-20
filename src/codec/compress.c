/**
 * @file compress.c
 * @brief Compressed frames through zlib (RULES.md D12), as
 * turborel-protocol's `compress.rs` makes and reads them
 * [PROTOCOL Compressed frames]: a frame compressed only where it comes out
 * shorter, and one received inflated no further than it declares, so a
 * stream cannot make the client hold more than the longest body it takes.
 */
#include "codec/compress.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/reader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef TRL_HAVE_ZLIB
/* zlib's input pointers are const, as nothing here is written through them. */
#define ZLIB_CONST
#include <zconf.h>
#include <zlib.h>
#endif

/* The bytes of a compressed frame's body before its stream: the kind held
   and its length. */
#define TRL_COMPRESS_PREFIX 5U

bool trl_compression_built(void) {
#ifdef TRL_HAVE_ZLIB
    return true;
#else
    return false;
#endif
}

#ifdef TRL_HAVE_ZLIB

/* Appends `body` compressed as a `z` frame holding a frame of `kind`, if
   that is shorter than the frame as it is; else nothing, and false. */
static bool put_compressed(trl_buffer *out, uint8_t kind, trl_slice body, trl_codec_error *error) {
    uLong bound = compressBound((uLong)body.length);
    size_t start = trl_frame_start(out, TRL_KIND_COMPRESSED);
    trl_put_u8(out, kind);
    trl_put_u32(out, (uint32_t)body.length);
    size_t at = out->length;
    if (trl_put_space(out, bound) == NULL) {
        return trl_buffer_ok(out, error);
    }
    uLongf written = bound;
    if (compress(out->data + at, &written, body.bytes, (uLong)body.length) != Z_OK ||
        written + TRL_COMPRESS_PREFIX >= body.length) {
        out->length = start;
        return false;
    }
    out->length = at + written;
    trl_frame_finish(out, start);
    return true;
}

bool trl_compress_frames(trl_buffer *out, trl_slice frames, size_t threshold, trl_codec_error *error) {
    size_t at = 0U;
    while (frames.length - at >= TRL_FRAME_HEADER_BYTES) {
        trl_frame_header header = {0U, 0U};
        (void)trl_frame_header_decode(frames.bytes + at, frames.length - at, UINT32_MAX, &header);
        size_t rest = frames.length - at - TRL_FRAME_HEADER_BYTES;
        size_t length = header.length < rest ? header.length : rest;
        size_t whole = TRL_FRAME_HEADER_BYTES + length;
        trl_slice body = {frames.bytes + at + TRL_FRAME_HEADER_BYTES, length};
        if (whole < threshold || !put_compressed(out, header.kind, body, error)) {
            trl_put_raw(out, frames.bytes + at, whole);
        }
        at += whole;
    }
    return trl_buffer_ok(out, error);
}

/* Inflates `stream` into `out`, exactly `length` bytes, the stream ending
   there with nothing after it. */
static bool inflate_exactly(trl_slice stream, size_t length, trl_buffer *out, trl_reader *reader) {
    size_t at = out->length;
    if (trl_put_space(out, length) == NULL && length > 0U) {
        reader->failed = true;
        return trl_buffer_ok(out, reader->error);
    }
    z_stream z;
    z.zalloc = Z_NULL;
    z.zfree = Z_NULL;
    z.opaque = Z_NULL;
    z.next_in = stream.bytes;
    z.avail_in = (uInt)stream.length;
    if (inflateInit(&z) != Z_OK) {
        trl_codec_no_memory(reader->error);
        return false;
    }
    z.next_out = out->data == NULL ? NULL : out->data + at;
    z.avail_out = (uInt)length;
    int result = inflate(&z, Z_FINISH);
    bool whole = result == Z_STREAM_END && z.avail_in == 0U && z.avail_out == 0U;
    (void)inflateEnd(&z);
    if (!whole) {
        out->length = at;
        return trl_reader_fail(reader, result == Z_STREAM_END
                                           ? "a compressed frame inflates to another length than it declares"
                                           : "a compressed frame's stream is malformed, or inflates past its length");
    }
    return true;
}

bool trl_decompress_frame(trl_slice body, uint32_t max_body, uint8_t *kind, trl_buffer *out, trl_codec_error *error) {
    trl_reader reader;
    trl_reader_init(&reader, body.bytes, body.length, NULL, error);
    reader.what = "compressed frame";
    uint32_t length = 0U;
    if (!trl_read_u8(&reader, kind) || !trl_read_u32(&reader, &length)) {
        return false;
    }
    if (*kind == TRL_KIND_COMPRESSED) {
        return trl_reader_fail(&reader, "a compressed frame holds another");
    }
    if (length > max_body) {
        return trl_reader_fail(&reader, "a compressed frame's body is longer than taken");
    }
    trl_slice stream = {body.bytes + reader.at, body.length - reader.at};
    if (stream.length > UINT32_MAX) {
        return trl_reader_fail(&reader, "a compressed frame's stream is too long");
    }
    return inflate_exactly(stream, length, out, &reader);
}

#else

/* A library built without zlib refuses compressed frames, saying so. */
static bool not_built(trl_codec_error *error) {
    trl_codec_fail(error, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                   "libturborel was built without zlib, which compressed frames need");
    return false;
}

bool trl_compress_frames(trl_buffer *out, trl_slice frames, size_t threshold, trl_codec_error *error) {
    (void)out;
    (void)frames;
    (void)threshold;
    return not_built(error);
}

bool trl_decompress_frame(trl_slice body, uint32_t max_body, uint8_t *kind, trl_buffer *out, trl_codec_error *error) {
    (void)body;
    (void)max_body;
    (void)kind;
    (void)out;
    return not_built(error);
}

#endif
