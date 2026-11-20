/**
 * @file frame.h
 * @brief The frame header: a message's kind and its body's length
 * [PROTOCOL Frames].
 *
 * Every message, either way, is a frame: its kind, a u8; the length of
 * its body, a u32 little-endian, the header not counted; then the body.
 * This file reads and writes the five bytes of the header alone, with no
 * I/O, so the transport and the tests share one codec.
 */
#ifndef TRL_CODEC_FRAME_H
#define TRL_CODEC_FRAME_H

#include <stddef.h>
#include <stdint.h>

/** @brief The bytes of a frame's header, kind and length [PROTOCOL Frames]. */
#define TRL_FRAME_HEADER_BYTES 5U

/**
 * @brief The longest body the client takes unless told otherwise, 16 MiB
 * [PROTOCOL Frames].
 *
 * A longer frame fails the connection, so a server cannot make the client
 * hold memory without bound.
 */
#define TRL_FRAME_MAX_BODY (16U * 1024U * 1024U)

/** @brief A frame's header, as read or to be written. */
typedef struct trl_frame_header {
    /** @brief Which message the frame holds, read in its direction. */
    uint8_t kind;
    /** @brief The bytes of the body, the header not counted. */
    uint32_t length;
} trl_frame_header;

/** @brief What reading or writing a frame header came to. */
typedef enum trl_frame_result {
    /** @brief The header was read or written. */
    TRL_FRAME_OK = 0,
    /** @brief Fewer than TRL_FRAME_HEADER_BYTES bytes were given. */
    TRL_FRAME_SHORT,
    /**
     * @brief The body is longer than the most taken, which fails the
     * connection (SQLCODE -919, SQLSTATE 08006) [PROTOCOL Frames].
     */
    TRL_FRAME_TOO_LONG,
} trl_frame_result;

/**
 * @brief Writes `header` as the first TRL_FRAME_HEADER_BYTES bytes of
 * `out`.
 *
 * @param header The kind and length to write.
 * @param out Where to write; at least `capacity` bytes.
 * @param capacity The bytes `out` holds.
 * @return TRL_FRAME_OK, or TRL_FRAME_SHORT with nothing written if
 * `capacity` is less than TRL_FRAME_HEADER_BYTES.
 */
trl_frame_result trl_frame_header_encode(trl_frame_header header, uint8_t *out, size_t capacity);

/**
 * @brief Reads a frame header from the first TRL_FRAME_HEADER_BYTES bytes
 * of `in`.
 *
 * @param in The bytes received; at least `available` bytes.
 * @param available The bytes `in` holds.
 * @param max_body The longest body taken, TRL_FRAME_MAX_BODY by default.
 * @param out Where the header read is put; left as it was unless the
 * result is TRL_FRAME_OK.
 * @return TRL_FRAME_OK; TRL_FRAME_SHORT if fewer than
 * TRL_FRAME_HEADER_BYTES bytes are available; TRL_FRAME_TOO_LONG if the
 * length is more than `max_body`.
 */
trl_frame_result trl_frame_header_decode(const uint8_t *in, size_t available, uint32_t max_body, trl_frame_header *out);

#endif
