/**
 * @file import.h
 * @brief Import [CLIENT Importing]: rows read from a source, a record at a
 * time, and inserted into a table over a connection, a batch of them to
 * each transaction; by bulk copy where the server grants it. A source is
 * the program's, or delimited text, CSV or tab-separated, read from a
 * reader of the program's.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_IMPORT_H
#define TRL_IMPORT_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/failure.h"
#include "turborel/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How many rows an import commits at a time, unless told. */
#define TRL_IMPORT_BATCH 1000
/** @brief The longest record delimited text holds by default, in bytes: the protocol's longest message's. */
#define TRL_DELIMITED_MAX_RECORD 16777216

/** @brief Where in a source a record began. */
typedef struct trl_position {
    /** @brief The record's number among the source's records of data, from 1, a header not counted. */
    uint64_t record;
    /** @brief The line it began on, from 1; 0 of a source not made of lines. */
    uint64_t line;
} trl_position;

/** @brief Why a source cannot give its next record: where, and what is wrong. */
typedef struct trl_source_error {
    /** @brief Where the record began. */
    trl_position position;
    /** @brief What is wrong, in plain words; the source's, valid until its next call. */
    const char *message;
} trl_source_error;

/** @brief A source's headings: the names of its fields, or none. */
typedef struct trl_headings {
    /** @brief The names, UTF-8, the source's; null if it names none, its fields then the table's columns in order. */
    const char *const *names;
    /** @brief How many. */
    size_t count;
} trl_headings;

/** @brief What a source's call came to. */
typedef enum trl_source_read {
    /** @brief It gave what was asked. */
    TRL_READ_OK = 0,
    /** @brief It has no more records. */
    TRL_READ_END,
    /** @brief It cannot be read, and says why. */
    TRL_READ_FAILED,
} trl_source_read;

/**
 * @brief Where rows to import come from: the program's, or delimited
 * text's. `headings` is asked once, before the first record; then
 * `record` until it gives none. A source holds no more than a record at a
 * time, so memory is bounded however long its input.
 */
typedef struct trl_source {
    /** @brief The source's own. */
    void *context;
    /** @brief Gives its headings, or none; TRL_READ_FAILED fills `error`. */
    trl_source_read (*headings)(void *context, trl_headings *out, trl_source_error *error);
    /**
     * @brief Gives its next record's values, into `values`, empty, and
     * where it began; a source that knows no types gives character
     * strings and nulls, which are cast to their columns' types as they
     * are inserted.
     */
    trl_source_read (*record)(void *context, trl_params *values, trl_position *at, trl_source_error *error);
} trl_source;

/** @brief Why a row, or an import, failed: the input's, or an SQL condition. */
typedef struct trl_cause {
    /** @brief Whether it is an SQL condition, the server's or one of a string too long for its column. */
    bool sql;
    /** @brief Of the input's: what is wrong, in plain words. Of an SQL condition: its message. */
    const char *message;
    /** @brief Of an SQL condition: the failure, without a position in the statement the import made; else null. */
    const trl_failure *failure;
} trl_cause;

/** @brief A row skipped: where its record began, and why. */
typedef struct trl_rejected {
    /** @brief Where its record began. */
    trl_position position;
    /** @brief Why it failed. */
    trl_cause cause;
} trl_rejected;

/**
 * @brief Is given each row an import skips.
 *
 * @param context The program's.
 * @param row The row, valid until the handler returns.
 */
typedef void (*trl_rejected_handler)(void *context, const trl_rejected *row);

/** @brief What an import is to do. */
typedef struct trl_import {
    /** @brief The table the rows go into, as SQL writes its name. */
    const char *table;
    /** @brief How many rows are committed at a time, TRL_IMPORT_BATCH unless told; 0 for all at once. */
    uint64_t batch;
    /**
     * @brief Whether a row refused, by the server or as a string too long
     * for its column, or of too many or too few fields, is skipped and
     * given to `rejected`, rather than ending the import.
     */
    bool skip;
    /** @brief Is given each row skipped; may be null. */
    trl_rejected_handler rejected;
    /** @brief The handler's context. */
    void *context;
} trl_import;

/** @brief What an import did, and why it stopped if it failed. */
typedef struct trl_imported trl_imported;

/**
 * @brief Inserts a source's records into a table, through one prepared
 * INSERT of the source's headings, or of the table's columns, each value
 * cast by the server to its column's type but a string for a character
 * string column, assigned to it as INSERT assigns, so one too long is
 * refused, not cut; a field longer than 64 KiB for a large object column
 * sent by creating a large object. A batch is committed at a time; a
 * failure rolls the batch back and stops, or, if asked, a bad row is
 * skipped. Where the server grants `copy`, no row is skipped and no
 * column is a large object's, the rows go by bulk copy, each batch a copy.
 * The session must have no transaction active.
 *
 * @param conn The connection.
 * @param source The source.
 * @param how What to do.
 * @param out Where what it did goes, the caller's to free; made on
 * TRL_FAILED too, holding why it stopped.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED;
 * TRL_NO_MEMORY, nothing made.
 */
TRL_API trl_status trl_import_rows(trl_conn *conn, const trl_source *source, const trl_import *how, trl_imported **out);

/**
 * @brief How many rows were inserted and committed.
 *
 * @param imported What an import did; null gives 0.
 * @return The rows.
 */
TRL_API uint64_t trl_imported_rows(const trl_imported *imported);

/**
 * @brief How many rows were skipped, each given to the handler.
 *
 * @param imported What an import did; null gives 0.
 * @return The rows.
 */
TRL_API uint64_t trl_imported_rejected(const trl_imported *imported);

/**
 * @brief Why the import stopped, if it failed.
 *
 * @param imported What an import did.
 * @param cause Where the cause goes, valid as long as `imported`.
 * @return Whether it failed.
 */
TRL_API bool trl_imported_cause(const trl_imported *imported, trl_cause *cause);

/**
 * @brief Where the record that stopped the import began, if one did.
 *
 * @param imported What an import did.
 * @param position Where it goes.
 * @return Whether a record stopped it.
 */
TRL_API bool trl_imported_position(const trl_imported *imported, trl_position *position);

/**
 * @brief How many rows of the batch under way a failure rolled back.
 *
 * @param imported What an import did; null gives 0.
 * @return The rows.
 */
TRL_API uint64_t trl_imported_rolled_back(const trl_imported *imported);

/**
 * @brief Writes why an import stopped as one line, as turborel-client
 * displays it: "record 3, line 4: cause; 2 rows committed, 1 rolled
 * back".
 *
 * @param imported What an import did.
 * @param out Where the text goes, ended by a null, cut short to fit; may
 * be null if `capacity` is 0.
 * @param capacity The bytes `out` holds.
 * @return The bytes of the whole text, its null not counted; 0 of an
 * import that did not fail.
 */
TRL_API size_t trl_imported_text(const trl_imported *imported, char *out, size_t capacity);

/**
 * @brief Frees what an import did.
 *
 * @param imported It; null does nothing.
 */
TRL_API void trl_imported_free(trl_imported *imported);

/** @brief How delimited text is written. */
typedef struct trl_dialect {
    /** @brief What splits fields: one ASCII character, not a quote or a line break. */
    char delimiter;
    /** @brief Whether fields may be enclosed in double quotes, as CSV's are. */
    bool quoted;
    /** @brief Whether the first record names the fields. */
    bool header;
    /** @brief What a field not enclosed in quotes is written as to be null; "" by default. */
    const char *null;
    /** @brief The longest record, in bytes, its line break counted. */
    size_t max_record;
} trl_dialect;

/**
 * @brief CSV (RFC 4180): fields split at commas, enclosed in quotes as
 * they need; no header; an empty field null.
 *
 * @return The dialect.
 */
TRL_API trl_dialect trl_dialect_csv(void);

/**
 * @brief Tab-separated text: fields split at tabs, never quoted; no
 * header; an empty field null.
 *
 * @return The dialect.
 */
TRL_API trl_dialect trl_dialect_tsv(void);

/**
 * @brief A reader of delimited text's bytes.
 *
 * @param context The program's.
 * @param into Where they go.
 * @param capacity How many `into` holds.
 * @param length Where how many were read goes: 0 at the end.
 * @return Whether it read; false if the input cannot be read.
 */
typedef bool (*trl_text_read)(void *context, uint8_t *into, size_t capacity, size_t *length);

/** @brief Delimited text, read a record at a time. */
typedef struct trl_delimited trl_delimited;

/**
 * @brief Delimited text read from a reader, written as `dialect` says: a
 * record a line, a line break CR LF or LF, a quoted field free to hold
 * the delimiter, a quote written twice or a line break; UTF-8, a byte
 * order mark beginning it passed over.
 *
 * @param read The reader.
 * @param context The reader's context.
 * @param dialect The dialect, copied.
 * @param out Where it goes, the caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a delimiter
 * not one ASCII character other than a quote, CR or LF; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_delimited_new(trl_text_read read, void *context, const trl_dialect *dialect,
                                     trl_delimited **out);

/**
 * @brief Delimited text as a source, which it is until freed.
 *
 * @param delimited The delimited text.
 * @return The source.
 */
TRL_API trl_source trl_delimited_source(trl_delimited *delimited);

/**
 * @brief Frees delimited text.
 *
 * @param delimited It; null does nothing.
 */
TRL_API void trl_delimited_free(trl_delimited *delimited);

#ifdef __cplusplus
}
#endif

#endif
