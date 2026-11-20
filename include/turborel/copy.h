/**
 * @file copy.h
 * @brief Bulk copy and export [CLIENT Copying and exporting]: rows sent
 * into a table with no round trip but the copy's end, all or nothing; and
 * a query's rows written as they come, as CSV or as the protocol's own
 * frames, which read back with every value as it was.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_COPY_H
#define TRL_COPY_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/statements.h"
#include "turborel/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief About the most bytes of rows one CopyData carries. */
#define TRL_COPY_CHUNK_BYTES 262144

/** @brief Where a copy's rows go: a table, as SQL names it, and its columns. */
typedef struct trl_copy_target {
    /** @brief The table, as SQL writes its name, UTF-8. */
    const char *table;
    /** @brief The columns, in the order of each row's values; null for every column of the table. */
    const char *const *columns;
    /** @brief How many. */
    size_t column_count;
} trl_copy_target;

/** @brief A copy under way, over its connection. */
typedef struct trl_copy trl_copy;

/**
 * @brief Begins a copy into a table, the transaction's own under
 * autocommit [PROTOCOL CopyIn 'I']. Until it ends, the connection sends
 * only its rows, its end and Ping.
 *
 * @param conn The connection, which the copy holds until it ends.
 * @param target The table and its columns, copied.
 * @param out Where the copy goes, the caller's to free; null on failure.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED with
 * the connection's failure: `copy` not granted (SQLSTATE 08006, the
 * connection kept), or the connection lost; TRL_NO_MEMORY. A table not
 * found, or a column not of it, is trl_copy_finish()'s failure.
 */
TRL_API trl_status trl_copy_in(trl_conn *conn, const trl_copy_target *target, trl_copy **out);

/**
 * @brief Sends a row: its values, in the target's columns' order, sent
 * with the rows before it once they are TRL_COPY_CHUNK_BYTES, with no
 * reply [PROTOCOL CopyData 'J'].
 *
 * @param copy The copy.
 * @param row The row's values, copied.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a copy
 * ended; TRL_FAILED with the connection's failure, a value the protocol
 * cannot carry or the connection lost. A row the server refuses is
 * trl_copy_finish()'s failure.
 */
TRL_API trl_status trl_copy_send(trl_copy *copy, const trl_params *row);

/**
 * @brief Ends the copy [PROTOCOL CopyEnd 'N']: its rows inserted, all of
 * them, or none.
 *
 * @param copy The copy, which ends.
 * @param rows Where the rows inserted go; may be null.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a copy
 * ended; TRL_FAILED with the connection's failure, the copy's, every row
 * of it undone: the table or a column not found, a row of too many or too
 * few values, a value its column refuses, its detail naming the row, as
 * "row 7 of the copy".
 */
TRL_API trl_status trl_copy_finish(trl_copy *copy, uint64_t *rows);

/**
 * @brief Gives the copy up: every row of it undone.
 *
 * @param copy The copy, which ends.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a copy
 * ended; TRL_FAILED with the connection's failure.
 */
TRL_API trl_status trl_copy_abandon(trl_copy *copy);

/**
 * @brief Frees a copy; one not ended is abandoned first, so the
 * connection takes requests again.
 *
 * @param copy The copy; null does nothing.
 */
TRL_API void trl_copy_free(trl_copy *copy);

/** @brief What a source of rows gave. */
typedef enum trl_source_status {
    /** @brief A row, in the list given. */
    TRL_SOURCE_ROW = 0,
    /** @brief No more rows. */
    TRL_SOURCE_END,
    /** @brief A failure of the program's, which abandons the copy. */
    TRL_SOURCE_FAILED,
} trl_source_status;

/**
 * @brief A source of rows: fills `row`, empty, with the next row's values.
 *
 * @param context The program's.
 * @param row Where the values go.
 * @return What it gave.
 */
typedef trl_source_status (*trl_row_source)(void *context, trl_params *row);

/**
 * @brief Copies every row `source` gives into a table, as trl_copy_in(),
 * trl_copy_send() and trl_copy_finish() do.
 *
 * @param conn The connection.
 * @param target The table and its columns.
 * @param source The source of rows.
 * @param context The source's context.
 * @param rows Where the rows inserted go; may be null.
 * @return As those functions return; TRL_FAILED with SQLCODE -910,
 * SQLSTATE TB910, of a source that failed, the copy abandoned.
 */
TRL_API trl_status trl_copy_rows(trl_conn *conn, const trl_copy_target *target, trl_row_source source, void *context,
                                 uint64_t *rows);

/** @brief How an export writes a query's rows. */
typedef enum trl_export_format {
    /** @brief CSV (RFC 4180): a header of the columns' names, then a record for each row. */
    TRL_EXPORT_CSV = 0,
    /** @brief The protocol's own frames: a Description, Rows, a Complete. */
    TRL_EXPORT_NATIVE,
} trl_export_format;

/**
 * @brief A writer of an export's bytes.
 *
 * @param context The program's.
 * @param bytes The bytes.
 * @param length How many.
 * @return Whether they were written; false ends the export.
 */
typedef bool (*trl_export_write)(void *context, const uint8_t *bytes, size_t length);

/** @brief Where an export goes: its format, and its writer. */
typedef struct trl_export_sink {
    /** @brief The format. */
    trl_export_format format;
    /** @brief The writer. */
    trl_export_write write;
    /** @brief The writer's context. */
    void *context;
} trl_export_sink;

/**
 * @brief Runs a query and writes its rows as they come, a batch at a
 * time, so memory holds a batch whatever the result's size. As CSV: a
 * field quoted where it holds a comma, a quote, a line break or spaces at
 * either end, a quote in it written twice, a null an empty field, an empty
 * string `""`, each record ended by CR LF, each value as
 * trl_value_to_text() writes it. As native: the frames the server sent, a
 * Description then Rows, and a Complete.
 *
 * @param conn The connection.
 * @param sql The query.
 * @param sink Where its rows go.
 * @param rows Where how many rows were written goes; may be null.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED
 * with the connection's failure: the query's; a statement that is no
 * query; a write that failed (SQLSTATE 08006, the export's, the
 * connection kept).
 */
TRL_API trl_status trl_export(trl_conn *conn, const char *sql, const trl_export_sink *sink, uint64_t *rows);

/**
 * @brief A reader of native rows' bytes.
 *
 * @param context The program's.
 * @param into Where they go.
 * @param capacity How many `into` holds.
 * @return How many were read: 0 at the end.
 */
typedef size_t (*trl_native_read)(void *context, uint8_t *into, size_t capacity);

/**
 * @brief Reads back native rows an export wrote: their columns and rows,
 * every value as it was, of whatever type.
 *
 * @param read The reader.
 * @param context The reader's context.
 * @param out Where the result goes, the caller's to free. On TRL_FAILED
 * it is made, holding the failure, which trl_result_failure() gives.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED,
 * SQLSTATE 08006, of input that is not native rows, or cut short;
 * TRL_NO_MEMORY, nothing made.
 */
TRL_API trl_status trl_read_native(trl_native_read read, void *context, trl_result **out);

/**
 * @brief A result's failure: of trl_read_native() alone.
 *
 * @param result The result.
 * @return Its failure, or null if it has none.
 */
TRL_API const trl_failure *trl_result_failure(const trl_result *result);

#ifdef __cplusplus
}
#endif

#endif
