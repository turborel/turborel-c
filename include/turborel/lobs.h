/**
 * @file lobs.h
 * @brief Large objects [CLIENT Large objects]: a CLOB or BLOB value read
 * whole from its row, or by its locator, a range or a chunk at a time; a
 * locator freed or held; and a new large object written a piece at a time
 * and passed as a parameter by its locator.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_LOBS_H
#define TRL_LOBS_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most octets one read of a large object gives, a chunk. */
#define TRL_LOB_CHUNK_BYTES 262144
/**
 * @brief The longest large object a server sends whole by default,
 * SERVER.LOB_INLINE_BYTES; a longer one comes as its locator.
 */
#define TRL_LOB_INLINE_BYTES 65536

/** @brief What a large object value is. */
typedef struct trl_lob_info {
    /** @brief Whether it is a character large object, else a binary one. */
    bool character;
    /** @brief Whether it came as its locator, else whole in its row. */
    bool located;
    /** @brief Its octets. */
    uint64_t octets;
    /** @brief Its characters, of a character one. */
    uint64_t characters;
} trl_lob_info;

/** @brief A range of a large object: where it begins, and how much. */
typedef struct trl_lob_range {
    /** @brief The first octet, or character, from 0. */
    uint64_t from;
    /** @brief The most octets, or characters. */
    uint64_t count;
    /** @brief Whether `from` and `count` are a character large object's characters, whole ones. */
    bool characters;
} trl_lob_range;

/**
 * @brief What a large object value is: a CLOB or BLOB value, whole or by
 * its locator.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a value of
 * another kind.
 */
TRL_API trl_status trl_value_lob(const trl_value *value, trl_lob_info *out);

/**
 * @brief Reads a range of a large object: of one whole, from the value; of
 * one by its locator, from the server, at most a chunk [PROTOCOL Lob 'L'].
 * Fewer than asked come only at its end, or at a chunk's.
 *
 * @param conn The connection the value came over.
 * @param lob The large object value.
 * @param range The range.
 * @param out Where the octets go.
 * @param length On entry the octets `out` holds; on return those read. A
 * range in characters reads at most a quarter as many characters as `out`
 * holds octets, so whole characters fit.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer, a value of
 * another kind, or `out` too short for one octet or character;
 * TRL_FAILED with the connection's failure: SQLSTATE 0F001 of a locator
 * no longer valid, or the connection lost.
 */
TRL_API trl_status trl_lob_read(trl_conn *conn, const trl_value *lob, trl_lob_range range, uint8_t *out,
                                size_t *length);

/** @brief A large object read from its beginning to its end, a chunk at a time. */
typedef struct trl_lob_reader trl_lob_reader;

/**
 * @brief A reader of a large object's octets, which reads its locator's
 * value a chunk a request.
 *
 * @param conn The connection the value came over; the reader uses it
 * until freed.
 * @param lob The large object value, copied.
 * @param out Where the reader goes, the caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a value of
 * another kind; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_lob_reader_new(trl_conn *conn, const trl_value *lob, trl_lob_reader **out);

/**
 * @brief Reads the next octets.
 *
 * @param reader The reader.
 * @param out Where they go.
 * @param capacity How many `out` holds.
 * @param length Where how many were read goes: 0 at the end.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED with
 * the connection's failure, SQLSTATE 22000 of a server that sent less
 * than the value's length.
 */
TRL_API trl_status trl_lob_reader_read(trl_lob_reader *reader, uint8_t *out, size_t capacity, size_t *length);

/**
 * @brief Frees a reader.
 *
 * @param reader The reader; null does nothing.
 */
TRL_API void trl_lob_reader_free(trl_lob_reader *reader);

/**
 * @brief Frees a large object's locator: it, and the value it names, are
 * no longer valid [PROTOCOL Lob 'L'].
 *
 * @param conn The connection.
 * @param lob A large object value that came by its locator.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a value of
 * another kind; TRL_FAILED with the connection's failure: SQLSTATE 22000
 * of a large object sent whole, 0F001 of a locator not valid.
 */
TRL_API trl_status trl_lob_free_locator(trl_conn *conn, const trl_value *lob);

/**
 * @brief Holds a large object's locator: valid past its transaction's
 * commit, until it is freed or a transaction is rolled back.
 *
 * @param conn The connection.
 * @param lob A large object value that came by its locator.
 * @return As trl_lob_free_locator() returns.
 */
TRL_API trl_status trl_lob_hold(trl_conn *conn, const trl_value *lob);

/** @brief A new large object, written a piece at a time. */
typedef struct trl_lob_writer trl_lob_writer;

/**
 * @brief Creates a large object, empty, on the server, to write.
 *
 * @param conn The connection; the writer uses it until freed.
 * @param character Whether it is a character large object, its octets
 * UTF-8, else a binary one.
 * @param out Where the writer goes, the caller's to free. On TRL_FAILED it
 * is null, and the connection holds the failure.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED;
 * TRL_NO_MEMORY.
 */
TRL_API trl_status trl_lob_writer_new(trl_conn *conn, bool character, trl_lob_writer **out);

/**
 * @brief Writes octets: sent in Appends of a chunk as they come, a
 * character large object's cut where a character ends, so a write may
 * split a character.
 *
 * @param writer The writer.
 * @param bytes The octets.
 * @param length How many.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED with
 * the connection's failure: SQLSTATE 22021 of a character large object
 * whose octets an Append sent are not UTF-8, which the server checks as
 * each comes, every later Append then refused.
 */
TRL_API trl_status trl_lob_write(trl_lob_writer *writer, const uint8_t *bytes, size_t length);

/**
 * @brief Sends what is written and not yet sent, and adds the large object
 * to `params` as its next value, by its locator, valid until its
 * transaction ends and used once.
 *
 * @param writer The writer, which can write no more.
 * @param params The parameter list.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a writer
 * finished; TRL_FAILED with the connection's failure: SQLSTATE 22021 of a
 * character large object whose last octets are not UTF-8; TRL_NO_MEMORY.
 * One that ends within a character is refused, SQLSTATE 22021, where it is
 * used.
 */
TRL_API trl_status trl_lob_writer_finish(trl_lob_writer *writer, trl_params *params);

/**
 * @brief Frees a writer; the large object, if not finished, is left to
 * its transaction's end.
 *
 * @param writer The writer; null does nothing.
 */
TRL_API void trl_lob_writer_free(trl_lob_writer *writer);

#ifdef __cplusplus
}
#endif

#endif
