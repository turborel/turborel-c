# The SQL-agent

[Contents](README.md)

## The SQL-agent

### NAME

`trl_agent`, `trl_agent_connector`, `trl_agent_new`,
`trl_agent_connected`, `trl_agent_query`, `trl_agent_query_batches`,
`trl_agent_current`, `trl_agent_current_name`, `trl_agent_count`,
`trl_agent_failure`, `trl_agent_terminate`, `trl_agent_free`: a
program's connections, each named, one current.

### SYNOPSIS

```c
#include <turborel/turborel.h>

typedef trl_status (*trl_agent_connector)(void *context, const char *server, const char *user, trl_conn **out);

trl_status trl_agent_new(trl_agent_connector connect, void *context, trl_agent **out);
trl_status trl_agent_connected(trl_agent_connector connect, void *context, trl_conn *conn, trl_agent **out);
trl_status trl_agent_query(trl_agent *agent, const char *sql, trl_result **out);
trl_status trl_agent_query_batches(trl_agent *agent, const char *sql, trl_batch_handler handler, void *context,
                                   trl_result **out);
trl_conn *trl_agent_current(const trl_agent *agent);
const char *trl_agent_current_name(const trl_agent *agent);
size_t trl_agent_count(const trl_agent *agent);
const trl_failure *trl_agent_failure(const trl_agent *agent);
trl_status trl_agent_terminate(trl_agent *agent);
void trl_agent_free(trl_agent *agent);
```

### DESCRIPTION

An agent holds a program's connections, each named, one current, and
runs CONNECT, SET CONNECTION and DISCONNECT itself, as SQL-92's
SQL-agent does [CLIENT The SQL-agent]; every other statement goes to
the current connection. `trl_agent_new` makes one with no connection;
`trl_agent_connected` one whose default connection, current, is `conn`,
as a client begins connected, the connection the agent's after. An
agent is one thread's at a time, as a connection is.

`connect` makes the connections CONNECT asks for, given `context`, the
server CONNECT TO names, its spaces trimmed, or null for CONNECT TO
DEFAULT; and the user USER names, trimmed, or null for none. What a
server's name means, an address or a service, is the program's to say.
The connection it makes, even one holding a failure, is the agent's.

`trl_agent_query` runs one statement. A statement that begins CONNECT,
DISCONNECT or SET CONNECTION is read as Turborel reads it, a semicolon
allowed at its end ([CONNECT](https://github.com/turborel/turborel/blob/main/doc/sql/statements/connect.md),
[SET CONNECTION](https://github.com/turborel/turborel/blob/main/doc/sql/statements/set-connection.md),
[DISCONNECT](https://github.com/turborel/turborel/blob/main/doc/sql/statements/disconnect.md)):

```text
CONNECT TO DEFAULT
CONNECT TO 'server' [ AS 'name' ] [ USER 'user' ]
SET CONNECTION { DEFAULT | 'name' }
DISCONNECT { DEFAULT | ALL | CURRENT | 'name' }
```

each name a character string literal, there being no parameters. It
runs it itself, and its result is the completion, SQLSTATE 00000, no
rows. CONNECT makes a connection current, the one current before
dormant, named as AS names it, else as the server is, and TO DEFAULT
the default one; a name is an identifier, so `'reports'` names REPORTS
and `'"reports"'` reports. SET CONNECTION makes the connection named, or
the default one, current, a name that is no identifier taken as
written. DISCONNECT ends by `trl_conn_terminate()` the connection it
names, the current one, the default one or all; with the current one
ended none is current until one is made or chosen. An error ending one
is a warning: the completion is SQLSTATE 01002, the connection ended
still.

Any other statement runs in the current connection's session, as
`trl_query()` runs it, and `trl_agent_query_batches` as
`trl_query_batches()` does, a connection statement's completion given
as `trl_agent_query`'s.

`trl_agent_current` is the current connection, still the agent's, to
read what its server last told it or to cancel its statement; null if
there is none. `trl_agent_current_name` is its name, null for the
default connection or for none. `trl_agent_count` is how many
connections the agent holds, current and dormant. `trl_agent_failure`
is why the last statement failed.

`trl_agent_terminate` ends every connection by `trl_conn_terminate()`,
as a client ends, every one ended though one fails; `trl_agent_free`
frees the agent, each connection it holds closed by `trl_conn_free()`.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer; `TRL_NO_MEMORY`. `TRL_FAILED`,
the agent's failure:

| SQLSTATE | SQLCODE | When |
|---|---|---|
| 0A001 | -900 | CONNECT or SET CONNECTION while the current connection's transaction is active: none spans servers |
| 28000 | -104 | CONNECT's user is no authorization identifier |
| 2E000 | -919 | CONNECT's name, or its server's when it has none, is no identifier |
| 08002 | -919 | CONNECT's name, or DEFAULT, is a connection's already |
| 08001 | -919 | CONNECT's server cannot be reached: `connect` failed with SQLSTATE 08001 or 08006 |
| 08004 | -919 | The server refused the connection: `connect` failed otherwise, its message kept |
| 08003 | -919 | SET CONNECTION or DISCONNECT names no connection held; another statement with none current |
| 25000 | -917 | DISCONNECT of a connection whose transaction is active |
| 2A000 | -101, -102 | A connection statement that is none: a literal not ended, or not of its Format |
| 2A000 | -103 | A connection statement naming by anything but a character string literal |
| 0A000 | -900 | A connection statement naming by an embedded variable, or a literal of a character set other than SQL_TEXT |

A failure of another statement is its connection's, as `trl_query()`
gives it. `trl_agent_terminate` returns `TRL_FAILED`, with the first
failure ending a connection.

### EXAMPLES

[Pipelines and Pools](../user/08-pipelines-and-pools.md).
