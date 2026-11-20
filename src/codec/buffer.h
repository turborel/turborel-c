/**
 * @file buffer.h
 * @brief A growing buffer that requests are written into, field by field,
 * little-endian [PROTOCOL Values and fields].
 *
 * A write that cannot be made, for want of memory or because a field is
 * longer than its length's u32 holds, marks the buffer failed, and every
 * later write does nothing; the caller tests the buffer once, at the end.
 */
#ifndef TRL_CODEC_BUFFER_H
#define TRL_CODEC_BUFFER_H

#include "codec/error.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The bytes of a `u32` [PROTOCOL Values and fields]. */
#define TRL_U32_BYTES 4U
/** @brief The bytes of a `u64` [PROTOCOL Values and fields]. */
#define TRL_U64_BYTES 8U
/** @brief The bytes of a key, with no length before them [PROTOCOL Values and fields]. */
#define TRL_KEY_BYTES 16U

/** @brief Bytes that go on the wire as they are: a view, not owned. */
typedef struct trl_slice {
    /** @brief The first byte; may be null if `length` is 0. */
    const uint8_t *bytes;
    /** @brief How many. */
    size_t length;
} trl_slice;

/** @brief Why a buffer stopped taking writes. */
typedef enum trl_buffer_state {
    /** @brief Every write was made. */
    TRL_BUFFER_OK = 0,
    /** @brief Memory could not be had. */
    TRL_BUFFER_NO_MEMORY,
    /** @brief A field, or a frame's body, is longer than its u32 length holds. */
    TRL_BUFFER_TOO_LONG,
} trl_buffer_state;

/** @brief A buffer; it owns its bytes. */
typedef struct trl_buffer {
    /** @brief The bytes written. */
    uint8_t *data;
    /** @brief How many were written. */
    size_t length;
    /** @brief How many `data` holds. */
    size_t capacity;
    /** @brief Whether every write was made. */
    trl_buffer_state state;
    /**
     * @brief The most bytes it may hold, 0 for no bound: past it a write
     * fails as memory not had, so what a buffer takes is bounded.
     */
    size_t limit;
} trl_buffer;

/**
 * @brief Readies an empty buffer.
 * @param buffer The buffer.
 */
void trl_buffer_init(trl_buffer *buffer);

/**
 * @brief Frees the bytes of `buffer`, which is then empty.
 * @param buffer The buffer; nothing is done if null.
 */
void trl_buffer_free(trl_buffer *buffer);

/**
 * @brief Records the buffer's failure, if any, in `error`.
 * @param buffer The buffer.
 * @param error Where a failure is recorded.
 * @return Nonzero if every write was made.
 */
bool trl_buffer_ok(const trl_buffer *buffer, trl_codec_error *error);

/**
 * @brief Appends `length` bytes as they are.
 * @param buffer The buffer.
 * @param bytes The bytes; may be null if `length` is 0.
 * @param length How many.
 */
void trl_put_raw(trl_buffer *buffer, const uint8_t *bytes, size_t length);

/**
 * @brief Appends a `u8`.
 * @param buffer The buffer.
 * @param value The byte.
 */
void trl_put_u8(trl_buffer *buffer, uint8_t value);

/**
 * @brief Appends a `u32`, least significant byte first.
 * @param buffer The buffer.
 * @param value The integer.
 */
void trl_put_u32(trl_buffer *buffer, uint32_t value);

/**
 * @brief Appends a `u64`, least significant byte first.
 * @param buffer The buffer.
 * @param value The integer.
 */
void trl_put_u64(trl_buffer *buffer, uint64_t value);

/**
 * @brief Appends an `i128`, two's complement, least significant first.
 * @param buffer The buffer.
 * @param value The integer.
 */
void trl_put_i128(trl_buffer *buffer, trl_int128 value);

/**
 * @brief Appends a flag: 1 if `set`, else 0.
 * @param buffer The buffer.
 * @param set The flag.
 */
void trl_put_flag(trl_buffer *buffer, bool set);

/**
 * @brief Appends bytes or a string: its length as a `u32`, then it.
 * @param buffer The buffer.
 * @param bytes The bytes.
 */
void trl_put_bytes(trl_buffer *buffer, trl_slice bytes);

/**
 * @brief Appends a count of a list as a `u32`.
 * @param buffer The buffer.
 * @param count The count.
 */
void trl_put_count(trl_buffer *buffer, size_t count);

/**
 * @brief Appends `length` bytes for the caller to fill.
 * @param buffer The buffer.
 * @param length How many.
 * @return The first of them, valid until the next write; null if the
 * buffer failed.
 */
uint8_t *trl_put_space(trl_buffer *buffer, size_t length);

/**
 * @brief Overwrites the `u32` at `at`, written before as a placeholder.
 * @param buffer The buffer.
 * @param at Where the `u32` begins.
 * @param value The integer.
 */
void trl_patch_u32(trl_buffer *buffer, size_t at, uint32_t value);

/**
 * @brief Begins a frame of kind `kind`: its header, its length yet unknown.
 * @param buffer The buffer.
 * @param kind The message's kind.
 * @return Where the frame begins, for trl_frame_finish().
 */
size_t trl_frame_start(trl_buffer *buffer, uint8_t kind);

/**
 * @brief Ends the frame begun at `start`: its length written into its
 * header, the bytes written since.
 * @param buffer The buffer.
 * @param start What trl_frame_start() gave.
 */
void trl_frame_finish(trl_buffer *buffer, size_t start);

#endif
