# Errors and Diagnostics

[Contents](README.md)

A call that cannot do what it was asked gives a status, and, if the
server or the connection failed it, a *failure* the handle keeps. This
chapter shows what a failure tells, which failures end the connection,
the failures the driver raises itself, and how warnings come: as a
statement's conditions, or as notices. [Errors](../client/30-errors.md)
gives each function in full, and [Appendix B](../appendix/b-errors.md)
each SQLSTATE.

## Statuses

Every call that can fail returns a `trl_status`:

| Status | Means |
|---|---|
| `TRL_OK` | it did what it was asked |
| `TRL_FAILED` | the server, or the connection, failed it: the handle keeps the failure |
| `TRL_INVALID_ARGUMENT` | an argument was null, or of another kind; nothing was done |
| `TRL_INVALID_TEXT` | text given was not of the form asked, as JSON text that is not JSON |
| `TRL_OUT_OF_RANGE` | a number given is past what it may be |
| `TRL_NO_MEMORY` | memory could not be had for the handle itself, which is not made |

Only `TRL_FAILED` leaves a failure to read. A program tests every call
the same way, against `TRL_OK`.

## A failure

`trl_conn_failure` gives the connection's last failure, valid until the
next call on the connection. It tells its SQLCODE and SQLSTATE; its
message, in plain words; the rule it cites, apart, as `DESIGN` or a
section of a standard; its position in the statement, if it has one; and
whether the session's transaction is still active after it.
`trl_failure_text` writes all of it as one line:

```c
trl_result *result = NULL;
if (trl_query(conn, "SELECT NAME FROM NOWHERE", &result) == TRL_FAILED) {
    const trl_failure *failure = trl_conn_failure(conn);
    uint64_t position = 0;
    printf("SQLCODE %d, SQLSTATE %s\n", trl_failure_sqlcode(failure), trl_failure_sqlstate(failure));
    printf("message: %s\n", trl_failure_message(failure));
    printf("rule: %s\n", trl_failure_rule(failure));
    if (trl_failure_position(failure, &position)) {
        printf("at character %llu\n", (unsigned long long)position);
    }
    printf("transaction still active: %s\n", trl_failure_active(failure) ? "yes" : "no");
    print_failure(conn);
}
```

```
SQLCODE -103, SQLSTATE 2A000
message: table ANN.NOWHERE is not defined
rule: X3.135-1986 §5.4 SR5
at character 17
transaction still active: yes
SQLCODE -103 (SQLSTATE 2A000): table ANN.NOWHERE is not defined [X3.135-1986 §5.4 SR5] (at character 17)
```

### Diagnostics

With the server's capability `diagnostics`, which the driver asks for, a
failure also tells what it is about: its severity, detail, hint and
context, and the schema, table, column, data type or constraint it
concerns. `trl_failure_diagnostic` gives each, or null:

```c
run(conn, "CREATE TABLE MEMBERS (ID INTEGER, CONSTRAINT MEMBERS_KEY PRIMARY KEY (ID))");
run(conn, "INSERT INTO MEMBERS VALUES (1)");
trl_result *result = NULL;
if (trl_query(conn, "INSERT INTO MEMBERS VALUES (1)", &result) == TRL_FAILED) {
    const trl_failure *failure = trl_conn_failure(conn);
    printf("severity: %s\n", trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_SEVERITY));
    printf("schema: %s\n", trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_SCHEMA));
    printf("table: %s\n", trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_TABLE));
    printf("constraint: %s\n", trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_CONSTRAINT));
    printf("detail: %s\n", trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_DETAIL));
    const char *hint = trl_failure_diagnostic(failure, TRL_DIAGNOSTIC_HINT);
    printf("hint: %s\n", hint == NULL ? "(none)" : hint);
}
```

```
severity: ERROR
schema: ANN
table: MEMBERS
constraint: MEMBERS_KEY
detail: the key (1) is held by another row
hint: (none)
```

## Failures that end the connection

A failure of SQLSTATE class 08, but 08003, ends the connection, as does
one of class 28 as a connection is made: `trl_failure_ends_connection`
says so of a failure, and `trl_conn_is_lost` of the connection, whose
every call after fails at once with the failure that lost it. A
connection that cannot be read, that times out, or whose server sends a
message out of turn or malformed, is lost with SQLCODE -919 and SQLSTATE
08006. Every other failure leaves the connection as it was, ready for
the next statement:

```c
trl_result *result = NULL;
if (trl_query(conn, "VALUES (1 / 0)", &result) == TRL_FAILED) {
    print_failure(conn);
    printf("ends the connection: %s\n", trl_failure_ends_connection(trl_conn_failure(conn)) ? "yes" : "no");
}
if (trl_query(conn, "VALUES (1)", &result) == TRL_OK) {
    printf("the next statement runs: %zu row\n", trl_result_row_count(result));
}
trl_result_free(result);
```

```
SQLCODE -301 (SQLSTATE 22012): a divisor shall not be 0 [X3.135-1986 §5.9 GR5]
ends the connection: no
the next statement runs: 1 row
```

## Failures the driver raises

Some failures are the driver's own, raised before anything is sent or
as a reply is read. Each carries the SQLCODE and SQLSTATE Turborel's own
client library gives the same failure, so a program handles them as it
handles the server's, and cites the rule `DESIGN`:

| SQLCODE | SQLSTATE | When | The connection |
|---|---|---|---|
| -919 | 08006 | it is lost, times out, or its server sends what cannot be read | lost |
| -919 | 08006 | a method needs a capability the server did not grant, or query attributes are past their bounds | kept |
| -919 | 28000 | a password or SCRAM over plain TCP, a SCRAM exchange the server did not prove, or one not bound where binding is required | not made |
| -918 | TB918 | memory could not be had for what a call makes | kept |
| -916 | TB916 | a value the server sent that the driver cannot take, the rest of the statement's replies read past | kept |
| -900 | 0A000 | past Turborel's limits, or not built in | kept |
| -301 | 2200N | XML text that is not well-formed content | kept |
| -301 | 22000, 22021 and others | a value the protocol cannot carry | kept |
| -910 | TB910 | a misuse Turborel's client has no case for | kept |

A method that needs a capability the server did not grant fails before
it sends anything:

```c
trl_conn_options *options = NULL;
trl_conn *bare = NULL;
trl_conn_options_new(&options);
trl_conn_options_ask_none(options);
if (trl_connect(target, "ann", trust, options, &bare) == TRL_OK && trl_conn_reset(bare) == TRL_FAILED) {
    char line[256];
    trl_failure_text(trl_conn_failure(bare), line, sizeof line);
    printf("%s\n", line);
    printf("lost: %s\n", trl_conn_is_lost(bare) ? "yes" : "no");
}
trl_conn_free(bare);
trl_conn_options_free(options);
```

```
SQLCODE -919 (SQLSTATE 08006): the server did not grant reset [DESIGN]
lost: no
```

[Appendix B](../appendix/b-errors.md) lists every SQLSTATE the driver
raises, and the classes the server sends.

## Warnings

A statement that succeeds may yet warn. Its completion's SQLSTATE is
then of class 01, which `trl_conn_warning` gives, or null. Each
condition the statement raised, warnings and its completion's condition
among them, comes one of two ways: with the server's capability
`conditions`, which the driver asks for, in the statement's completion,
which `trl_conn_condition_count` and `trl_conn_condition_at` give; else
as a *notice*. A statement that fails sends its conditions as notices.

```c
run(conn, "CREATE TABLE READINGS (V INTEGER)");
run(conn, "INSERT INTO READINGS VALUES (NULL), (4)");
run(conn, "SELECT SUM(V) FROM READINGS");
printf("warning: %s\n", trl_conn_warning(conn));
for (size_t i = 0; i < trl_conn_condition_count(conn); ++i) {
    const trl_failure *condition = trl_conn_condition_at(conn, i);
    printf("condition %s: %s\n", trl_failure_sqlstate(condition), trl_failure_message(condition));
}
```

```
warning: 01003
condition 01003: the statement completed with warning: null value eliminated in set function
```

### Notices

The server sends its own notices, as of a transaction a reset rolled
back, and a statement's conditions where `conditions` was not granted.
A connection with no handler keeps the last 64 notices, the oldest
dropped first, which `trl_conn_notice_count` and `trl_conn_notice_at`
give and `trl_conn_clear_notices` clears. A handler,
`trl_conn_set_notice_handler`, is given each notice as it comes
instead, and none is kept. The handler first:

```c
static void heard(void *context, const trl_failure *notice) {
    (void)context;
    printf("heard %s: %s\n", trl_failure_sqlstate(notice), trl_failure_message(notice));
}
```

Then a reset with a transaction active:

```c
trl_conn_set_notice_handler(conn, heard, NULL);
run(conn, "CREATE TABLE TEMPORARY_WORK (ID INTEGER)");
trl_conn_reset(conn);
printf("kept: %zu\n", trl_conn_notice_count(conn));
```

```
heard 01T01: [DESIGN] Reset rolled back the transaction that was active
kept: 0
```

A handler runs within the call that read the notice, on its thread, and
must not call the driver on the same connection.
