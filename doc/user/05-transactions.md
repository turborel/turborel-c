# Transactions

[Contents](README.md)

A session's statements run in transactions, each ended by COMMIT or
ROLLBACK, or, under autocommit, each statement its own. This chapter
shows both, how a cursor and a large object's locator keep their
transaction, and how a program resets its session, changes its user, or
gives its statements attributes. [Statements and Cursors](../client/22-statements.md)
gives each function in full.

## A transaction

A statement begins a transaction if none is active, and the transaction
lasts until COMMIT or ROLLBACK. `trl_conn_in_transaction` says whether
one is active, as the server said after the last statement:

```c
printf("before: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
run(conn, "CREATE TABLE ACCOUNTS (ID INTEGER PRIMARY KEY, BALANCE INTEGER)");
run(conn, "INSERT INTO ACCOUNTS VALUES (1, 100), (2, 50)");
printf("after INSERT: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
run(conn, "COMMIT");
printf("after COMMIT: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
run(conn, "UPDATE ACCOUNTS SET BALANCE = BALANCE - 70 WHERE ID = 2");
run(conn, "ROLLBACK");
trl_result *result = NULL;
if (trl_query(conn, "SELECT BALANCE FROM ACCOUNTS WHERE ID = 2", &result) == TRL_OK) {
    trl_decimal balance;
    char text[TRL_DECIMAL_TEXT_BYTES];
    trl_value_decimal(trl_result_value(result, 0, 0), &balance);
    trl_decimal_text(&balance, text, sizeof text);
    printf("balance after ROLLBACK: %s\n", text);
}
trl_result_free(result);
```

```
before: none
after INSERT: active
after COMMIT: none
balance after ROLLBACK: 50
```

A session ended by `trl_conn_terminate` with a transaction active ends
it as the setting SESSION.COMMIT_AT_END says, committed unless it is
OFF; a connection freed without Terminate, or lost, has it rolled back.
A statement that fails does not, of itself, end the transaction: whether
it is still active, `trl_failure_active` tells of the failure
([Errors and Diagnostics](10-errors.md)).

## Autocommit

Under autocommit, which `trl_conn_set_autocommit` turns on, each
statement is its own transaction: the server commits it as it succeeds,
and rolls it back as it fails, with no COMMIT sent. A commit that fails,
as a deferred constraint can, fails the statement in its place:

```c
trl_conn_set_autocommit(conn, true);
run(conn, "CREATE TABLE NOTES (ID INTEGER PRIMARY KEY, BODY VARCHAR(40))");
run(conn, "INSERT INTO NOTES VALUES (1, 'kept')");
printf("after INSERT: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
run(conn, "INSERT INTO NOTES VALUES (1, 'a second 1')");
trl_conn_set_autocommit(conn, false);
trl_result *result = NULL;
if (trl_query(conn, "SELECT COUNT(*) FROM NOTES", &result) == TRL_OK) {
    trl_decimal count;
    trl_value_decimal(trl_result_value(result, 0, 0), &count);
    printf("%llu note\n", (unsigned long long)count.units.low);
}
trl_result_free(result);
```

```
after INSERT: none
SQLCODE -201 (SQLSTATE 23000): PRIMARY KEY ANN.NOTES_PRIMARY1 (ID) of ANN.NOTES would be violated: a row of these values is there [X3.135-1986 §6.4 GR2]
1 note
```

### Cursors under autocommit

A cursor left open keeps its transaction, so its rows stay as they were.
With the server's capability `fetch-ends`, which the driver asks for, a
cursor opened under autocommit ends its transaction as it ends: the
fetch of its last row commits it, closing it commits it, and rows that
fail roll it back:

```c
trl_conn_set_autocommit(conn, true);
trl_statement *query = NULL;
trl_prepare(conn, "SELECT X FROM (VALUES (1), (2), (3)) AS V(X)", &query);
trl_cursor cursor = {query, 0};
trl_result *part = NULL;
trl_cursor_open(conn, cursor, NULL, 2, &part);
printf("open: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
trl_result_free(part);
trl_cursor_fetch(conn, cursor, 2, &part);
printf("last row read: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
trl_result_free(part);
trl_cursor_open(conn, cursor, NULL, 1, &part);
trl_result_free(part);
trl_cursor_close(conn, cursor);
printf("closed early: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
trl_statement_free(query);
```

```
open: active
last row read: none
closed early: none
```

Without `fetch-ends`, the transaction stays active after the cursor
ends, until the program commits it.

### Large objects' locators

A CLOB or BLOB value longer than the server's SERVER.LOB_INLINE_BYTES,
64 KiB unless its operator chose otherwise, comes as a locator, which is
valid only within its transaction. A result that holds one keeps its
transaction, under autocommit too, until the program commits or rolls
it back. [Large Objects](06-large-objects.md) tells how a locator is
read.

## Resetting a session

`trl_conn_reset` gives the session back as a new connection has it: its
transaction rolled back, its prepared statements, cursors and temporary
tables gone, and its settings as they were at login; its user and the
connection stay. A transaction rolled back is told by a notice of
SQLSTATE 01T01:

```c
run(conn, "CREATE TABLE DRAFTS (ID INTEGER)");
trl_statement *query = NULL;
trl_prepare(conn, "SELECT ID FROM DRAFTS", &query);
if (trl_conn_reset(conn) == TRL_OK) {
    printf("transaction: %s\n", trl_conn_in_transaction(conn) ? "active" : "none");
    for (size_t i = 0; i < trl_conn_notice_count(conn); ++i) {
        const trl_failure *notice = trl_conn_notice_at(conn, i);
        printf("notice %s: %s\n", trl_failure_sqlstate(notice), trl_failure_message(notice));
    }
}
trl_result *result = NULL;
if (trl_execute(conn, query, NULL, &result) == TRL_FAILED) {
    print_failure(conn);
}
printf("still %s\n", trl_conn_authorization(conn));
trl_statement_free(query);
```

```
transaction: none
notice 01T01: [DESIGN] Reset rolled back the transaction that was active
SQLCODE -921 (SQLSTATE 26000): there is no prepared statement 0 [ISO/IEC 9075:1992 §17.10 GR1]
still ANN
```

## Changing the user

`trl_conn_change_user` makes the session another user's, proved by a
mechanism as at login, without a new connection: the session is renewed
as by a reset, its transaction rolled back. A refusal ends the
connection:

```c
run(conn, "CREATE USER BOB PASSWORD 'bob''s secret'");
run(conn, "COMMIT");
run(conn, "CREATE TABLE SCRATCH (ID INTEGER)");
trl_mechanism *as_bob = NULL;
trl_mechanism_trust(&as_bob);
if (trl_conn_change_user(conn, "bob", as_bob) == TRL_OK) {
    printf("now %s, transaction: %s\n", trl_conn_authorization(conn),
           trl_conn_in_transaction(conn) ? "active" : "none");
} else {
    print_failure(conn);
}
trl_mechanism_free(as_bob);
```

```
now BOB, transaction: none
```

It needs the server's capability `change-user`, as a reset needs
`reset`; without it, the call fails before it sends anything.

## Query attributes

Attributes of the program's, each a name and a value, go with every
statement the session sends after `trl_conn_set_query_attributes`, until
it is called again; the SQL function QUERY_ATTRIBUTE reads one. A
statement carries at most 32, each a name of 1 to 64 bytes and a value
of at most 1024, in UTF-8; more are refused before anything is sent,
with SQLSTATE 08006, the connection kept:

```c
const char *names[] = {"request"};
const char *values[] = {"r-1842"};
trl_result *result = NULL;
if (trl_conn_set_query_attributes(conn, names, values, 1) == TRL_OK &&
    trl_query(conn, "VALUES (QUERY_ATTRIBUTE('request'))", &result) == TRL_OK) {
    size_t length = 0;
    const char *text = trl_value_text(trl_result_value(result, 0, 0), &length);
    printf("%.*s\n", (int)length, text);
}
trl_result_free(result);
const char *empty[] = {""};
if (trl_conn_set_query_attributes(conn, empty, values, 1) == TRL_FAILED) {
    print_failure(conn);
}
```

```
r-1842
SQLCODE -919 (SQLSTATE 08006): a statement takes at most 32 attributes, each a name of 1 to 64 bytes and a value of at most 1024, UTF-8 [DESIGN]
```
