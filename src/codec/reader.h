/**
 * @file reader.h
 * @brief Fields read from a message's body, little-endian, each bounded by
 * the bytes left [PROTOCOL Values and fields].
 *
 * A read that fails records why in the reader's error, as a connection
 * failure (SQLCODE -919, SQLSTATE 08006), and returns false; a decoder
 * tests each read and stops at the first that fails, and the error keeps
 * that first cause. A count is never trusted beyond the bytes that follow it
 * (RULES.md R-SAFE-1).
 */
#ifndef TRL_CODEC_READER_H
#define TRL_CODEC_READER_H

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief How deep arrays, rows, multisets and structured values, and their
 * types, nest in a message, as turborel's MAX_TYPE_NESTING.
 */
#define TRL_MAX_NESTING 16U

/** @brief A body being read. */
typedef struct trl_reader {
    /** @brief The body. */
    const uint8_t *data;
    /** @brief Its bytes. */
    size_t length;
    /** @brief The next byte to read. */
    size_t at;
    /** @brief How many nested parts are being read. */
    unsigned int depth;
    /** @brief What the body is, as "reply", for a failure's message. */
    const char *what;
    /** @brief Where what is read is allocated. */
    trl_arena *arena;
    /** @brief Where a failure is recorded. */
    trl_codec_error *error;
    /** @brief Whether a read has failed. */
    bool failed;
} trl_reader;

/**
 * @brief Readies a reader of `length` bytes of `data`.
 * @param reader The reader.
 * @param data The body; may be null if `length` is 0.
 * @param length Its bytes.
 * @param arena Where what is read is allocated.
 * @param error Where a failure is recorded.
 */
void trl_reader_init(trl_reader *reader, const uint8_t *data, size_t length, trl_arena *arena, trl_codec_error *error);

/**
 * @brief Fails the reader: the body is malformed, for `why`.
 * @param reader The reader.
 * @param why Why.
 * @return False, so a decoder returns its result.
 */
bool trl_reader_fail(trl_reader *reader, const char *why);

/**
 * @brief Whether every byte was read.
 * @param reader The reader.
 * @return Nonzero at the end.
 */
bool trl_reader_at_end(const trl_reader *reader);

/**
 * @brief Checks that every byte was read; fails the reader if not
 * [PROTOCOL Frames].
 * @param reader The reader.
 * @return Nonzero if none is left.
 */
bool trl_reader_finish(trl_reader *reader);

/**
 * @brief Reads a `u8`.
 * @param reader The reader.
 * @param out The byte.
 * @return Nonzero if read.
 */
bool trl_read_u8(trl_reader *reader, uint8_t *out);

/**
 * @brief Reads a `u32`.
 * @param reader The reader.
 * @param out The integer.
 * @return Nonzero if read.
 */
bool trl_read_u32(trl_reader *reader, uint32_t *out);

/**
 * @brief Reads a `u64`.
 * @param reader The reader.
 * @param out The integer.
 * @return Nonzero if read.
 */
bool trl_read_u64(trl_reader *reader, uint64_t *out);

/**
 * @brief Reads an `i128`.
 * @param reader The reader.
 * @param out The integer.
 * @return Nonzero if read.
 */
bool trl_read_i128(trl_reader *reader, trl_int128 *out);

/**
 * @brief Reads a flag, 0 or 1; any other byte fails [PROTOCOL Values and fields].
 * @param reader The reader.
 * @param out The flag.
 * @return Nonzero if read.
 */
bool trl_read_flag(trl_reader *reader, bool *out);

/**
 * @brief Reads `count` bytes as they are, a view into the body.
 * @param reader The reader.
 * @param count How many.
 * @param out The bytes.
 * @return Nonzero if that many were left.
 */
bool trl_read_raw(trl_reader *reader, size_t count, trl_slice *out);

/**
 * @brief Reads bytes: their length, a `u32`, then them, a view into the body.
 * @param reader The reader.
 * @param out The bytes.
 * @return Nonzero if read.
 */
bool trl_read_bytes(trl_reader *reader, trl_slice *out);

/**
 * @brief Reads a string: as bytes, which must be UTF-8.
 * @param reader The reader.
 * @param out The string's bytes, a view into the body, not ended by a null.
 * @return Nonzero if read.
 */
bool trl_read_string(trl_reader *reader, trl_slice *out);

/**
 * @brief Reads the count of a list whose items each take at least `least`
 * bytes; a count more items than the bytes left could hold fails at once,
 * so it reserves nothing [PROTOCOL Values and fields].
 * @param reader The reader.
 * @param least The fewest bytes an item takes, at least 1.
 * @param out The count.
 * @return Nonzero if read.
 */
bool trl_read_count(trl_reader *reader, size_t least, uint32_t *out);

/**
 * @brief Memory for `count` items of `size` bytes from the reader's arena;
 * fails the reader, as memory not had, if none.
 * @param reader The reader.
 * @param count How many.
 * @param size The bytes of each.
 * @return The memory, zeroed; null on failure.
 */
void *trl_reader_alloc(trl_reader *reader, size_t count, size_t size);

/**
 * @brief Enters a nested part; fails past TRL_MAX_NESTING.
 * @param reader The reader.
 * @return Nonzero if entered; trl_reader_leave() leaves it.
 */
bool trl_reader_enter(trl_reader *reader);

/**
 * @brief Leaves a nested part trl_reader_enter() entered.
 * @param reader The reader.
 */
void trl_reader_leave(trl_reader *reader);

#endif
