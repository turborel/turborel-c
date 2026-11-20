/**
 * @file import_copy.c
 * @brief An import by bulk copy [CLIENT Importing]
 * [CLIENT Copying and exporting]: its rows checked as the INSERT's
 * parameters take them, then sent a batch to a copy, each copy committing
 * its batch as it ends, with no round trip but the copy's end. A row the server refuses fails its
 * copy, every row of the batch undone, the failure's detail naming the
 * row, so the import says its record as it would of a row inserted alone.
 */
#include "bulk/copy.h"
#include "bulk/imported.h"
#include "codec/buffer.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The records a batch notes the places of at first, and how its room grows. */
#define TRL_FIRST_PLACES 64U
#define TRL_GROWTH 2U

/* What the server's detail of a row a copy refused begins and ends with. */
#define TRL_ROW "row "
#define TRL_OF_THE_COPY " of the copy"
#define TRL_DECIMAL 10U
#define TRL_MESSAGE_BYTES 512U

/* The batch under way: where each of its records began. */
typedef struct batch {
    trl_position *at;
    size_t count;
    size_t capacity;
} batch;

/* The row of a copy `failure` names, from 1, or 0 if it names none. */
static size_t row_of(const trl_failure *failure) {
    const char *detail = trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_DETAIL);
    size_t prefix = strlen(TRL_ROW);
    size_t suffix = strlen(TRL_OF_THE_COPY);
    if (detail == NULL || strncmp(detail, TRL_ROW, prefix) != 0) {
        return 0U;
    }
    size_t length = strlen(detail);
    if (length <= prefix + suffix || strcmp(detail + length - suffix, TRL_OF_THE_COPY) != 0) {
        return 0U;
    }
    size_t row = 0U;
    for (size_t i = prefix; i < length - suffix; ++i) {
        if (detail[i] < '0' || detail[i] > '9' || row > SIZE_MAX / TRL_DECIMAL) {
            return 0U;
        }
        row = (row * TRL_DECIMAL) + (size_t)(detail[i] - '0');
    }
    return row;
}

/* Stops the import, `rolled_back` rows of the batch undone. */
static void stop_sql(trl_import_run *run, const trl_position *at, const trl_failure *failure, size_t rolled_back) {
    run->imported->rolled_back = rolled_back;
    trl_imported_stop_sql(run->imported, at, failure);
}

/* Ends the batch's copy: its rows committed, or why it failed, at the row
   it names. */
static bool ended(trl_import_run *run, trl_copy *copy, batch *b) {
    uint64_t rows = 0U;
    trl_status finished = trl_copy_finish(copy, &rows);
    trl_copy_free(copy);
    if (finished == TRL_OK) {
        run->imported->rows += rows;
        b->count = 0U;
        return true;
    }
    const trl_failure *failure = trl_conn_failure(run->conn);
    size_t row = row_of(failure);
    if (row > 0U && row <= b->count) {
        stop_sql(run, &b->at[row - 1U], failure, row - 1U);
    } else {
        stop_sql(run, NULL, failure, b->count);
    }
    return false;
}

/* What reading the next record came to. */
typedef enum next {
    NEXT_RECORD = 0,
    NEXT_END,
    NEXT_FAILED,
} next;

/* Reads the next record and checks it as the INSERT's parameters take it,
   into `run->sent`; where it began in `at`. */
static next checked(trl_import_run *run, const batch *b, trl_position *at) {
    trl_params_clear(run->values);
    trl_source_error error = {{0U, 0U}, ""};
    trl_source_read read = run->source->record(run->source->context, run->values, at, &error);
    if (read == TRL_READ_END) {
        return NEXT_END;
    }
    if (read != TRL_READ_OK) {
        run->imported->rolled_back = b->count;
        trl_imported_stop_input(run->imported, &error.position, error.message);
        return NEXT_FAILED;
    }
    char message[TRL_MESSAGE_BYTES];
    trl_failure failure;
    trl_failure_init(&failure);
    bool fits = trl_import_checked(run, &failure, message, sizeof message);
    if (!fits && message[0] != '\0') {
        run->imported->rolled_back = b->count;
        trl_imported_stop_input(run->imported, at, message);
    } else if (!fits) {
        stop_sql(run, at, &failure, b->count);
    }
    trl_failure_clear(&failure);
    return fits ? NEXT_RECORD : NEXT_FAILED;
}

/* Notes where a record of the batch began. */
static bool noted(batch *b, const trl_position *at) {
    if (b->count == b->capacity) {
        size_t capacity = b->capacity == 0U ? TRL_FIRST_PLACES : b->capacity * TRL_GROWTH;
        trl_position *grown = trl_realloc(b->at, capacity * sizeof *grown);
        if (grown == NULL) {
            return false;
        }
        b->at = grown;
        b->capacity = capacity;
    }
    b->at[b->count++] = *at;
    return true;
}

/* The copy's target: the table and its columns, as SQL names them. */
static trl_copy_target target_of(const trl_import_run *run, const char **columns) {
    const char *name = (const char *)run->columns.data;
    for (size_t i = 0U; i < run->column_count; ++i) {
        columns[i] = name;
        name += strlen(name) + 1U;
    }
    trl_copy_target target = {(const char *)run->table.data, columns, run->column_count};
    return target;
}

/* Copies a batch of the source's records, its copy begun at its first
   record and ended at its last; `more` whether records may follow. */
static bool copied(trl_import_run *run, const trl_copy_target *target, batch *b, bool *more) {
    trl_position at = {0U, 0U};
    next read = checked(run, b, &at);
    *more = false;
    if (read != NEXT_RECORD) {
        return read == NEXT_END;
    }
    trl_copy *copy = NULL;
    if (trl_copy_begin(run->conn, target, true, &copy) != TRL_OK) {
        stop_sql(run, NULL, trl_conn_failure(run->conn), 0U);
        return false;
    }
    while (read == NEXT_RECORD) {
        if (trl_copy_send(copy, run->sent) != TRL_OK || !noted(b, &at)) {
            stop_sql(run, &at, trl_conn_failure(run->conn), b->count);
            trl_copy_free(copy);
            return false;
        }
        if (b->count == run->how->batch) {
            *more = true;
            return ended(run, copy, b);
        }
        read = checked(run, b, &at);
    }
    if (read == NEXT_FAILED) {
        trl_copy_free(copy);
        return false;
    }
    return ended(run, copy, b);
}

bool trl_import_copied(trl_import_run *run) {
    const char **columns = (const char **)trl_calloc(run->column_count == 0U ? 1U : run->column_count, sizeof *columns);
    if (columns == NULL) {
        trl_failure no_memory;
        trl_failure_init(&no_memory);
        trl_failure_no_memory(&no_memory);
        stop_sql(run, NULL, &no_memory, 0U);
        trl_failure_clear(&no_memory);
        return false;
    }
    trl_copy_target target = target_of(run, columns);
    batch b = {NULL, 0U, 0U};
    bool more = true;
    bool done = true;
    while (done && more) {
        done = copied(run, &target, &b, &more);
    }
    free(b.at);
    free((void *)columns);
    return done;
}
