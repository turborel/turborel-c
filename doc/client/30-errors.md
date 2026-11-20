# Errors

[Contents](README.md)

This page describes the driver's error: the status every call returns,
and the failure a handle keeps, its SQLCODE, SQLSTATE and message, the
rule it cites, its position, whether a transaction is still active, and
its diagnostics; and the failures the driver raises itself, each with
the SQLCODE and SQLSTATE turborel-client gives it. The User's Guide's
[Errors and Diagnostics](../user/10-errors.md) shows them at work, and
[Appendix B](../appendix/b-errors.md) lists every SQLSTATE.

## Statuses

### NAME

`trl_status`: what a call came to.

### SYNOPSIS

```c
#include <turborel/turborel.h>

typedef enum trl_status {
    TRL_OK = 0,
    TRL_INVALID_ARGUMENT,
    TRL_INVALID_TEXT,
    TRL_OUT_OF_RANGE,
    TRL_FAILED,
    TRL_NO_MEMORY,
} trl_status;
```

### DESCRIPTION

Every call that can fail returns a `trl_status`. `TRL_FAILED` says the
server, the connection or the driver failed the call, and the handle it
was made on keeps the failure; a function that makes a handle gives it
even then, holding the failure, so a program frees it either way.
`TRL_INVALID_ARGUMENT`, `TRL_INVALID_TEXT` and `TRL_OUT_OF_RANGE` say an
argument was refused, with nothing done and no failure kept.
`TRL_NO_MEMORY` says memory could not be had for the handle itself, and
nothing is made; memory not had for what a call does on a handle is a
failure of SQLCODE -918 (RULES.md D9).

## Failures

### NAME

`trl_failure`, `trl_diagnostic`, `trl_failure_sqlcode`,
`trl_failure_sqlstate`, `trl_failure_message`, `trl_failure_rule`,
`trl_failure_position`, `trl_failure_active`, `trl_failure_diagnostic`,
`trl_failure_ends_connection`, `trl_failure_text`: what a failure
tells.

### SYNOPSIS

```c
typedef enum trl_diagnostic {
    TRL_DIAGNOSTIC_SEVERITY = 0,
    TRL_DIAGNOSTIC_DETAIL,
    TRL_DIAGNOSTIC_HINT,
    TRL_DIAGNOSTIC_CONTEXT,
    TRL_DIAGNOSTIC_SCHEMA,
    TRL_DIAGNOSTIC_TABLE,
    TRL_DIAGNOSTIC_COLUMN,
    TRL_DIAGNOSTIC_DATA_TYPE,
    TRL_DIAGNOSTIC_CONSTRAINT,
} trl_diagnostic;

int32_t trl_failure_sqlcode(const trl_failure *failure);
const char *trl_failure_sqlstate(const trl_failure *failure);
const char *trl_failure_message(const trl_failure *failure);
const char *trl_failure_rule(const trl_failure *failure);
bool trl_failure_position(const trl_failure *failure, uint64_t *position);
bool trl_failure_active(const trl_failure *failure);
const char *trl_failure_diagnostic(const trl_failure *failure, trl_diagnostic which);
bool trl_failure_ends_connection(const trl_failure *failure);
size_t trl_failure_text(const trl_failure *failure, char *out, size_t capacity);
```

### DESCRIPTION

A failure read from the server is its Error, or a Notice, kept whole:
its SQLCODE and SQLSTATE; its message, in plain words; the rule it
cites, apart; its position, in characters into the statement, tag 6;
whether the session's transaction is still active after it; and its
diagnostics, tags 7 to 15: severity, detail, hint, context, schema,
table, column, data type and constraint [PROTOCOL Errors and notices].
Diagnostics come with `diagnostics` granted; a severity is ERROR,
FATAL, WARNING or NOTICE. A notice's message is as the server sent it.

`trl_failure_ends_connection` says whether the failure ends the
connection: SQLSTATE class 08 but 08003, or class 28 as a connection is
made [PROTOCOL Error 'E']. `trl_failure_text` writes the failure as
turborel-client displays it, `SQLCODE -919 (SQLSTATE 08006): message
[rule] (at character n)`, cut short to fit `out` and ended by a null,
and gives the length of the whole.

A connection's failure, `trl_conn_failure`, is valid until the next call
on the connection; a notice or a condition until the next statement, or
the notices are cleared; a tree's, `trl_xml_failure`, until it is freed.

### ERRORS

None: each function takes a null failure, and gives 0, "", null or
false.

## The driver's own failures

### NAME

The failures the driver raises, before anything is sent or as a reply
is read.

### DESCRIPTION

Each carries the SQLCODE and SQLSTATE turborel-client gives the same
failure, where it has the case, and else those of the table every
Turborel driver shares; each cites the rule `DESIGN`, with the severity
FATAL if it ends the connection, else ERROR (RULES.md D9):

| SQLCODE | SQLSTATE | Failure | The connection |
|---|---|---|---|
| -919 | 08006 | a connection lost, timed out, or sent a message malformed or out of turn [PROTOCOL Frames] | lost |
| -919 | 08006 | a method needing a capability not granted; query attributes past their bounds [PROTOCOL The tail] | kept |
| -919 | 28000 | a password or SCRAM refused over plain TCP; a SCRAM exchange the server did not prove, or one not bound where binding is required | not made |
| -918 | TB918 | memory not had for what a call does | kept |
| -900 | 0A000 | past Turborel's limits, as a value nested too deep or an XML tree too large; not built in, as TLS in a library built without it | kept |
| -301 | 22000, 22003, 22015, 22021, 22032 | a value the protocol cannot carry: a string not UTF-8, a number or interval out of range, JSON text not valid | kept |
| -301 | 2200N | XML text that is not well-formed content | kept |
| -910 | TB910 | a misuse turborel-client has no case for, as a canceller of a connection over the program's own stream | kept |

### STANDARD

[PROTOCOL Errors and notices], [PROTOCOL Error 'E'],
[CLIENT What the server tells].

### SEE ALSO

[Appendix B: Errors](../appendix/b-errors.md),
[Statements and Cursors](22-statements.md)
