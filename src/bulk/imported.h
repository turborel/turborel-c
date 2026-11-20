/**
 * @file imported.h
 * @brief What an import did, and what its two ways, by INSERT and by
 * copy, share.
 */
#ifndef TRL_BULK_IMPORTED_H
#define TRL_BULK_IMPORTED_H

#include "codec/buffer.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief What an import did, and why it stopped. */
struct trl_imported {
    /** @brief Rows inserted and committed. */
    uint64_t rows;
    /** @brief Rows skipped. */
    uint64_t rejected;
    /** @brief Rows of the batch under way a failure rolled back. */
    uint64_t rolled_back;
    /** @brief Whether it failed. */
    bool failed;
    /** @brief Whether a record stopped it, and where it began. */
    bool placed;
    /** @brief Where. */
    trl_position position;
    /** @brief Whether its cause is an SQL condition, else the input's. */
    bool sql;
    /** @brief The input's cause, null-ended, owned. */
    char *message;
    /** @brief The SQL condition. */
    trl_failure failure;
};

/** @brief Stops the import for the input's cause `message`, at `position` if given. */
void trl_imported_stop_input(trl_imported *imported, const trl_position *position, const char *message);

/** @brief Stops the import for the SQL condition `failure`, its position dropped, at `position` if given. */
void trl_imported_stop_sql(trl_imported *imported, const trl_position *position, const trl_failure *failure);

/** @brief Copies `failure` into `into`, without a position in the statement the import made. */
void trl_imported_condition(trl_failure *into, const trl_failure *failure);

/** @brief An import under way: what it reads, what it inserts into, and how far it is. */
typedef struct trl_import_run {
    /** @brief The connection. */
    trl_conn *conn;
    /** @brief The source. */
    const trl_source *source;
    /** @brief What to do. */
    const trl_import *how;
    /** @brief The INSERT prepared. */
    trl_statement *insert;
    /** @brief What it did. */
    trl_imported *imported;
    /** @brief The table, as SQL text of delimited identifiers. */
    trl_buffer table;
    /** @brief The columns, each a delimited identifier, null-ended, one after another. */
    trl_buffer columns;
    /** @brief How many. */
    size_t column_count;
    /** @brief Rows inserted in the batch under way. */
    uint64_t in_batch;
    /** @brief The record read, its values. */
    trl_params *values;
    /** @brief The values sent for it. */
    trl_params *sent;
} trl_import_run;

/**
 * @brief Checks the record in `run->values` as the INSERT's parameters
 * take it, into `run->sent`: false, with `failure` set or the input's
 * cause in `message`, if it does not fit.
 */
bool trl_import_checked(trl_import_run *run, trl_failure *failure, char *message, size_t capacity);

/** @brief Imports the source's records by bulk copy, a batch a copy; false, the import stopped, on failure. */
bool trl_import_copied(trl_import_run *run);

#endif
