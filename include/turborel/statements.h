/**
 * @file statements.h
 * @brief Statements [CLIENT Running statements]
 * [CLIENT Prepared statements]: one run by Query, its rows all at once or a batch at a
 * time; prepared statements executed with parameters; cursors opened,
 * fetched and closed, several of one statement by their numbers; several
 * statements as one request.
 *
 * A result, a statement and a batch are handles the program frees; each
 * is used on the connection's thread. A program includes
 * turborel/turborel.h, which includes this.
 */
#ifndef TRL_STATEMENTS_H
#define TRL_STATEMENTS_H

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

/**
 * @brief What a statement came to: a query's columns and rows, or another
 * statement's completion, of its SQLCODE and SQLSTATE; or a batch of a
 * cursor's rows.
 */
typedef struct trl_result trl_result;

/**
 * @brief Runs one statement by Query, and gives its result: a query's
 * columns and every row, or another statement's completion
 * [PROTOCOL Query 'Q']. Under autocommit the statement is its own
 * transaction.
 *
 * @param conn The connection.
 * @param sql The statement, UTF-8.
 * @param out Where the result goes, the caller's to free; null unless
 * TRL_OK.
 * @return TRL_OK; TRL_FAILED with the connection's failure, as the
 * statement's; TRL_INVALID_ARGUMENT of a null pointer.
 */
TRL_API trl_status trl_query(trl_conn *conn, const char *sql, trl_result **out);

/**
 * @brief Runs one statement with its `?` parameters given as text, as
 * libpq's PQexecParams does: prepared, executed with each value as a
 * character string, which the server assigns to the parameter's type,
 * then destroyed; with no parameter, by Query, as trl_query().
 *
 * @param conn The connection.
 * @param sql The statement, UTF-8.
 * @param count How many parameters.
 * @param values Each one's text, UTF-8, null-ended; a null pointer for
 * the null value. May be null if `count` is 0.
 * @param out Where the result goes, the caller's to free; null unless
 * TRL_OK.
 * @return As trl_query() returns; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_query_params(trl_conn *conn, const char *sql, size_t count, const char *const *values,
                                    trl_result **out);

/**
 * @brief Given each batch of a query's rows as it comes: a result of the
 * query's columns and that batch's rows, borrowed for the call alone.
 */
typedef void (*trl_batch_handler)(void *context, const trl_result *batch);

/**
 * @brief Runs one statement by Query, giving a query's rows to `handler` a
 * batch at a time, as they come, rather than all at once.
 *
 * @param conn The connection.
 * @param sql The statement.
 * @param handler Given each batch, on this thread.
 * @param context Passed to `handler`.
 * @param out Where the result goes: of a query, its columns and no row, of
 * another statement its completion; the caller's to free.
 * @return As trl_query() returns.
 */
TRL_API trl_status trl_query_batches(trl_conn *conn, const char *sql, trl_batch_handler handler, void *context,
                                     trl_result **out);

/**
 * @brief Whether the result is a query's, of columns and rows.
 *
 * @param result The result; null gives false.
 * @return Whether it is.
 */
TRL_API bool trl_result_has_rows(const trl_result *result);

/**
 * @brief How many columns a query's result has.
 *
 * @param result The result; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_result_column_count(const trl_result *result);

/**
 * @brief A column's name, or null of a column the query left unnamed.
 *
 * @param result The result.
 * @param column Which, from 0.
 * @return Its name, UTF-8, null-ended, borrowed; null past the last.
 */
TRL_API const char *trl_result_column_name(const trl_result *result, size_t column);

/**
 * @brief Whether a column is NOT NULL, as the server described it, as
 * turborel-client's column's `not_null` [PROTOCOL Composite fields].
 *
 * @param result The result.
 * @param column Which, from 0.
 * @return Whether it is; false past the last.
 */
TRL_API bool trl_result_column_not_null(const trl_result *result, size_t column);

/**
 * @brief A column's type.
 *
 * @param result The result.
 * @param column Which, from 0.
 * @return Its type, borrowed; null past the last.
 */
TRL_API const trl_data_type *trl_result_column_type(const trl_result *result, size_t column);

/**
 * @brief The name of the type a column was declared with, as the
 * Information Schema's DATA_TYPE gives it, as INTEGER, DOUBLE PRECISION,
 * or USER-DEFINED of a distinct type; told with `declared-types` granted
 * [PROTOCOL Declared types].
 *
 * @param result The result.
 * @param column Which, from 0.
 * @return The name, UTF-8, borrowed; null if the server did not tell it,
 * or past the last column.
 */
TRL_API const char *trl_result_column_type_name(const trl_result *result, size_t column);

/**
 * @brief A column's type as SQL writes it: as it was declared, as INTEGER
 * or S.MONEY, where the server told it; else as its tag and what follows
 * say, as trl_type_to_text() writes it, so INTEGER is NUMERIC(10,0).
 *
 * @param result The result.
 * @param column Which, from 0.
 * @return The type, UTF-8, borrowed; null past the last column.
 */
TRL_API const char *trl_result_column_type_written(const trl_result *result, size_t column);

/**
 * @brief How many rows the result holds.
 *
 * @param result The result; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_result_row_count(const trl_result *result);

/**
 * @brief A value of a row.
 *
 * @param result The result.
 * @param row Which row, from 0.
 * @param column Which column, from 0.
 * @return The value, borrowed; null past the last row or column.
 */
TRL_API const trl_value *trl_result_value(const trl_result *result, size_t row, size_t column);

/**
 * @brief Whether a cursor's batch left the cursor with more rows: Suspended
 * rather than Complete [PROTOCOL Suspended 'S'].
 *
 * @param result The batch; null gives false.
 * @return Whether it did.
 */
TRL_API bool trl_result_more(const trl_result *result);

/**
 * @brief The completion's SQLCODE: 0, or 100 of a query that returned no
 * row; 0 of a batch that left the cursor with more.
 *
 * @param result The result; null gives 0.
 * @return The SQLCODE.
 */
TRL_API int32_t trl_result_sqlcode(const trl_result *result);

/**
 * @brief The completion's SQLSTATE, as "00000", "02000", or "01..." of a
 * warning; "" of a batch that left the cursor with more.
 *
 * @param result The result; null gives "".
 * @return The SQLSTATE, borrowed.
 */
TRL_API const char *trl_result_sqlstate(const trl_result *result);

/**
 * @brief Frees a result, and every value it holds.
 *
 * @param result The result; null does nothing.
 */
TRL_API void trl_result_free(trl_result *result);

/**
 * @brief A value's text, as libpq's PQgetvalue gives it: as
 * trl_value_to_text() writes it, made once and kept by the result.
 *
 * @param result The result.
 * @param row The row, from 0.
 * @param column The column, from 0.
 * @return The text, null-ended, valid until the result is freed; null of
 * the null value, of a row or column past the last, or if memory cannot
 * be had.
 */
TRL_API const char *trl_result_text(trl_result *result, size_t row, size_t column);

/** @brief A prepared statement: its number, its parameters and its columns. */
typedef struct trl_statement trl_statement;

/**
 * @brief Prepares a statement, its dynamic parameters written `?`, under a
 * number of the connection's own [PROTOCOL Prepare 'P'].
 *
 * @param conn The connection.
 * @param sql The statement.
 * @param out Where the statement goes, the caller's to free; null unless
 * TRL_OK.
 * @return TRL_OK; TRL_FAILED with the connection's failure;
 * TRL_INVALID_ARGUMENT of a null pointer.
 */
TRL_API trl_status trl_prepare(trl_conn *conn, const char *sql, trl_statement **out);

/**
 * @brief How many dynamic parameters the statement takes.
 *
 * @param statement The statement; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_statement_parameter_count(const trl_statement *statement);

/**
 * @brief The type a parameter takes.
 *
 * @param statement The statement.
 * @param parameter Which, from 0.
 * @return Its type, borrowed; null past the last.
 */
TRL_API const trl_data_type *trl_statement_parameter_type(const trl_statement *statement, size_t parameter);

/**
 * @brief How many columns a query's statement gives; of a CALL, one for
 * each `?` of an OUT or INOUT parameter.
 *
 * @param statement The statement; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_statement_column_count(const trl_statement *statement);

/**
 * @brief A column's name, or null of one unnamed.
 *
 * @param statement The statement.
 * @param column Which, from 0.
 * @return Its name, borrowed; null past the last.
 */
TRL_API const char *trl_statement_column_name(const trl_statement *statement, size_t column);

/**
 * @brief A column's type.
 *
 * @param statement The statement.
 * @param column Which, from 0.
 * @return Its type, borrowed; null past the last.
 */
TRL_API const trl_data_type *trl_statement_column_type(const trl_statement *statement, size_t column);

/**
 * @brief The name of the type a column was declared with, as
 * trl_result_column_type_name() gives it.
 *
 * @param statement The statement.
 * @param column Which, from 0.
 * @return The name, borrowed; null if not told, or past the last column.
 */
TRL_API const char *trl_statement_column_type_name(const trl_statement *statement, size_t column);

/**
 * @brief A column's type as SQL writes it, as
 * trl_result_column_type_written() gives it.
 *
 * @param statement The statement.
 * @param column Which, from 0.
 * @return The type, borrowed; null past the last column.
 */
TRL_API const char *trl_statement_column_type_written(const trl_statement *statement, size_t column);

/**
 * @brief Executes a prepared statement with values, as many times as wanted
 * [PROTOCOL Execute 'E']: a query's rows all at once, a CALL's output
 * parameters as one row, another statement's completion.
 *
 * @param conn The connection that prepared it.
 * @param statement The statement.
 * @param params Its parameters' values, in order; null for none.
 * @param out Where the result goes, the caller's to free.
 * @return As trl_query() returns.
 */
TRL_API trl_status trl_execute(trl_conn *conn, const trl_statement *statement, const trl_params *params,
                               trl_result **out);

/**
 * @brief A cursor: a prepared statement, and a number of the program's
 * own, 0 unless `cursors` is granted [PROTOCOL Execute 'E'].
 */
typedef struct trl_cursor {
    /** @brief Its statement. */
    const trl_statement *statement;
    /** @brief Its number among the statement's cursors. */
    uint32_t number;
} trl_cursor;

/**
 * @brief Opens a cursor of a query's statement, and takes its first rows,
 * at most `rows`.
 *
 * @param conn The connection.
 * @param cursor The cursor.
 * @param params Its parameters' values; null for none.
 * @param rows The most rows of the first batch.
 * @param out Where the batch goes, the caller's to free; trl_result_more()
 * says whether the cursor has more.
 * @return As trl_query() returns; TRL_FAILED, sending nothing, of a cursor
 * numbered other than 0 when `cursors` is not granted.
 */
TRL_API trl_status trl_cursor_open(trl_conn *conn, trl_cursor cursor, const trl_params *params, uint32_t rows,
                                   trl_result **out);

/**
 * @brief Fetches a cursor's next batch, at most `rows` [PROTOCOL Fetch 'F'].
 *
 * @param conn The connection.
 * @param cursor The cursor.
 * @param rows The most rows.
 * @param out Where the batch goes, the caller's to free.
 * @return As trl_cursor_open() returns.
 */
TRL_API trl_status trl_cursor_fetch(trl_conn *conn, trl_cursor cursor, uint32_t rows, trl_result **out);

/**
 * @brief Closes a cursor [PROTOCOL Close 'C'].
 *
 * @param conn The connection.
 * @param cursor The cursor.
 * @return TRL_OK; TRL_FAILED, as of a cursor not open; TRL_INVALID_ARGUMENT.
 */
TRL_API trl_status trl_cursor_close(trl_conn *conn, trl_cursor cursor);

/**
 * @brief An open cursor's columns, with `describe` [PROTOCOL Describe 'Y'].
 *
 * @param conn The connection.
 * @param cursor The cursor.
 * @param out Where a result of its columns and no row goes, the caller's
 * to free.
 * @return TRL_OK; TRL_FAILED, sending nothing if `describe` was not
 * granted; TRL_INVALID_ARGUMENT.
 */
TRL_API trl_status trl_describe_cursor(trl_conn *conn, trl_cursor cursor, trl_result **out);

/**
 * @brief Describes a prepared statement again, with `describe`.
 *
 * @param conn The connection.
 * @param statement The statement.
 * @param out Where the statement as now described goes, the caller's to
 * free; of the same number.
 * @return As trl_describe_cursor() returns.
 */
TRL_API trl_status trl_describe(trl_conn *conn, const trl_statement *statement, trl_statement **out);

/**
 * @brief Deallocates a prepared statement in the session
 * [PROTOCOL Deallocate 'D']; the handle is still freed by
 * trl_statement_free().
 *
 * @param conn The connection.
 * @param statement The statement.
 * @return TRL_OK; TRL_FAILED, as of a cursor of it open;
 * TRL_INVALID_ARGUMENT.
 */
TRL_API trl_status trl_deallocate(trl_conn *conn, const trl_statement *statement);

/**
 * @brief Frees a statement's handle; the session keeps it until
 * trl_deallocate() or the session's end.
 *
 * @param statement The statement; null does nothing.
 */
TRL_API void trl_statement_free(trl_statement *statement);

/** @brief What several statements run as one request came to. */
typedef struct trl_ran trl_ran;

/**
 * @brief Runs a text of several statements as one request, each in order,
 * until one fails, with `multi-statement`.
 *
 * @param conn The connection.
 * @param sql The statements.
 * @param out Where what they came to goes, the caller's to free.
 * @return TRL_OK, a statement's failure in the ran; TRL_FAILED, sending
 * nothing if `multi-statement` was not granted, or of the connection lost;
 * TRL_INVALID_ARGUMENT.
 */
TRL_API trl_status trl_query_many(trl_conn *conn, const char *sql, trl_ran **out);

/**
 * @brief How many statements came to an outcome.
 *
 * @param ran What they came to; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_ran_count(const trl_ran *ran);

/**
 * @brief A statement's outcome, by its place.
 *
 * @param ran What they came to.
 * @param index Which, from 0.
 * @return The result, borrowed; null past the last.
 */
TRL_API const trl_result *trl_ran_result(const trl_ran *ran, size_t index);

/**
 * @brief The failure that stopped the rest, or null if every statement ran.
 *
 * @param ran What they came to.
 * @return The failure, borrowed, or null.
 */
TRL_API const trl_failure *trl_ran_failure(const trl_ran *ran);

/**
 * @brief Frees what several statements came to.
 *
 * @param ran It; null does nothing.
 */
TRL_API void trl_ran_free(trl_ran *ran);

#ifdef __cplusplus
}
#endif

#endif
