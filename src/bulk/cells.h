/**
 * @file cells.h
 * @brief A copy's rows by column, as text, as CopyColumns carries them
 * [PROTOCOL CopyColumns 'O']: of each column, a bit for each null, where
 * each cell ends, and the cells' text end to end.
 */
#ifndef TRL_BULK_CELLS_H
#define TRL_BULK_CELLS_H

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief A column of the rows: its null bits, its ends, its text. */
typedef struct trl_cell_column {
    /** @brief A bit for each row, set if its cell is null. */
    trl_buffer nulls;
    /** @brief Where each row's cell ends in the text. */
    uint32_t *ends;
    /** @brief How many ends `ends` holds room for. */
    size_t room;
    /** @brief The cells' text. */
    trl_buffer text;
} trl_cell_column;

/** @brief Rows by column, as text. */
typedef struct trl_cells {
    /** @brief The columns, as many as the first row's values. */
    trl_cell_column *columns;
    /** @brief How many. */
    size_t column_count;
    /** @brief The rows held. */
    uint32_t rows;
    /** @brief About how many octets the rows take in a CopyColumns. */
    size_t bytes;
} trl_cells;

/** @brief Makes rows by column that hold none. */
void trl_cells_init(trl_cells *cells);

/**
 * @brief Whether a row of `values` goes by column, its cells their text:
 * each value null or a character string of UTF-8, as many as the rows'
 * columns, or any number but none of the first row.
 */
bool trl_cells_take(const trl_cells *cells, const trl_value *values, size_t count);

/** @brief Adds a row trl_cells_take() takes; false, the rows as they were, if memory cannot be had. */
bool trl_cells_add(trl_cells *cells, const trl_value *values, size_t count);

/** @brief Encodes the rows as a CopyColumns into `out`. */
bool trl_cells_encode(const trl_cells *cells, trl_buffer *out, trl_codec_error *error);

/** @brief Drops the rows, to be filled again, any number of columns. */
void trl_cells_clear(trl_cells *cells);

/** @brief Frees what the rows hold. */
void trl_cells_free(trl_cells *cells);

#endif
