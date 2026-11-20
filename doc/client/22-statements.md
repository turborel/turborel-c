# Statements and Cursors

[Contents](README.md)

This page describes how the driver gives turborel-client's statements:
running a statement and its rows, a batch at a time or all at once;
preparing and executing; cursors and numbered cursors; describe and
deallocate; several statements in one request; what a completion tells;
notices and conditions; and the session's control, autocommit, reset,
change-user and query attributes. The User's Guide's
[Running Queries](../user/03-queries.md),
[Prepared Statements](../user/04-prepared-statements.md),
[Transactions](../user/05-transactions.md) and
[Errors and Diagnostics](../user/10-errors.md) show them at work.

## Results

### NAME

`trl_query`, `trl_query_params`, `trl_query_batches`,
`trl_result_has_rows`, `trl_result_column_count`, `trl_result_column_name`,
`trl_result_column_type`, `trl_result_column_type_name`,
`trl_result_column_type_written`, `trl_result_row_count`, `trl_result_value`,
`trl_result_text`, `trl_result_more`, `trl_result_sqlcode`, `trl_result_sqlstate`,
`trl_result_free`: a statement run, and what it gave.

### SYNOPSIS

```c
#include <turborel/turborel.h>

typedef void (*trl_batch_handler)(void *context, const trl_result *batch);

trl_status trl_query(trl_conn *conn, const char *sql, trl_result **out);
trl_status trl_query_params(trl_conn *conn, const char *sql, size_t count, const char *const *values,
                            trl_result **out);
trl_status trl_query_batches(trl_conn *conn, const char *sql, trl_batch_handler handler, void *context,
                             trl_result **out);
bool trl_result_has_rows(const trl_result *result);
size_t trl_result_column_count(const trl_result *result);
const char *trl_result_column_name(const trl_result *result, size_t column);
const trl_data_type *trl_result_column_type(const trl_result *result, size_t column);
const char *trl_result_column_type_name(const trl_result *result, size_t column);
const char *trl_result_column_type_written(const trl_result *result, size_t column);
size_t trl_result_row_count(const trl_result *result);
const trl_value *trl_result_value(const trl_result *result, size_t row, size_t column);
const char *trl_result_text(trl_result *result, size_t row, size_t column);
bool trl_result_more(const trl_result *result);
int32_t trl_result_sqlcode(const trl_result *result);
const char *trl_result_sqlstate(const trl_result *result);
void trl_result_free(trl_result *result);
```

### DESCRIPTION

`trl_query` sends Query, its `ends` set under autocommit, and reads the
replies to the statement's end: a query's Description, its Rows and its
Complete, or another statement's Complete [PROTOCOL Query 'Q']. The
result holds the columns, every row, and the completion's SQLCODE and
SQLSTATE, a query of no rows SQLCODE 100 and SQLSTATE 02000. It is the
caller's, and lasts until `trl_result_free`, its values with it, whatever
the connection does after.

`trl_query_params` runs a statement with its `?` parameters given as
text, as libpq's PQexecParams does: each value a null-ended UTF-8
string, or a null pointer for the null value. The statement is
prepared, executed with each value as a character string, which the
server assigns to the parameter's type, and destroyed; its result is as
`trl_query`'s. With no parameter it is `trl_query`.

`trl_query_batches` gives each Rows reply to `handler` as it comes, a
batch whose rows and values last only until the handler returns, the
columns the query's; its result has the columns and the completion, and
no rows [CLIENT Running statements].

`trl_result_has_rows` says whether the statement was a query.
`trl_result_column_name` gives a column's name, or null of one the query
did not name. `trl_result_column_type` gives a column's type, by which
its values are read. With `declared-types` granted, which a connection
asks for, the server tells the type each column was declared with
[PROTOCOL Declared types]: `trl_result_column_type_name` gives its name
as the Information Schema's DATA_TYPE gives it, as INTEGER, FLOAT or
USER-DEFINED, null where the server did not tell it;
`trl_result_column_type_written` gives the type as SQL writes it, as
declared where told, as INTEGER or ANN.MONEY, else as the type's tag and
sizes say, as `trl_type_to_text` writes it, so INTEGER is NUMERIC(10,0)
[CLIENT Prepared statements]. Rows and columns are counted from 0; a
place past the last gives null. `trl_result_text` gives a value's text,
as `trl_value_to_text` writes it, made once and kept by the result until
it is freed; null of the null value, of a place past the last, or if
memory cannot be had. `trl_result_more` says whether a cursor's batch
left rows to fetch.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer, or parameters counted but
not given, nothing sent. `TRL_FAILED`, `*out` null, with the
connection's failure: the statement's Error, of `trl_query_params` its
preparing's or its execution's,
the connection kept unless it is of class 08; or the driver's own,
SQLSTATE 08006 of a connection lost, a reply out of turn ("the server
sent no result", "the server broke off its rows") or a malformed one.

### EXAMPLES

[Running Queries](../user/03-queries.md) runs each.

## Prepared statements

### NAME

`trl_prepare`, `trl_statement_parameter_count`,
`trl_statement_parameter_type`, `trl_statement_column_count`,
`trl_statement_column_name`, `trl_statement_column_type`,
`trl_statement_column_type_name`, `trl_statement_column_type_written`,
`trl_execute`, `trl_describe`, `trl_deallocate`, `trl_statement_free`:
a statement prepared once and run many times.

### SYNOPSIS

```c
trl_status trl_prepare(trl_conn *conn, const char *sql, trl_statement **out);
size_t trl_statement_parameter_count(const trl_statement *statement);
const trl_data_type *trl_statement_parameter_type(const trl_statement *statement, size_t parameter);
size_t trl_statement_column_count(const trl_statement *statement);
const char *trl_statement_column_name(const trl_statement *statement, size_t column);
const trl_data_type *trl_statement_column_type(const trl_statement *statement, size_t column);
const char *trl_statement_column_type_name(const trl_statement *statement, size_t column);
const char *trl_statement_column_type_written(const trl_statement *statement, size_t column);
trl_status trl_execute(trl_conn *conn, const trl_statement *statement, const trl_params *params,
                       trl_result **out);
trl_status trl_describe(trl_conn *conn, const trl_statement *statement, trl_statement **out);
trl_status trl_deallocate(trl_conn *conn, const trl_statement *statement);
void trl_statement_free(trl_statement *statement);
```

### DESCRIPTION

`trl_prepare` sends Prepare under a number of the connection's own, each
statement prepared taking the next, so no statement of the session is
replaced, nor one whose cursor is open; Prepared gives its parameters,
a statement's inputs alone, and its columns
[PROTOCOL Prepare 'P'] [PROTOCOL Prepared 'P']. A CALL's columns are one
for each `?` of an OUT or INOUT parameter, in the procedure's parameter
order, named and typed as each; an INOUT `?` is both a parameter and a
column. `trl_statement_column_type_name` and
`trl_statement_column_type_written` give a column's declared type as
their `trl_result_` likes do.

`trl_execute` sends Execute, cursor 0, with `params`, null for none, its
`ends` set under autocommit, and reads its replies as `trl_query` does,
every row kept: a query's Rows with no Description, its columns those
Prepared gave; a CALL's one row of its OUT and INOUT parameters' values;
another statement's Complete [PROTOCOL Execute 'E']. A statement is
valid across transactions, until it is deallocated or the session reset.

`trl_describe` sends Describe of the statement, answered by Prepared,
and gives a new statement of the same number [PROTOCOL Describe 'Y'];
it needs `describe`. `trl_deallocate` sends Deallocate
[PROTOCOL Deallocate 'D']. `trl_statement_free` frees the handle alone,
sending nothing.

### ERRORS

As `trl_query`'s; and "the server sent no Prepared". SQLSTATE 26000 of
a statement deallocated or unknown to the server, 24000 of one whose
cursor is open deallocated. `trl_describe` without `describe` gives
SQLSTATE 08006, sending nothing, the connection kept.

### EXAMPLES

[Prepared Statements](../user/04-prepared-statements.md) runs each.

## Cursors

### NAME

`trl_cursor`, `trl_cursor_open`, `trl_cursor_fetch`, `trl_cursor_close`,
`trl_describe_cursor`: a query's rows read in parts.

### SYNOPSIS

```c
typedef struct trl_cursor {
    const trl_statement *statement;
    uint32_t number;
} trl_cursor;

trl_status trl_cursor_open(trl_conn *conn, trl_cursor cursor, const trl_params *params, uint32_t rows,
                           trl_result **out);
trl_status trl_cursor_fetch(trl_conn *conn, trl_cursor cursor, uint32_t rows, trl_result **out);
trl_status trl_cursor_close(trl_conn *conn, trl_cursor cursor);
trl_status trl_describe_cursor(trl_conn *conn, trl_cursor cursor, trl_result **out);
```

### DESCRIPTION

A cursor is a prepared statement and a number, 0 unless the server
granted `cursors`, with which a statement has several open at once.
`trl_cursor_open` sends Execute of at most `rows` rows, tail 3 naming
the cursor if it is not 0; its batch's `trl_result_more` says whether
Suspended left the cursor open with more, else Complete closed it
[PROTOCOL Execute 'E']. `trl_cursor_fetch` sends Fetch of the next
batch [PROTOCOL Fetch 'F'], and `trl_cursor_close` sends Close
[PROTOCOL Close 'C']. `trl_describe_cursor` sends Describe of an open
cursor, answered by Description, and gives a result of its columns and
no rows [PROTOCOL Describe 'Y'].

A cursor left open keeps its transaction. Under autocommit with
`fetch-ends`, Execute and Fetch end the transaction as the cursor ends:
the fetch of the last row commits, a failure rolls back, and Close
commits [CLIENT What the server tells].

### ERRORS

As `trl_execute`'s. SQLSTATE 24000 of a cursor opened that is open, or
fetched or closed that is not. A cursor numbered other than 0 without
`cursors`, or a cursor described without `describe`, gives SQLSTATE
08006, sending nothing, the connection kept.

### EXAMPLES

[Prepared Statements](../user/04-prepared-statements.md) and
[Transactions](../user/05-transactions.md) run each.

## Several statements

### NAME

`trl_query_many`, `trl_ran_count`, `trl_ran_result`, `trl_ran_failure`,
`trl_ran_free`: several statements as one request.

### SYNOPSIS

```c
trl_status trl_query_many(trl_conn *conn, const char *sql, trl_ran **out);
size_t trl_ran_count(const trl_ran *ran);
const trl_result *trl_ran_result(const trl_ran *ran, size_t index);
const trl_failure *trl_ran_failure(const trl_ran *ran);
void trl_ran_free(trl_ran *ran);
```

### DESCRIPTION

`trl_query_many` sends Query with tail 2, `multi`: the statements run
one by one, `ends` applying to each, each Complete but the last marked
by tail 5, and the first failure's Error ends the replies
[PROTOCOL Query 'Q']. It gives each outcome before the failure, in
order, and the failure, or null if every statement succeeded
[CLIENT Controlling the session].

### ERRORS

`TRL_FAILED` without `multi-statement`, sending nothing, SQLSTATE 08006,
the connection kept; and as `trl_query`'s of a connection lost. A
statement's failure is not the call's: `trl_ran_failure` gives it.

### EXAMPLES

[Running Queries](../user/03-queries.md) runs it.

## What a completion tells

### NAME

`trl_conn_autocommit`, `trl_conn_set_autocommit`,
`trl_conn_in_transaction`, `trl_conn_warning`, `trl_conn_rows_affected`,
`trl_conn_generated_count`, `trl_conn_generated_at`,
`trl_conn_server_time`, `trl_conn_commit_position`,
`trl_conn_condition_count`, `trl_conn_condition_at`: the session's
transaction, and what the last statement's completion told.

### SYNOPSIS

```c
typedef struct trl_timing {
    uint64_t total;
    uint64_t queued;
    uint64_t locks;
} trl_timing;

trl_status trl_conn_set_autocommit(trl_conn *conn, bool on);
bool trl_conn_autocommit(const trl_conn *conn);
bool trl_conn_in_transaction(const trl_conn *conn);
const char *trl_conn_warning(const trl_conn *conn);
bool trl_conn_rows_affected(const trl_conn *conn, uint64_t *rows);
size_t trl_conn_generated_count(const trl_conn *conn);
const char *trl_conn_generated_at(const trl_conn *conn, size_t index);
bool trl_conn_server_time(const trl_conn *conn, trl_timing *timing);
bool trl_conn_commit_position(const trl_conn *conn, uint64_t *position);
size_t trl_conn_condition_count(const trl_conn *conn);
const trl_failure *trl_conn_condition_at(const trl_conn *conn, size_t index);
```

### DESCRIPTION

Under autocommit each statement is sent with `ends`, committed as it
succeeds and rolled back as it fails [CLIENT Running statements].
`trl_conn_in_transaction` gives whether the session's transaction is
active, as the last reply that told it said.

Each Complete replaces what the last told [PROTOCOL Complete 'K']. With
`row-counts`, tag 6 is the rows a statement changed, or a query's rows
in all, and tag 18 the server's time in microseconds, in all, queued and
waiting for locks; with `generated-values`, tag 16 is each identity
value an INSERT generated, as SQL writes it, in the order of its rows;
with `conditions`, tag 17 is each condition the statement raised. `trl_conn_commit_position` gives tag 4, the
log position of the last commit, with `commit-position`, kept until a
later commit tells another. `trl_conn_warning` gives the completion's
SQLSTATE if it is of class 01, else null. Each function that gives a
`bool` gives whether it was told.

### ERRORS

`trl_conn_set_autocommit` gives `TRL_INVALID_ARGUMENT` of a null
connection; the rest give 0, false or null of one.

### EXAMPLES

[Running Queries](../user/03-queries.md) and
[Transactions](../user/05-transactions.md) run each.

## Notices

### NAME

`trl_notice_handler`, `trl_conn_set_notice_handler`,
`trl_conn_notice_count`, `trl_conn_notice_at`, `trl_conn_clear_notices`:
the notices the server sends.

### SYNOPSIS

```c
typedef void (*trl_notice_handler)(void *context, const trl_failure *notice);

trl_status trl_conn_set_notice_handler(trl_conn *conn, trl_notice_handler handler, void *context);
size_t trl_conn_notice_count(const trl_conn *conn);
const trl_failure *trl_conn_notice_at(const trl_conn *conn, size_t index);
void trl_conn_clear_notices(trl_conn *conn);
```

### DESCRIPTION

A Notice is given to the handler as it is read, within the call that
read it, the failure valid until the handler returns; with no handler,
the last 64 are kept, the oldest dropped first, until cleared
[CLIENT What the server tells]. A statement's conditions come as
Notices where `conditions` was not granted, and a failing statement's
always; the server's own notices, as 01T01 of a transaction Reset or
ChangeUser rolled back, always [PROTOCOL Errors and notices]. A null
handler keeps notices again.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null connection.

### EXAMPLES

[Errors and Diagnostics](../user/10-errors.md) runs each.

## The session's control

### NAME

`trl_conn_reset`, `trl_conn_change_user`,
`trl_conn_set_query_attributes`: a session renewed, made another
user's, or its statements given attributes.

### SYNOPSIS

```c
trl_status trl_conn_reset(trl_conn *conn);
trl_status trl_conn_change_user(trl_conn *conn, const char *user, trl_mechanism *mechanism);
trl_status trl_conn_set_query_attributes(trl_conn *conn, const char *const *names, const char *const *values,
                                         size_t count);
```

### DESCRIPTION

`trl_conn_reset` sends Reset: the transaction rolled back, told by a
Notice 01T01 if one was active; the prepared statements, cursors, a
copy, temporary tables and settings gone, a Status of each setting
told; the authentication and the cancel key kept; then a Complete
saying no transaction is active [PROTOCOL Reset 'W']. It needs `reset`.

`trl_conn_change_user` sends ChangeUser with the mechanism's first
bytes, then answers each Challenge as a login does. Accepted, the
session is renewed as by Reset, under a new cancel key, and Ready tells
the new authorization; refused, the connection fails
[PROTOCOL ChangeUser 'U']. It needs `change-user`. The mechanism serves
one exchange at a time, as at login.

`trl_conn_set_query_attributes` sets the attributes every later Query
and Execute carries in tail 1, with `query-attributes`, until it is
called again; a count of 0 clears them, needing nothing granted
[CLIENT Controlling the session]. Names and values are copied.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer. `TRL_FAILED` with SQLSTATE
08006, sending nothing, the connection kept, without the capability, or
of attributes past their bounds: more than 32, a name empty or longer
than 64 bytes, a value longer than 1024, or either not UTF-8
[PROTOCOL The tail]. ChangeUser refused fails with the server's Error,
of class 28, and loses the connection.

### EXAMPLES

[Transactions](../user/05-transactions.md) runs each.

### STANDARD

[CLIENT Running statements], [CLIENT Prepared statements],
[CLIENT What the server tells], [CLIENT Controlling the session], and
the sections of [PROTOCOL Conversations] each section names.

### SEE ALSO

[Values](23-values.md), [Errors](30-errors.md),
[Connections](21-connections.md)
