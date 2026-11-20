/**
 * @file requests.h
 * @brief Every request a client sends, encoded as a frame [PROTOCOL Requests]:
 * Hello to CopyEnd, each tail written only with what it says.
 *
 * Each encoder appends one whole frame to a buffer, or, if a field cannot
 * be sent, records why and leaves the buffer as it was.
 */
#ifndef TRL_CODEC_REQUESTS_H
#define TRL_CODEC_REQUESTS_H

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/tail.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief A Hello's fields [PROTOCOL Hello 'H']. */
typedef struct trl_hello {
    /** @brief The protocol's version, 1. */
    uint32_t version;
    /** @brief The user. */
    trl_slice user;
    /** @brief The mechanism of authentication. */
    trl_slice mechanism;
    /** @brief The mechanism's first bytes. */
    trl_slice initial;
    /** @brief The capabilities asked for. */
    const trl_slice *capabilities;
    /** @brief How many. */
    size_t capability_count;
    /** @brief The connection's attributes, tail tag 1. */
    const trl_attribute *attributes;
    /** @brief How many. */
    size_t attribute_count;
} trl_hello;

/** @brief A Query's fields [PROTOCOL Query 'Q']. */
typedef struct trl_query_request {
    /** @brief The statement, or with `multi` several. */
    trl_slice sql;
    /** @brief Whether the transaction ends with the statement. */
    bool ends;
    /** @brief The request's attributes, tail tag 1. */
    const trl_attribute *attributes;
    /** @brief How many. */
    size_t attribute_count;
    /** @brief Whether the text is several statements, tail tag 2. */
    bool multi;
} trl_query_request;

/** @brief An Execute's fields [PROTOCOL Execute 'E']. */
typedef struct trl_execute_request {
    /** @brief The statement's number. */
    uint32_t statement;
    /** @brief A value for each parameter. */
    const trl_value *values;
    /** @brief How many. */
    size_t value_count;
    /** @brief The most rows sent now; 0 for all. */
    uint32_t rows;
    /** @brief Whether the transaction ends with the statement. */
    bool ends;
    /** @brief The request's attributes, tail tag 1. */
    const trl_attribute *attributes;
    /** @brief How many. */
    size_t attribute_count;
    /** @brief The cursor's number, tail tag 3; 0 is written as nothing. */
    uint32_t cursor;
} trl_execute_request;

/** @brief A Lob request's fields, by its op [PROTOCOL Lob 'L']. */
typedef struct trl_lob_request {
    /** @brief TRL_LOB_READ, CREATE, APPEND, FREE or HOLD. */
    uint8_t op;
    /** @brief The locator, of read, append, free and hold. */
    uint32_t locator;
    /** @brief The first octet or character, from 0, of read. */
    uint64_t from;
    /** @brief The most octets or characters, of read. */
    uint64_t count;
    /** @brief Of read, in characters, not octets. */
    bool characters;
    /** @brief Of create, TRL_LOB_CHARACTER or TRL_LOB_BINARY. */
    uint8_t kind;
    /** @brief Of append, the octets. */
    trl_slice bytes;
} trl_lob_request;

/** @brief A CopyIn's fields [PROTOCOL CopyIn 'I']. */
typedef struct trl_copy_in_request {
    /** @brief The table, as SQL names it. */
    trl_slice table;
    /** @brief Its columns the rows give, in order; none for every column. */
    const trl_slice *columns;
    /** @brief How many. */
    size_t column_count;
    /** @brief Whether the transaction ends with the copy. */
    bool ends;
} trl_copy_in_request;

/** @brief A row of a CopyData: its values. */
typedef struct trl_row_values {
    /** @brief The values. */
    const trl_value *values;
    /** @brief How many. */
    size_t count;
} trl_row_values;

/**
 * @brief Encodes a Hello.
 * @param buffer The buffer.
 * @param hello Its fields.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_hello(trl_buffer *buffer, const trl_hello *hello, trl_codec_error *error);

/**
 * @brief Encodes an Answer [PROTOCOL Answer 'A'].
 * @param buffer The buffer.
 * @param answer Its bytes.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_answer(trl_buffer *buffer, trl_slice answer, trl_codec_error *error);

/**
 * @brief Encodes a Query.
 * @param buffer The buffer.
 * @param query Its fields.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_query(trl_buffer *buffer, const trl_query_request *query, trl_codec_error *error);

/**
 * @brief Encodes a Prepare [PROTOCOL Prepare 'P'].
 * @param buffer The buffer.
 * @param statement The client's number for it.
 * @param sql The statement.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_prepare(trl_buffer *buffer, uint32_t statement, trl_slice sql, trl_codec_error *error);

/**
 * @brief Encodes an Execute.
 * @param buffer The buffer.
 * @param execute Its fields.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_execute(trl_buffer *buffer, const trl_execute_request *execute, trl_codec_error *error);

/**
 * @brief Encodes a Fetch [PROTOCOL Fetch 'F'].
 * @param buffer The buffer.
 * @param statement The statement's number.
 * @param rows The most rows sent; 0 for all.
 * @param cursor The cursor's number, tail tag 3; 0 is written as nothing.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_fetch(trl_buffer *buffer, uint32_t statement, uint32_t rows, uint32_t cursor, trl_codec_error *error);

/**
 * @brief Encodes a Close [PROTOCOL Close 'C'].
 * @param buffer The buffer.
 * @param statement The statement's number.
 * @param cursor The cursor's number, tail tag 3; 0 is written as nothing.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_close(trl_buffer *buffer, uint32_t statement, uint32_t cursor, trl_codec_error *error);

/**
 * @brief Encodes a Deallocate [PROTOCOL Deallocate 'D'].
 * @param buffer The buffer.
 * @param statement The statement's number.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_deallocate(trl_buffer *buffer, uint32_t statement, trl_codec_error *error);

/**
 * @brief Encodes a Lob request [PROTOCOL Lob 'L'].
 * @param buffer The buffer.
 * @param request Its op and fields.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_lob(trl_buffer *buffer, const trl_lob_request *request, trl_codec_error *error);

/**
 * @brief Encodes a Cancel [PROTOCOL Cancel 'Z'].
 * @param buffer The buffer.
 * @param key The cancel key Ready gave, 16 bytes.
 * @param error Where a key not of 16 bytes is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_cancel(trl_buffer *buffer, trl_slice key, trl_codec_error *error);

/**
 * @brief Encodes a request that is a header alone: Terminate, Ping or
 * Reset [PROTOCOL Terminate 'X'] [PROTOCOL Ping 'G'] [PROTOCOL Reset 'W'].
 * @param buffer The buffer.
 * @param kind TRL_KIND_TERMINATE, TRL_KIND_PING or TRL_KIND_RESET.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_bare(trl_buffer *buffer, uint8_t kind, trl_codec_error *error);

/**
 * @brief Encodes a Describe [PROTOCOL Describe 'Y'].
 * @param buffer The buffer.
 * @param what TRL_DESCRIBE_STATEMENT, of cursor 0, or TRL_DESCRIBE_CURSOR.
 * @param statement The statement's number.
 * @param cursor The cursor's number; 0 of a statement.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_describe(trl_buffer *buffer, uint8_t what, uint32_t statement, uint32_t cursor, trl_codec_error *error);

/**
 * @brief Encodes a ChangeUser [PROTOCOL ChangeUser 'U'].
 * @param buffer The buffer.
 * @param user The user.
 * @param mechanism The mechanism.
 * @param initial Its first bytes.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_change_user(trl_buffer *buffer, trl_slice user, trl_slice mechanism, trl_slice initial,
                            trl_codec_error *error);

/**
 * @brief Encodes a CopyIn [PROTOCOL CopyIn 'I'].
 * @param buffer The buffer.
 * @param copy Its fields.
 * @param error Where a field that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_copy_in(trl_buffer *buffer, const trl_copy_in_request *copy, trl_codec_error *error);

/**
 * @brief Encodes a CopyData [PROTOCOL CopyData 'J'].
 * @param buffer The buffer.
 * @param rows The rows.
 * @param count How many.
 * @param error Where a value that cannot be sent is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_copy_data(trl_buffer *buffer, const trl_row_values *rows, size_t count, trl_codec_error *error);

/**
 * @brief Encodes a CopyData of `count` rows already written, each as
 * trl_write_row() writes one [PROTOCOL CopyData 'J'].
 * @param buffer The buffer.
 * @param rows The rows' bytes.
 * @param count How many rows they are.
 * @param error Where a failure goes.
 * @return Nonzero if encoded.
 */
bool trl_encode_copy_rows(trl_buffer *buffer, trl_slice rows, size_t count, trl_codec_error *error);

/**
 * @brief Encodes a CopyEnd [PROTOCOL CopyEnd 'N'].
 * @param buffer The buffer.
 * @param abandon Whether every row is undone.
 * @param error Where a failure is recorded.
 * @return Nonzero if encoded.
 */
bool trl_encode_copy_end(trl_buffer *buffer, bool abandon, trl_codec_error *error);

#endif
