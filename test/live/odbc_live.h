/**
 * @file odbc_live.h
 * @brief What the ODBC driver's live tests share: the server they start,
 * a connection of the driver to it, and ways to run and read statements.
 */
#ifndef TRL_TEST_LIVE_ODBC_LIVE_H
#define TRL_TEST_LIVE_ODBC_LIVE_H

// NOLINTBEGIN(misc-include-cleaner)
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sqlucode.h>
// NOLINTEND(misc-include-cleaner)

#include <stdbool.h>

/** @brief The bytes of a text the tests give or read. */
#define ODBC_TEXT_BYTES 256

/** @brief The connection string of the test's server, as ann, by trust. */
extern char odbc_connection_string[ODBC_TEXT_BYTES];

/** @brief An environment, a connection of it, connected, and a statement of it. */
typedef struct odbc_opened {
    /** @brief The environment. */
    SQLHENV env;
    /** @brief The connection. */
    SQLHDBC dbc;
    /** @brief The statement. */
    SQLHSTMT stmt;
} odbc_opened;

/** @brief `text` as the SQLCHAR an ODBC function takes, a copy in one of a few buffers used in turn. */
SQLCHAR *sql_text(const char *text);

/** @brief Opens an environment, a connection to the test's server and a statement; whether it could. */
bool odbc_open(odbc_opened *o);

/** @brief Frees what odbc_open() opened. */
void odbc_close(odbc_opened *o);

/** @brief Runs `sql` on `stmt`, its cursor closed after; whether it succeeded or changed no row. */
bool odbc_run(SQLHSTMT stmt, const char *sql);

/** @brief The SQLSTATE of the handle's first diagnostic record, its native code into `native` if not null. */
const char *odbc_state(SQLSMALLINT type, SQLHANDLE handle, SQLINTEGER *native);

/** @brief What a function on `stmt` returned, its first diagnostic told on standard error if it failed. */
SQLRETURN odbc_said(SQLHSTMT stmt, SQLRETURN returned);

/** @brief The descriptor `stmt` uses as its `attribute`, SQL_ATTR_APP_ROW_DESC or another. */
SQLHDESC odbc_descriptor(SQLHSTMT stmt, SQLINTEGER attribute);

/** @brief Starts the test's server in `name` under the working directory, its schema ANN made; whether it could,
 * `*skip` if it was not asked for. */
bool odbc_start(const char *name, bool *skip);

/** @brief Stops the test's server. */
void odbc_stop(void);

#endif
