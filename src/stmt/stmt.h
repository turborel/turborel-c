/**
 * @file stmt.h
 * @brief A statement's request written and its replies read apart, as a
 * pipeline writes several requests before it reads their replies.
 */
#ifndef TRL_STMT_STMT_H
#define TRL_STMT_STMT_H

#include "codec/buffer.h"
#include "codec/error.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Where a request is written: its buffer, and the codec's failure if it cannot be. */
typedef struct trl_encoding {
    /** @brief The buffer it is appended to. */
    trl_buffer *out;
    /** @brief Where the codec's failure goes. */
    trl_codec_error *error;
} trl_encoding;

/**
 * @brief Appends a Query of `sql` to `to`, its transaction ended with it
 * if `ends`, carrying the session's query attributes if `attributed`.
 */
bool trl_query_encode(const trl_conn *conn, const char *sql, bool ends, bool attributed, trl_encoding to);

/** @brief Reads a Query's replies into a new result, as trl_query() reads them once it has sent its request. */
trl_status trl_query_read(trl_conn *conn, trl_result **out);

/**
 * @brief Appends an Execute of `statement`, cursor 0, with `params`, null
 * for none, to `to`, its transaction ended with it if `ends`.
 */
bool trl_execute_encode(const trl_conn *conn, const trl_statement *statement, const trl_params *params, bool ends,
                        trl_encoding to);

/** @brief Reads an Execute's replies into a new result, as trl_execute() reads them once it has sent its request. */
trl_status trl_execute_read(trl_conn *conn, const trl_statement *statement, trl_result **out);

/** @brief The number a statement is prepared under. */
uint32_t trl_statement_number(const trl_statement *statement);

/** @brief How many statements the connection has prepared in its session, each numbered from 0. */
uint32_t trl_statements_prepared(const trl_conn *conn);

/** @brief Notes that every statement the connection prepared is gone, as its session's reset destroys them. */
void trl_statements_gone(trl_conn *conn);

#endif
