/**
 * @file result.h
 * @brief A result's state: its arena, its columns and rows, and its
 * completion; and how a statement's replies fill it.
 */
#ifndef TRL_STMT_RESULT_H
#define TRL_STMT_RESULT_H

#include "codec/arena.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief A result: what it holds lives in its arena, its rows' list apart. */
struct trl_result {
    /** @brief Its replies' bodies, and what they decode to. */
    trl_arena arena;
    /** @brief Whether it is a query's, of columns and rows. */
    bool has_rows;
    /** @brief Its columns. */
    const trl_column *columns;
    /** @brief How many. */
    uint32_t column_count;
    /** @brief Their names, null-ended, null of one unnamed. */
    char **names;
    /** @brief Their declared types' names, null of one not told. */
    char **type_names;
    /** @brief Their types as SQL writes them: as declared, if told, else as their tags write them. */
    char **written;
    /** @brief Its rows. */
    trl_row *rows;
    /** @brief How many. */
    size_t row_count;
    /** @brief How many its list holds. */
    size_t row_capacity;
    /** @brief Whether a cursor's batch left more. */
    bool more;
    /** @brief Whether another statement's replies follow its Complete, tag 5. */
    bool followed;
    /** @brief Its completion's SQLCODE. */
    int32_t sqlcode;
    /** @brief Its completion's SQLSTATE. */
    char sqlstate[TRL_SQLSTATE_BYTES];
    /** @brief Whether it holds a failure, of native rows not read. */
    bool failed;
    /** @brief The failure. */
    trl_failure failure;
    /** @brief Each value's text, row by row, made as trl_result_text() is first asked for it; null until then. */
    char **texts;
};

/** @brief A new, empty result; null if memory cannot be had. */
trl_result *trl_result_make(void);

/** @brief Gives the result columns, copied into its arena. */
bool trl_result_set_columns(trl_result *result, const trl_column *columns, uint32_t count);

/** @brief Gives the result columns already in its arena, their names named. */
bool trl_result_take_columns(trl_result *result, const trl_column *columns, uint32_t count);

/** @brief Adds rows already in its arena. */
bool trl_result_add_rows(trl_result *result, const trl_row *rows, uint32_t count);

/** @brief Keeps a completion's SQLCODE and SQLSTATE. */
void trl_result_complete(trl_result *result, const trl_completed *completed);

/** @brief Reads a statement's rows into `result`: Rows to Complete, or to Suspended, its more. */
bool trl_read_rows(trl_conn *conn, trl_result *result);

/** @brief Fills `query` of the connection's statement settings: autocommit and attributes. */
void trl_query_of(const trl_conn *conn, const char *sql, bool multi, trl_query_request *query);

#endif
