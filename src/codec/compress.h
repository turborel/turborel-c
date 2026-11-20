/**
 * @file compress.h
 * @brief Compressed frames, kind `z`: the kind of the frame held, its
 * body's length, then a zlib stream of the body [PROTOCOL Compressed frames].
 */
#ifndef TRL_CODEC_COMPRESS_H
#define TRL_CODEC_COMPRESS_H

#include "codec/buffer.h"
#include "codec/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief The client's threshold unless told another, turborel-client's:
 * a frame shorter than this, header included, is sent as it is, as a
 * stream's header and trailer would outweigh what it saves.
 */
#define TRL_COMPRESS_THRESHOLD 1024U

/**
 * @brief Whether the library was built with zlib, which compressed frames
 * need (RULES.md D12).
 * @return Nonzero if it was.
 */
bool trl_compression_built(void);

/**
 * @brief Appends `frames`, whole frames one after another, each of at least
 * `threshold` bytes, header included, compressed where that makes it
 * shorter, the others as they are.
 * @param out Where the frames go.
 * @param frames The frames.
 * @param threshold The least whole length compressed.
 * @param error Where a failure is recorded, as zlib not built in.
 * @return Nonzero if appended.
 */
bool trl_compress_frames(trl_buffer *out, trl_slice frames, size_t threshold, trl_codec_error *error);

/**
 * @brief The frame a compressed frame's body holds: its kind, and its body
 * inflated to exactly the length it declares, never further.
 * @param body The `z` frame's body.
 * @param max_body The longest body taken.
 * @param kind The kind of the frame held.
 * @param out Where the body inflated goes, appended.
 * @param error Where a failure is recorded: a `z` within a `z`, a length
 * past `max_body`, a stream not of RFC 1950, or one that inflates to
 * another length, fails the connection.
 * @return Nonzero if inflated.
 */
bool trl_decompress_frame(trl_slice body, uint32_t max_body, uint8_t *kind, trl_buffer *out, trl_codec_error *error);

#endif
