/**
 * @file cells.c
 * @brief A copy's rows by column, as text [PROTOCOL CopyColumns 'O']: a
 * cell is its character string's text, which the server takes as it
 * takes the string in CopyData, and a null cell a bit set, ending where
 * the cell before it ends, as turborel-client's import fills its cells.
 */
#include "bulk/cells.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/utf8.h"
#include "codec/values.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The bits of an octet, the octets of an end, and how the room for ends grows. */
#define TRL_BITS 8U
#define TRL_END_BYTES 4U
#define TRL_FIRST_ROOM 64U
#define TRL_GROWTH 2U

void trl_cells_init(trl_cells *cells) {
    memset(cells, 0, sizeof *cells);
}

/* Whether `value` goes as a cell: null, or a character string of UTF-8
   whose text keeps its column's last end within a u32 after `before`. */
static bool cell_of(const trl_value *value, uint64_t before) {
    if (value->tag == TRL_TAG_NULL) {
        return true;
    }
    if (value->tag != TRL_TAG_CHARACTER && value->tag != TRL_TAG_VARYING) {
        return false;
    }
    return before + value->as.bytes.length <= UINT32_MAX &&
           trl_utf8_valid(value->as.bytes.bytes, value->as.bytes.length);
}

bool trl_cells_take(const trl_cells *cells, const trl_value *values, size_t count) {
    if (count == 0U || cells->rows == UINT32_MAX || (cells->rows > 0U && count != cells->column_count)) {
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        uint64_t before = cells->rows == 0U ? 0U : cells->columns[i].text.length;
        if (!cell_of(&values[i], before)) {
            return false;
        }
    }
    return true;
}

/* Makes room for the first row's `count` columns. */
static bool columns_for(trl_cells *cells, size_t count) {
    if (cells->column_count == count && cells->columns != NULL) {
        return true;
    }
    trl_cells_free(cells);
    cells->columns = trl_calloc(count, sizeof *cells->columns);
    if (cells->columns == NULL) {
        return false;
    }
    cells->column_count = count;
    for (size_t i = 0U; i < count; ++i) {
        trl_buffer_init(&cells->columns[i].nulls);
        trl_buffer_init(&cells->columns[i].text);
    }
    return true;
}

/* Adds a cell of `value` to `column`, at row `row`. */
static bool add_cell(trl_cell_column *column, uint32_t row, const trl_value *value) {
    if (row == column->room) {
        size_t room = column->room == 0U ? TRL_FIRST_ROOM : column->room * TRL_GROWTH;
        uint32_t *ends = trl_realloc(column->ends, room * sizeof *ends);
        if (ends == NULL) {
            return false;
        }
        column->ends = ends;
        column->room = room;
    }
    if (row % TRL_BITS == 0U) {
        trl_put_u8(&column->nulls, 0U);
    }
    if (value->tag == TRL_TAG_NULL) {
        if (column->nulls.state == TRL_BUFFER_OK) {
            column->nulls.data[row / TRL_BITS] |= (uint8_t)(1U << (row % TRL_BITS));
        }
    } else {
        trl_put_raw(&column->text, value->as.bytes.bytes, value->as.bytes.length);
    }
    column->ends[row] = (uint32_t)column->text.length;
    return column->nulls.state == TRL_BUFFER_OK && column->text.state == TRL_BUFFER_OK;
}

/* Takes row `row` back out of `column`, whose rows before it are kept. */
static void drop_row(trl_cell_column *column, uint32_t row) {
    column->text.length = row == 0U ? 0U : column->ends[row - 1U];
    column->nulls.length = ((size_t)row + TRL_BITS - 1U) / TRL_BITS;
    if (row % TRL_BITS != 0U) {
        column->nulls.data[row / TRL_BITS] &= (uint8_t)~(1U << (row % TRL_BITS));
    }
    column->text.state = TRL_BUFFER_OK;
    column->nulls.state = TRL_BUFFER_OK;
}

bool trl_cells_add(trl_cells *cells, const trl_value *values, size_t count) {
    if (cells->rows == 0U && !columns_for(cells, count)) {
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (!add_cell(&cells->columns[i], cells->rows, &values[i])) {
            /* Memory not had: the row taken back out, the rows before it kept. */
            for (size_t j = 0U; j <= i; ++j) {
                drop_row(&cells->columns[j], cells->rows);
            }
            return false;
        }
        cells->bytes += TRL_END_BYTES + (values[i].tag == TRL_TAG_NULL ? 0U : values[i].as.bytes.length);
    }
    ++cells->rows;
    return true;
}

bool trl_cells_encode(const trl_cells *cells, trl_buffer *out, trl_codec_error *error) {
    trl_copy_column *columns = trl_calloc(cells->column_count == 0U ? 1U : cells->column_count, sizeof *columns);
    if (columns == NULL) {
        trl_codec_fail(error, TRL_SQLCODE_RESOURCES, TRL_SQLSTATE_RESOURCES, "memory could not be had");
        return false;
    }
    for (size_t i = 0U; i < cells->column_count; ++i) {
        const trl_cell_column *column = &cells->columns[i];
        columns[i].nulls.bytes = column->nulls.data;
        columns[i].nulls.length = column->nulls.length;
        columns[i].ends = column->ends;
        columns[i].text.bytes = column->text.data;
        columns[i].text.length = column->text.length;
    }
    bool encoded = trl_encode_copy_columns(out, cells->rows, columns, cells->column_count, error);
    free(columns);
    return encoded;
}

void trl_cells_clear(trl_cells *cells) {
    for (size_t i = 0U; i < cells->column_count; ++i) {
        cells->columns[i].nulls.length = 0U;
        cells->columns[i].text.length = 0U;
    }
    cells->rows = 0U;
    cells->bytes = 0U;
}

void trl_cells_free(trl_cells *cells) {
    for (size_t i = 0U; cells->columns != NULL && i < cells->column_count; ++i) {
        trl_buffer_free(&cells->columns[i].nulls);
        trl_buffer_free(&cells->columns[i].text);
        free(cells->columns[i].ends);
    }
    free(cells->columns);
    trl_cells_init(cells);
}
