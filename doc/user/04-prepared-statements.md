# Prepared Statements

[Contents](README.md)

A statement prepared once is run many times, with other values each
time. This chapter shows how a program prepares a statement, its
parameters written `?`, gives them values and executes it; how it reads
a query's rows through a cursor, a part at a time, several cursors of one
statement at once; how it calls a procedure; and how it describes a
statement and lets it go. [Statements and Cursors](../client/22-statements.md)
gives each function in full.

## Preparing and executing

`trl_prepare` sends a statement's text, each dynamic parameter written
`?`, and gives a *statement*: the server has parsed it, and told the
type each parameter takes and the columns a query gives. A parameter
list, `trl_params`, holds the values of one execution, in the order of
the `?`s; `trl_execute` runs the statement with them, as many times as
the program wants:

```c
run(conn, "CREATE TABLE PARTS (ID INTEGER PRIMARY KEY, NAME VARCHAR(20), PRICE DECIMAL(7,2))");
trl_statement *insert = NULL;
trl_params *params = NULL;
if (trl_prepare(conn, "INSERT INTO PARTS VALUES (?, ?, ?)", &insert) != TRL_OK || trl_params_new(&params) != TRL_OK) {
    print_failure(conn);
    return 1;
}
printf("%zu parameters\n", trl_statement_parameter_count(insert));
const char *names[] = {"bolt", "nut", "washer"};
const char *prices[] = {"0.25", "0.10", "0.05"};
for (int i = 0; i < 3; ++i) {
    trl_decimal id = {{(uint64_t)i + 1, 0}, 0};
    trl_decimal price;
    trl_decimal_parse(prices[i], &price);
    trl_params_clear(params);
    trl_params_add_decimal(params, id);
    trl_params_add_text(params, names[i], strlen(names[i]));
    trl_params_add_decimal(params, price);
    trl_result *result = NULL;
    if (trl_execute(conn, insert, params, &result) == TRL_OK) {
        uint64_t rows = 0;
        trl_conn_rows_affected(conn, &rows);
        printf("inserted %llu\n", (unsigned long long)rows);
    } else {
        print_failure(conn);
    }
    trl_result_free(result);
}
trl_params_free(params);
trl_statement_free(insert);
run(conn, "COMMIT");
```

```
3 parameters
inserted 1
inserted 1
inserted 1
```

A parameter list copies each value it is given, so the program's own may
go at once; `trl_params_clear` empties it for the next execution. A
statement run once takes one too: `trl_query_values` prepares it,
executes it with the list and destroys it, as `trl_query_params` does
with text. Each kind of value has its function:

| Function | Gives a parameter of |
|---|---|
| `trl_params_add_null` | the null value, of whatever type the parameter takes |
| `trl_params_add_text` | a character string, UTF-8 |
| `trl_params_add_bytes` | a binary string |
| `trl_params_add_decimal` | an exact number |
| `trl_params_add_double` | an approximate number |
| `trl_params_add_boolean` | a boolean |
| `trl_params_add_datetime` | a date, time or timestamp, its kind given |
| `trl_params_add_interval` | an interval |
| `trl_params_add_decfloat` | a decimal floating-point number |
| `trl_params_add_uuid` | a UUID, of its 16 octets; `trl_uuid_parse` makes them of its text |
| `trl_params_add_json` | a JSON value, of its JSON text |
| `trl_params_add_xml` | an XML value, of its serialization |
| `trl_params_add_value` | a copy of a value read, of any kind |

The server casts each value to the type its parameter takes, as SQL's
assignment does, and fails the execution if it cannot. Text that is not
UTF-8 is refused as it is added, with `TRL_INVALID_ARGUMENT`, before
anything is sent; JSON text that is not JSON, or nested too deep, with
`TRL_FAILED`, the list keeping the failure, read by
`trl_params_failure`.

## A statement's parameters and columns

A statement tells the type of each parameter, and of a query each
column's name and type, before it runs:

```c
trl_statement *query = NULL;
if (trl_prepare(conn, "SELECT NAME, PRICE * ? FROM PARTS WHERE ID = ?", &query) == TRL_OK) {
    for (size_t i = 0; i < trl_statement_parameter_count(query); ++i) {
        const trl_data_type *type = trl_statement_parameter_type(query, i);
        printf("parameter %zu: kind %d precision %u\n", i, (int)trl_type_kind(type), trl_type_precision(type));
    }
    for (size_t i = 0; i < trl_statement_column_count(query); ++i) {
        const char *name = trl_statement_column_name(query, i);
        printf("column %zu: %s, kind %d\n", i, name == NULL ? "(none)" : name,
               (int)trl_type_kind(trl_statement_column_type(query, i)));
    }
}
trl_statement_free(query);
```

```
parameter 0: kind 2 precision 7
parameter 1: kind 2 precision 10
column 0: NAME, kind 5
column 1: (none), kind 2
```

A statement stays valid across transactions, until it is deallocated,
the session is reset, or the connection ends. `trl_describe` asks the
server for its parameters and columns again, as a new statement of the
same number, after a change to a table it reads.

## Cursors

`trl_execute` keeps all a query's rows. A *cursor* reads them in parts:
`trl_cursor_open` executes the statement and takes the first rows, at
most as many as asked; `trl_cursor_fetch` takes the next part;
`trl_result_more` says whether the cursor has more; and
`trl_cursor_close` closes it before its end. A cursor is named by its
statement and a number, 0 for the statement's one cursor:

```c
trl_statement *query = NULL;
if (trl_prepare(conn, "SELECT NAME FROM PARTS ORDER BY ID", &query) != TRL_OK) {
    print_failure(conn);
    return 1;
}
trl_cursor cursor = {query, 0};
trl_result *part = NULL;
bool more = trl_cursor_open(conn, cursor, NULL, 2, &part) == TRL_OK;
while (more) {
    for (size_t row = 0; row < trl_result_row_count(part); ++row) {
        size_t length = 0;
        const char *name = trl_value_text(trl_result_value(part, row, 0), &length);
        printf("%.*s ", (int)length, name);
    }
    printf("| more: %s\n", trl_result_more(part) ? "yes" : "no");
    more = trl_result_more(part);
    trl_result_free(part);
    part = NULL;
    if (more && trl_cursor_fetch(conn, cursor, 2, &part) != TRL_OK) {
        print_failure(conn);
        more = false;
    }
}
if (trl_cursor_fetch(conn, cursor, 2, &part) != TRL_OK) {
    print_failure(conn);
}
trl_statement_free(query);
```

```
bolt nut | more: yes
washer | more: no
SQLCODE -501 (SQLSTATE 24000): the cursor is not open [ISO/IEC 9075:1992 §13.3 GR1, §13.4 GR1]
```

A cursor is closed once its last row is read; fetching from one closed,
or one never opened, fails with SQLSTATE 24000, as does opening one
already open.

### Several cursors of one statement

With the server's capability `cursors`, a statement has several cursors
open at once, each of its own number; `trl_describe_cursor` gives an
open cursor's columns:

```c
trl_statement *query = NULL;
trl_prepare(conn, "SELECT ID FROM PARTS ORDER BY ID", &query);
trl_cursor first = {query, 1};
trl_cursor second = {query, 2};
trl_result *part = NULL;
trl_cursor_open(conn, first, NULL, 1, &part);
trl_result_free(part);
trl_cursor_open(conn, second, NULL, 2, &part);
trl_result_free(part);
if (trl_cursor_fetch(conn, first, 1, &part) == TRL_OK) {
    trl_decimal id;
    char text[TRL_DECIMAL_TEXT_BYTES];
    trl_value_decimal(trl_result_value(part, 0, 0), &id);
    trl_decimal_text(&id, text, sizeof text);
    printf("first cursor, next row: %s\n", text);
}
trl_result_free(part);
trl_result *columns = NULL;
if (trl_describe_cursor(conn, second, &columns) == TRL_OK) {
    printf("second cursor: %zu column, %s\n", trl_result_column_count(columns), trl_result_column_name(columns, 0));
}
trl_result_free(columns);
trl_cursor_close(conn, first);
trl_cursor_close(conn, second);
trl_statement_free(query);
```

```
first cursor, next row: 2
second cursor: 1 column, ID
```

Without `cursors`, a cursor numbered other than 0 fails before anything
is sent.

## Calling a procedure

A CALL prepared with a `?` for each of a procedure's OUT and INOUT
parameters has a column for each, in the procedure's order, named as the
parameter; an INOUT `?` is both a parameter and a column. Its execution
gives one row, the parameters' values once the procedure ran:

```c
run(conn, "CREATE PROCEDURE SCALE (IN FACTOR INTEGER, INOUT AMOUNT INTEGER, OUT DOUBLED INTEGER) CONTAINS SQL "
          "BEGIN SET AMOUNT = AMOUNT * FACTOR; SET DOUBLED = AMOUNT * 2; END");
trl_statement *call = NULL;
trl_params *params = NULL;
trl_result *result = NULL;
if (trl_prepare(conn, "CALL SCALE(3, ?, ?)", &call) == TRL_OK && trl_params_new(&params) == TRL_OK) {
    printf("%zu parameter, %zu columns: %s %s\n", trl_statement_parameter_count(call),
           trl_statement_column_count(call), trl_statement_column_name(call, 0), trl_statement_column_name(call, 1));
    trl_decimal amount = {{5, 0}, 0};
    trl_params_add_decimal(params, amount);
    if (trl_execute(conn, call, params, &result) == TRL_OK) {
        for (size_t column = 0; column < trl_result_column_count(result); ++column) {
            trl_decimal value;
            char text[TRL_DECIMAL_TEXT_BYTES];
            trl_value_decimal(trl_result_value(result, 0, column), &value);
            trl_decimal_text(&value, text, sizeof text);
            printf("%s = %s\n", trl_result_column_name(result, column), text);
        }
    } else {
        print_failure(conn);
    }
} else {
    print_failure(conn);
}
trl_result_free(result);
trl_params_free(params);
trl_statement_free(call);
```

```
1 parameter, 2 columns: AMOUNT DOUBLED
AMOUNT = 15
DOUBLED = 30
```

## Letting a statement go

`trl_deallocate` tells the server to forget a statement; executing it
after fails with SQLSTATE 26000. A statement whose cursor is open is not
deallocated, failing with SQLSTATE 24000, until the cursor is closed.
`trl_statement_free` frees the program's handle, and is called after
`trl_deallocate`, or alone when the session ends: the server forgets a
session's statements as it ends.

```c
trl_statement *query = NULL;
trl_result *result = NULL;
trl_prepare(conn, "SELECT ID FROM PARTS", &query);
if (trl_deallocate(conn, query) == TRL_OK && trl_execute(conn, query, NULL, &result) == TRL_FAILED) {
    print_failure(conn);
}
trl_statement_free(query);
```

```
SQLCODE -921 (SQLSTATE 26000): there is no prepared statement 0 [ISO/IEC 9075:1992 §17.10 GR1]
```
