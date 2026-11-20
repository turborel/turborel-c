# Pools

[Contents](README.md)

## Pools

### NAME

`trl_pool`, `trl_pool_config`, `trl_connector`,
`trl_pool_config_default`, `trl_pool_new`, `trl_pool_of_settings`,
`trl_pool_hold`, `trl_pool_get`, `trl_pool_release`, `trl_pool_counts`,
`trl_pool_free`: a bounded set of connections a program's threads
share.

### SYNOPSIS

```c
#include <turborel/turborel.h>

#define TRL_POOL_MAX 10
#define TRL_POOL_IDLE_TIMEOUT_MS 600000U
#define TRL_POOL_MAX_LIFETIME_MS 3600000U
#define TRL_POOL_WAIT_MS 30000U
#define TRL_POOL_RESET_MOST_STATEMENTS 256U

typedef struct trl_pool_config {
    size_t max;
    size_t max_idle;
    uint32_t idle_timeout_ms;
    uint32_t max_lifetime_ms;
    uint32_t wait_ms;
    bool check;
    const char *const *reset;
    size_t reset_count;
} trl_pool_config;

typedef trl_status (*trl_connector)(void *context, trl_conn **out);

trl_pool_config trl_pool_config_default(void);
trl_status trl_pool_new(trl_connector connect, void *context, const trl_pool_config *config, trl_pool **out);
trl_status trl_pool_of_settings(const trl_settings *settings, const trl_pool_config *config, trl_pool **out);
trl_pool *trl_pool_hold(trl_pool *pool);
trl_status trl_pool_get(trl_pool *pool, trl_conn **out);
void trl_pool_release(trl_pool *pool, trl_conn *conn);
void trl_pool_counts(trl_pool *pool, size_t *made, size_t *idle);
void trl_pool_free(trl_pool *pool);
```

### DESCRIPTION

A pool keeps a bounded set of connections that a program's threads
share [CLIENT Pools]. `trl_pool_new` makes one of the connections
`connect` makes, logged in, each handed `context`, as several threads
may call it at once; `trl_pool_of_settings` one of the connections
`trl_settings_connect()` makes of `settings`, which must last as long as
the pool. Each copies `config`, its statements too; null is
`trl_pool_config_default()`'s:

| Field | What it says | Default |
|---|---|---|
| `max` | The most connections made at once, handed out and idle | 10 |
| `max_idle` | The most kept idle | 10 |
| `idle_timeout_ms` | How long one may stay idle; 0, as long as it lives | 10 minutes |
| `max_lifetime_ms` | How long one lives from when it was made; 0, without limit | 1 hour |
| `wait_ms` | How long `trl_pool_get` waits for one to come back | 30 seconds |
| `check` | Whether an idle connection is checked to answer before it is handed out | yes |
| `reset`, `reset_count` | Statements run as each comes back, after the pool's own | none |

`trl_pool_hold` gives another part of the program a hold of the same
pool, each hold freed by `trl_pool_free`; the last frees the pool,
closing each idle connection. Every connection handed out must be given
back first. A pool's functions may be called from any thread at once
(RULES.md D11); the pool starts no thread, and each wait is the waiting
thread's.

`trl_pool_get` hands out an idle connection that answers, or makes one
while fewer than `max` are made, or waits for one to come back, at most
`wait_ms`. One idle past `idle_timeout_ms`, past its life, or that does
not answer its check is closed as `trl_pool_get` meets it, and another
taken or made. The check is Ping where the server grants it, which the
server answers without a worker; else ROLLBACK, which a session outside
a transaction completes doing nothing. Each wait of a check or a reset
is bounded by 30 seconds, or the connection's read timeout if shorter.

`trl_pool_release` gives a connection back, reset to the session it
logged in with, so no borrower sees another's. Where the server grants
it, the protocol's Reset ends the session's transaction, prepared
statements, cursors, temporary tables and settings in one request and
keeps its user; autocommit is off, then `reset`'s statements run. Of a
server that grants neither Ping nor Reset, the reset is one pipeline:
ROLLBACK, each statement the connection prepared closed and destroyed,
SET SESSION AUTHORIZATION and SET SCHEMA of its login's user, SET TIME
ZONE LOCAL, then `reset`'s statements; a statement already destroyed is
no failure, and a setting a borrower changed by SET stays as it set it
unless `reset` sets it back. A connection is closed rather than kept if
it is lost, if its reset fails, if it has lived its time, if more than
`TRL_POOL_RESET_MOST_STATEMENTS` of its statements would have to be
destroyed, or if `max_idle` are idle already. A connection not of the
pool is closed.

`trl_pool_counts` gives how many connections the pool has made, handed
out and idle, and how many are idle; either pointer may be null.

### ERRORS

`trl_pool_new` and `trl_pool_of_settings`: `TRL_INVALID_ARGUMENT` of a
null pointer, a `max` of 0, or a `reset_count` without `reset`;
`TRL_NO_MEMORY`.

`trl_pool_get`: `TRL_INVALID_ARGUMENT` of a null pointer;
`TRL_NO_MEMORY`, nothing made; `TRL_FAILED`, with a connection holding
why, the caller's to free: SQLSTATE 08001, SQLCODE -919, if no
connection came back within `wait_ms`, or the failure of making one, as
`connect` or the settings gave it.

### EXAMPLES

[Pipelines and Pools](../user/08-pipelines-and-pools.md).
