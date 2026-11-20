/**
 * @file odbc.h
 * @brief The ODBC 3.x driver's handles and what they share (RULES.md D2):
 * an environment, its connections, their statements, and the descriptors
 * of each statement's parameters and columns, as Microsoft's *ODBC
 * Programmer's Reference* lays them out; each handle's diagnostics; and
 * text passed in and out, as UTF-8 or as SQLWCHAR. The driver is built
 * over the C API, `turborel/turborel.h`, alone.
 */
#ifndef TRL_ODBC_ODBC_H
#define TRL_ODBC_ODBC_H

/* ODBC's headers are each platform's own, which the include checker does
   not know (R-LINT-3); Windows' need windows.h first. */
// NOLINTBEGIN(misc-include-cleaner)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <sqlucode.h>
// NOLINTEND(misc-include-cleaner)

#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An entry point ODBC's driver manager finds by name: exported by the
   module definition file on Windows, made visible elsewhere. */
#ifdef _WIN32
#define TRL_ODBC_EXPORT
#else
#define TRL_ODBC_EXPORT __attribute__((visibility("default")))
#endif

/** @brief The bytes of a SQLSTATE, its null included. */
#define TRL_ODBC_STATE_BYTES 6U

/** @brief A diagnostic record: its SQLSTATE, native code (the SQLCODE) and message. */
typedef struct trl_odbc_record {
    /** @brief Its SQLSTATE. */
    char sqlstate[TRL_ODBC_STATE_BYTES];
    /** @brief Its native error code: the failure's SQLCODE. */
    SQLINTEGER native;
    /** @brief Its message, the driver's prefix included; owned. */
    char *message;
} trl_odbc_record;

/** @brief A handle's diagnostics, made by the last function called on it. */
typedef struct trl_odbc_diag {
    /** @brief Its records. */
    trl_odbc_record *records;
    /** @brief How many. */
    size_t count;
    /** @brief What the function returned. */
    SQLRETURN returned;
} trl_odbc_diag;

/** @brief A value read a piece at a time: the value, and how much of it has been given. */
typedef struct trl_odbc_piece {
    /** @brief The value. */
    const trl_value *value;
    /** @brief Its text, as trl_result_text() gives it, or as read of a large object; may be null. */
    const char *text;
    /** @brief A large object's text read whole, owned; may be null. */
    char *owned;
    /** @brief How much of it has been given: octets, or SQLWCHAR units. */
    size_t given;
    /** @brief Whether all of it has been. */
    bool done;
} trl_odbc_piece;

typedef struct trl_odbc_env trl_odbc_env;
typedef struct trl_odbc_dbc trl_odbc_dbc;
typedef struct trl_odbc_stmt trl_odbc_stmt;
typedef struct trl_odbc_desc trl_odbc_desc;

/** @brief An environment. */
struct trl_odbc_env {
    /** @brief SQL_HANDLE_ENV. */
    SQLSMALLINT kind;
    /** @brief Its diagnostics. */
    trl_odbc_diag diag;
    /** @brief The ODBC version the application asked for, 0 until it does. */
    SQLINTEGER version;
    /** @brief Its connections not yet freed, the last allocated first. */
    trl_odbc_dbc *connections;
};

/** @brief A connection. */
struct trl_odbc_dbc {
    /** @brief SQL_HANDLE_DBC. */
    SQLSMALLINT kind;
    /** @brief Its diagnostics. */
    trl_odbc_diag diag;
    /** @brief Its environment. */
    trl_odbc_env *env;
    /** @brief The C API's connection, while connected. */
    trl_conn *conn;
    /** @brief The data source's name, or "", while connected; owned. */
    char *dsn;
    /** @brief Whether each statement is its own transaction: ODBC's default. */
    bool autocommit;
    /** @brief How long connecting waits, in seconds; 0 for the default. */
    SQLUINTEGER login_timeout;
    /** @brief What cancels its statement running, from another thread; made as it connects. */
    trl_canceller *canceller;
    /** @brief Its statements not yet freed, the last allocated first. */
    trl_odbc_stmt *statements;
    /** @brief The descriptors the application allocated, not yet freed. */
    trl_odbc_desc *descriptors;
    /** @brief SQL_ATTR_METADATA_ID, the default of its statements. */
    bool metadata_id;
    /** @brief SQL_ATTR_ACCESS_MODE: whether the application said it only reads. */
    bool read_only;
    /** @brief The next connection of its environment. */
    trl_odbc_dbc *next;
};

/** @brief A descriptor's record: a parameter's or a column's. */
typedef struct trl_odbc_field {
    /** @brief SQL_DESC_TYPE, its verbose type. */
    SQLSMALLINT type;
    /** @brief SQL_DESC_CONCISE_TYPE. */
    SQLSMALLINT concise_type;
    /** @brief SQL_DESC_DATETIME_INTERVAL_CODE. */
    SQLSMALLINT interval_code;
    /** @brief SQL_DESC_DATA_PTR. */
    SQLPOINTER data;
    /** @brief SQL_DESC_OCTET_LENGTH. */
    SQLLEN octet_length;
    /** @brief SQL_DESC_OCTET_LENGTH_PTR. */
    SQLLEN *length_ptr;
    /** @brief SQL_DESC_INDICATOR_PTR. */
    SQLLEN *indicator_ptr;
    /** @brief SQL_DESC_LENGTH. */
    SQLULEN length;
    /** @brief SQL_DESC_PRECISION. */
    SQLSMALLINT precision;
    /** @brief SQL_DESC_SCALE. */
    SQLSMALLINT scale;
    /** @brief SQL_DESC_PARAMETER_TYPE. */
    SQLSMALLINT parameter_type;
} trl_odbc_field;

/** @brief Which of a statement's four descriptors, or an application's own. */
typedef enum trl_odbc_role {
    /** @brief The application row descriptor: the columns bound. */
    TRL_ODBC_ARD = 0,
    /** @brief The application parameter descriptor: the parameters bound. */
    TRL_ODBC_APD,
    /** @brief The implementation row descriptor: the result's columns. */
    TRL_ODBC_IRD,
    /** @brief The implementation parameter descriptor: the parameters' SQL types. */
    TRL_ODBC_IPD,
} trl_odbc_role;

/** @brief A descriptor. */
struct trl_odbc_desc {
    /** @brief SQL_HANDLE_DESC. */
    SQLSMALLINT kind;
    /** @brief Its diagnostics. */
    trl_odbc_diag diag;
    /** @brief Its connection. */
    trl_odbc_dbc *dbc;
    /** @brief The statement it is implicitly of, or null of one the application allocated. */
    trl_odbc_stmt *stmt;
    /** @brief What it describes. */
    trl_odbc_role role;
    /** @brief Its records, from 1; record 0 unused, as bookmarks are not given. */
    trl_odbc_field *fields;
    /** @brief SQL_DESC_COUNT. */
    SQLSMALLINT count;
    /** @brief How many records its list holds. */
    SQLSMALLINT capacity;
    /** @brief SQL_DESC_ARRAY_SIZE. */
    SQLULEN array_size;
    /** @brief SQL_DESC_BIND_TYPE. */
    SQLULEN bind_type;
    /** @brief SQL_DESC_BIND_OFFSET_PTR. */
    SQLLEN *bind_offset;
    /** @brief SQL_DESC_ARRAY_STATUS_PTR. */
    SQLUSMALLINT *array_status;
    /** @brief SQL_DESC_ROWS_PROCESSED_PTR. */
    SQLULEN *rows_processed;
    /** @brief The next descriptor the application allocated on its connection. */
    trl_odbc_desc *next;
};

/** @brief A statement. */
struct trl_odbc_stmt {
    /** @brief SQL_HANDLE_STMT. */
    SQLSMALLINT kind;
    /** @brief Its diagnostics. */
    trl_odbc_diag diag;
    /** @brief Its connection. */
    trl_odbc_dbc *dbc;
    /** @brief Its four implicit descriptors. */
    trl_odbc_desc implicit[TRL_ODBC_IPD + 1];
    /** @brief The application row descriptor in use, implicit or the application's. */
    trl_odbc_desc *ard;
    /** @brief The application parameter descriptor in use. */
    trl_odbc_desc *apd;
    /** @brief The statement SQLPrepare made, until another or SQLFreeStmt. */
    trl_statement *prepared;
    /** @brief The result of the statement executed, all its rows read. */
    trl_result *result;
    /** @brief The first row of the rowset fetched; the rows before the first if none yet. */
    size_t rowset;
    /** @brief The rows of the rowset fetched. */
    size_t rowset_rows;
    /** @brief Whether a rowset has been fetched. */
    bool fetched;
    /** @brief The column SQLGetData last read, 0 for none. */
    SQLUSMALLINT got_column;
    /** @brief How much of that column's value it has given. */
    trl_odbc_piece got;
    /** @brief The rows the last statement changed, or -1. */
    SQLLEN row_count;
    /** @brief SQL_ATTR_MAX_ROWS: the most rows of a query kept, 0 for all. */
    SQLULEN max_rows;
    /** @brief SQL_ATTR_METADATA_ID: whether catalog functions' arguments are identifiers. */
    bool metadata_id;
    /** @brief SQL_ATTR_CURSOR_TYPE. */
    SQLULEN cursor_type;
    /** @brief SQL_ATTR_NOSCAN: whether escape sequences are sent as they are. */
    bool noscan;
    /** @brief SQL_ATTR_QUERY_TIMEOUT, in seconds; kept, as the server bounds no statement by it. */
    SQLULEN query_timeout;
    /** @brief The cursor's name SQLSetCursorName gave, owned; null for the one the driver makes. */
    char *cursor_name;
    /** @brief The next statement of its connection. */
    trl_odbc_stmt *next;
};

/* Handles. */

/** @brief The environment `handle` is, or null if it is none. */
trl_odbc_env *trl_odbc_env_of(SQLHANDLE handle);

/** @brief The connection `handle` is, or null if it is none. */
trl_odbc_dbc *trl_odbc_dbc_of(SQLHANDLE handle);

/** @brief The statement `handle` is, or null if it is none. */
trl_odbc_stmt *trl_odbc_stmt_of(SQLHANDLE handle);

/** @brief The descriptor `handle` is, or null if it is none. */
trl_odbc_desc *trl_odbc_desc_of(SQLHANDLE handle);

/** @brief The diagnostics of any handle of `type`, or null if it is none. */
trl_odbc_diag *trl_odbc_diag_of(SQLSMALLINT type, SQLHANDLE handle);

/** @brief Frees every statement and descriptor of `dbc`, as disconnecting does. */
void trl_odbc_free_children(trl_odbc_dbc *dbc);

/** @brief Where SQLConnect's data source's name, user and password are in their list. */
enum trl_odbc_credential {
    /** @brief The data source's name. */
    TRL_ODBC_DSN = 0,
    /** @brief The user. */
    TRL_ODBC_USER = 1,
    /** @brief The password. */
    TRL_ODBC_PASSWORD = 2,
    /** @brief How many. */
    TRL_ODBC_CREDENTIALS = 3,
};

/** @brief Connects `dbc` by a connection string, and a data source's name, user and password; any may be null. */
SQLRETURN trl_odbc_connect(trl_odbc_dbc *dbc, const char *string, const char *const *credentials);

/** @brief Gives up the statement's result and its rows, as a cursor closed. */
void trl_odbc_close_cursor(trl_odbc_stmt *stmt);

/** @brief Gives up the statement SQLPrepare made, destroying it on the server where it may. */
void trl_odbc_unprepare(trl_odbc_stmt *stmt);

/* Descriptors. */

/** @brief Makes `desc` empty, of `role`, of `dbc`'s, implicitly `stmt`'s if not null. */
void trl_odbc_desc_init(trl_odbc_desc *desc, trl_odbc_dbc *dbc, trl_odbc_stmt *stmt, trl_odbc_role role);

/** @brief Frees what `desc` holds. */
void trl_odbc_desc_destroy(trl_odbc_desc *desc);

/** @brief Record `number`, from 1, of `desc`, made and the count raised if it was past it; null if memory cannot be
 * had. */
trl_odbc_field *trl_odbc_desc_field(trl_odbc_desc *desc, SQLSMALLINT number);

/** @brief Sets a record's concise type, C or SQL, and its verbose type and datetime subcode with it. */
void trl_odbc_field_type(trl_odbc_field *field, SQLSMALLINT concise);

/* Diagnostics. */

/** @brief Clears `diag`, as each function but the diagnostics' does first. */
void trl_odbc_clear(trl_odbc_diag *diag);

/** @brief Adds a record of `sqlstate`, SQLCODE `native` and `message`; false if memory cannot be had. */
bool trl_odbc_record_add(trl_odbc_diag *diag, const char *sqlstate, SQLINTEGER native, const char *message);

/** @brief Records the driver's own error of `sqlstate` and `message`; SQL_ERROR. */
SQLRETURN trl_odbc_fail(trl_odbc_diag *diag, const char *sqlstate, const char *message);

/** @brief Records the driver's own warning of `sqlstate` and `message`; SQL_SUCCESS_WITH_INFO. */
SQLRETURN trl_odbc_warn(trl_odbc_diag *diag, const char *sqlstate, const char *message);

/** @brief Records the C API's `failure`, its SQLSTATE, SQLCODE and message; SQL_ERROR. */
SQLRETURN trl_odbc_failed(trl_odbc_diag *diag, const trl_failure *failure);

/** @brief Records memory not had (HY001); SQL_ERROR. */
SQLRETURN trl_odbc_no_memory(trl_odbc_diag *diag);

/** @brief Records what a function came to, as SQL_DIAG_RETURNCODE gives it; `returned`. */
SQLRETURN trl_odbc_return(trl_odbc_diag *diag, SQLRETURN returned);

/** @brief Records the warning the connection's last statement completed with, if any; SQL_SUCCESS_WITH_INFO if it did.
 */
SQLRETURN trl_odbc_warned(trl_odbc_diag *diag, const trl_conn *conn);

/* Text. */

/** @brief Where text goes out: a buffer, its capacity, and where its length goes. */
typedef struct trl_odbc_out {
    /** @brief The buffer; may be null. */
    SQLPOINTER buffer;
    /** @brief Its capacity, in characters of its kind, or in bytes if `bytes`. */
    SQLLEN capacity;
    /** @brief Where the whole text's length goes, if not null, as a SQLSMALLINT. */
    SQLSMALLINT *small_length;
    /** @brief Or as a SQLINTEGER. */
    SQLINTEGER *length;
    /** @brief Or as a SQLLEN. */
    SQLLEN *long_length;
    /** @brief Whether the text goes out as SQLWCHAR. */
    bool wide;
    /** @brief Whether the capacity and length are in bytes. */
    bool bytes;
} trl_odbc_out;

/**
 * @brief Writes `text`, UTF-8, to `out`, null-ended, cut short to fit;
 * SQL_SUCCESS, or SQL_SUCCESS_WITH_INFO with 01004 on `diag` if cut.
 */
SQLRETURN trl_odbc_put(trl_odbc_diag *diag, const trl_odbc_out *out, const char *text);

/** @brief Text an application gave, `length` bytes or SQL_NTS, as a new UTF-8 string; null if memory cannot be had. */
char *trl_odbc_text(const SQLCHAR *text, SQLINTEGER length);

/** @brief Text an application gave as SQLWCHAR, `length` characters or SQL_NTS, as a new UTF-8 string; null if it
 * cannot be. */
char *trl_odbc_wide_text(const SQLWCHAR *text, SQLINTEGER length);

/** @brief `text`, UTF-8, as SQLWCHAR into `out` of `capacity` characters, null-ended, cut short; how many characters
 * the whole takes. */
size_t trl_odbc_widen(const char *text, SQLWCHAR *out, size_t capacity);

/** @brief Where SQLGetDiagRec's parts go: the SQLSTATE, of six characters, the native error, and the message. */
typedef struct trl_odbc_rec_out {
    /** @brief The SQLSTATE's buffer, of the message's kind; may be null. */
    SQLPOINTER state;
    /** @brief The native error's; may be null. */
    SQLINTEGER *native;
    /** @brief The message's. */
    trl_odbc_out message;
} trl_odbc_rec_out;

/** @brief SQLGetDiagRec's work: record `number` of the handle's diagnostics into `out`. */
SQLRETURN trl_odbc_diag_rec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, const trl_odbc_rec_out *out);

/**
 * @brief SQLGetDiagField's work: the field `field` of record `number`, or
 * of the header, into `out`, text as `out` asks, a number into its
 * buffer; SQL_NO_DATA past the last record, SQL_ERROR of a field that is
 * none.
 */
SQLRETURN trl_odbc_diag_field(const trl_odbc_out *out, SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number,
                              SQLSMALLINT field);

/**
 * @brief SQLGetDescField's work: field `id` of record `number`, or of the
 * header, into `out`; of the implementation row descriptor, the result's
 * columns' as SQLColAttribute gives them.
 */
SQLRETURN trl_odbc_desc_get(trl_odbc_desc *desc, SQLSMALLINT number, SQLSMALLINT id, const trl_odbc_out *out);

/* Statements, for the Unicode entry points. */

/** @brief SQLExecDirect's work, of `sql` in UTF-8. */
SQLRETURN trl_odbc_exec_direct(trl_odbc_stmt *stmt, const char *sql);

/** @brief SQLPrepare's work, of `sql` in UTF-8. */
SQLRETURN trl_odbc_prepare(trl_odbc_stmt *stmt, const char *sql);

/** @brief SQLNativeSql's work: `sql` as the driver sends it, into `out`. */
SQLRETURN trl_odbc_native_sql(trl_odbc_dbc *dbc, const char *sql, const trl_odbc_out *out);

/** @brief SQLSetCursorName's work. */
SQLRETURN trl_odbc_set_cursor_name(trl_odbc_stmt *stmt, const char *name);

/** @brief SQLGetCursorName's work, into `out`. */
SQLRETURN trl_odbc_get_cursor_name(trl_odbc_stmt *stmt, const trl_odbc_out *out);

/** @brief SQLGetInfo's work, into `out`. */
SQLRETURN trl_odbc_get_info(trl_odbc_dbc *dbc, SQLUSMALLINT type, const trl_odbc_out *out);

/** @brief SQLSetConnectAttr's work. */
SQLRETURN trl_odbc_set_connect_attr(trl_odbc_dbc *dbc, SQLINTEGER attribute, SQLPOINTER value);

/** @brief SQLGetConnectAttr's work, a number into `out`'s buffer, text as it asks. */
SQLRETURN trl_odbc_get_connect_attr(trl_odbc_dbc *dbc, SQLINTEGER attribute, const trl_odbc_out *out);

/** @brief SQLSetStmtAttr's work. */
SQLRETURN trl_odbc_set_stmt_attr(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLPOINTER value);

/** @brief SQLGetStmtAttr's work. */
SQLRETURN trl_odbc_get_stmt_attr(trl_odbc_stmt *stmt, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER *length);

#endif
