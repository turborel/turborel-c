# The C API

[Contents](README.md)

## NAME

`libturborel`'s standard interface: a libpq-style C API of handles,
statuses and results, and the three calls that make its shortest path,
`trl_connect_uri`, `trl_query_params` and `trl_result_text`.

## SYNOPSIS

```
#include <turborel/turborel.h>

trl_status trl_connect_uri(const char *uri, trl_conn **out);
trl_status trl_query(trl_conn *conn, const char *sql, trl_result **out);
trl_status trl_query_params(trl_conn *conn, const char *sql, size_t count, const char *const *values,
                            trl_result **out);
size_t trl_result_row_count(const trl_result *result);
size_t trl_result_column_count(const trl_result *result);
const char *trl_result_column_name(const trl_result *result, size_t column);
const char *trl_result_text(trl_result *result, size_t row, size_t column);
void trl_result_free(trl_result *result);
const trl_failure *trl_conn_failure(const trl_conn *conn);
void trl_conn_free(trl_conn *conn);
```

## DESCRIPTION

The C API is the whole of `turborel/turborel.h`. A program holds opaque
handles: a connection, `trl_conn`; a result, `trl_result`; a prepared
statement, `trl_statement`; and the rest that the pages of the low-level
client describe. Each call answers with a `trl_status`: TRL_OK;
TRL_FAILED, the failure kept by the handle and read by its `_failure`
function; TRL_NO_MEMORY; or TRL_INVALID_ARGUMENT for an argument the
call cannot take. Each function's comment in the header says who owns
what it gives. A handle the program is given is freed by its `_free`
function, and a borrowed pointer is valid while its handle is
(RULES.md D10). A failure gives its SQLCODE, SQLSTATE, message and
diagnostics, as [Errors](30-errors.md) describes.

A program written against libpq finds each of its calls here:

| libpq | The C API |
|---|---|
| `PQconnectdb` | `trl_connect_uri` |
| `PQconnectdbParams` | `trl_options_set`, then `trl_settings_resolve` and `trl_settings_connect` ([Settings](26-settings.md)) |
| `PQstatus`, `PQerrorMessage` | `trl_conn_failure`, `trl_failure_sqlstate`, `trl_failure_message` |
| `PQfinish` | `trl_conn_terminate`, then `trl_conn_free` |
| `PQexec` | `trl_query` |
| `PQexecParams` | `trl_query_params` |
| `PQprepare`, `PQexecPrepared` | `trl_prepare`, `trl_execute` ([Statements and Cursors](22-statements.md)) |
| `PQntuples`, `PQnfields`, `PQfname` | `trl_result_row_count`, `trl_result_column_count`, `trl_result_column_name` |
| `PQgetvalue` | `trl_result_text`, or `trl_result_value` for the typed value ([Values](23-values.md)) |
| `PQgetisnull` | `trl_result_text` giving null, or `trl_value_is_null` |
| `PQcmdTuples` | `trl_conn_rows_affected` |
| `PQclear` | `trl_result_free` |
| `PQcancel` | `trl_cancel` ([Connections](21-connections.md)) |
| `lo_*` | the large object calls ([Large Objects](24-large-objects.md)) |
| `PQputCopyData`, `PQgetCopyData` | the copy calls ([Copy, Import and Export](25-copy.md)) |
| `PQsetNoticeProcessor` | `trl_conn_set_notice_handler` |
| pipeline mode | the pipeline calls ([Pipelines](27-pipelines.md)) |

`trl_connect_uri` connects as a `turborel://` URI says. The URI is laid
with the rest of a connection's settings, the environment's and the
service file's among them, as `trl_settings_from_uri` lays it, and the
connection is made by `trl_settings_connect`. [Settings](26-settings.md)
gives the URI's form. The connection is the caller's to free even when
the call fails: it then holds the failure, of a URI that cannot be read
as well as of a server that cannot be reached.

`trl_query_params` runs one statement with its `?` parameters given as
text, each a null-ended UTF-8 string, or a null pointer for the null
value. The statement is prepared, executed with each value as a
character string, which the server assigns to the parameter's type, and
then destroyed. With no parameter it runs the statement by Query, as
`trl_query` does. A failure is the connection's, as `trl_query`'s is;
the result is made only on TRL_OK.

`trl_result_text` gives a value's text, as `trl_value_to_text` writes it,
by its row and column, both from 0. The text is made once and kept by
the result, so it is valid until the result is freed. It is null for
the null value, for a row or column past the last, and when memory
cannot be had.

## ERRORS

Each call fails as the calls it is made of fail. `trl_connect_uri`
fails as `trl_settings_from_uri` and `trl_settings_connect` do: 08001
for a URI or a setting that cannot be read; 08006 for a server that
cannot be reached, or of several servers 08001 naming each; 28000 for a
user the server refuses. `trl_query_params`
fails as `trl_prepare` and `trl_execute` do, with the server's SQLSTATE
for a statement it refuses. Each returns TRL_INVALID_ARGUMENT for a null
handle, statement or `out`, or for parameters counted but not given.

## EXAMPLES

A program connects by a URI, makes a table, adds its rows with their
values given as text, and reads them back:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_conn *conn = NULL;
    if (trl_connect_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable", &conn) != TRL_OK) {
        printf("%s %s\n", trl_failure_sqlstate(trl_conn_failure(conn)), trl_failure_message(trl_conn_failure(conn)));
        trl_conn_free(conn);
        return 1;
    }
    trl_result *result = NULL;
    trl_query(conn, "CREATE TABLE CREW (ID INTEGER, NAME VARCHAR(20))", &result);
    trl_result_free(result);
    static const char *const crew[][2] = {{"1", "Ann"}, {"2", "Bob"}, {"3", NULL}};
    for (size_t i = 0; i < 3; ++i) {
        trl_query_params(conn, "INSERT INTO CREW VALUES (?, ?)", 2, crew[i], &result);
        trl_result_free(result);
    }

    const char *const most[] = {"2"};
    if (trl_query_params(conn, "SELECT ID, NAME FROM CREW WHERE ID >= ? ORDER BY ID", 1, most, &result) == TRL_OK) {
        printf("%s %s\n", trl_result_column_name(result, 0), trl_result_column_name(result, 1));
        for (size_t row = 0; row < trl_result_row_count(result); ++row) {
            const char *name = trl_result_text(result, row, 1);
            printf("%s %s\n", trl_result_text(result, row, 0), name != NULL ? name : "(null)");
        }
    }
    trl_result_free(result);

    if (trl_query_params(conn, "SELECT ID FROM CREW WHERE ID = ?", 1, (const char *const[]){"x"}, &result) != TRL_OK) {
        printf("%s %s\n", trl_failure_sqlstate(trl_conn_failure(conn)), trl_failure_message(trl_conn_failure(conn)));
    }
    trl_result_free(result);
    trl_conn_terminate(conn);
    trl_conn_free(conn);
    return 0;
}
```

```
ID NAME
2 Bob
3 (null)
22018 'x' is not a <signed numeric literal>
```

A URI that names no server that answers fails, and the connection holds
why:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_conn *conn = NULL;
    if (trl_connect_uri("turborel://ann@127.0.0.1:1/?tls_mode=disable&connect_timeout=2", &conn) != TRL_OK) {
        printf("%s\n", trl_failure_sqlstate(trl_conn_failure(conn)));
    }
    trl_conn_free(conn);
    return 0;
}
```

```
08006
```

## STANDARD

The standard API is the driver's own, after libpq's shape (RULES.md D2).
Beneath it are [CLIENT Connecting], [CLIENT Connecting by settings],
[CLIENT The connection URI], [CLIENT Running statements] and
[CLIENT Prepared statements]. It fulfils rows S-API-1 to S-API-8 of
[CONFORMANCE.md](../../CONFORMANCE.md).

## SEE ALSO

[The ODBC Driver](01-api-odbc.md), [Connections](21-connections.md),
[Statements and Cursors](22-statements.md), [Values](23-values.md),
[Settings](26-settings.md), [Errors](30-errors.md),
[Running Queries](../user/03-queries.md).
