# Pipelines

[Contents](README.md)

## Pipelines

### NAME

`trl_pipeline`, `trl_pipeline_new`, `trl_pipeline_query`,
`trl_pipeline_execute`, `trl_pipeline_on_error`, `trl_pipeline_count`,
`trl_pipeline_run`, `trl_pipeline_outcome`, `trl_pipeline_result`,
`trl_pipeline_failure`, `trl_pipeline_free`: a batch of requests written
at once, one outcome each.

### SYNOPSIS

```c
#include <turborel/turborel.h>

#define TRL_PIPELINE_WINDOW 65536

typedef enum trl_on_error { TRL_ON_ERROR_CONTINUE = 0, TRL_ON_ERROR_SKIP } trl_on_error;
typedef enum trl_piped { TRL_PIPED_DONE = 0, TRL_PIPED_FAILED, TRL_PIPED_SKIPPED } trl_piped;

trl_status trl_pipeline_new(trl_conn *conn, trl_pipeline **out);
trl_status trl_pipeline_query(trl_pipeline *pipeline, const char *sql);
trl_status trl_pipeline_execute(trl_pipeline *pipeline, const trl_statement *statement, const trl_params *params);
trl_status trl_pipeline_on_error(trl_pipeline *pipeline, trl_on_error on_error);
size_t trl_pipeline_count(const trl_pipeline *pipeline);
trl_status trl_pipeline_run(trl_pipeline *pipeline);
trl_piped trl_pipeline_outcome(const trl_pipeline *pipeline, size_t request);
const trl_result *trl_pipeline_result(const trl_pipeline *pipeline, size_t request);
const trl_failure *trl_pipeline_failure(const trl_pipeline *pipeline, size_t request);
void trl_pipeline_free(trl_pipeline *pipeline);
```

### DESCRIPTION

A pipeline sends a connection's requests without waiting for each
reply, so a batch costs one round trip, not one a request
[CLIENT Pipelines]. `trl_pipeline_new` makes one of `conn`, which it
borrows: the connection must not be used otherwise, nor freed, until
the pipeline is freed. `trl_pipeline_query` queues a statement by Query,
its text copied, and `trl_pipeline_execute` an execution of a statement
`conn` prepared, its parameters copied, null for none
[PROTOCOL Requests].

`trl_pipeline_run` writes the requests queued and reads their replies,
one outcome for each, in the order queued. The server answers each
request alone and in turn, so a failure is its request's own, and the
requests after it run. Requests are written a window of
`TRL_PIPELINE_WINDOW` bytes at a time, a request longer than that alone
in its own, and each window's replies read before the next is written,
so a long batch never leaves client and server each waiting for the
other to read. Under autocommit each request is its own transaction, as
`trl_query()` makes it. A pipeline runs once.

`trl_pipeline_on_error` with `TRL_ON_ERROR_SKIP` makes a batch stop at
its first failure, the requests before it kept. The driver sets a
savepoint before each request but the first; the requests after a
failure are rolled back to the savepoint set after it and their outcomes
`TRL_PIPED_SKIPPED`, and those of a window not yet written are never
sent. Such a batch is one transaction: under autocommit, of a batch
that began none, it is committed as the batch ends; otherwise it stays
as a statement would leave it. What a rollback undoes is undone; a
session's settings, which no rollback undoes, are not. The savepoints
are released as the batch ends.

`trl_pipeline_count` is how many requests are queued, and after the
run how many outcomes there are, the same. `trl_pipeline_outcome` is
what request `request` came to: `TRL_PIPED_DONE`, its result
`trl_pipeline_result`'s, a query's columns and rows or another
statement's completion, as `trl_query()` and `trl_execute()` give them;
`TRL_PIPED_FAILED`, its failure `trl_pipeline_failure`'s; or
`TRL_PIPED_SKIPPED`, neither. Each is the pipeline's, valid until it is
freed. A request past the last is skipped, with neither.

`trl_pipeline_free` frees the pipeline, its requests and its outcomes;
null does nothing.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer, and of a request queued or a
run after the pipeline has run; `TRL_NO_MEMORY`. `trl_pipeline_run`
returns `TRL_FAILED`, with the connection's failure and no outcome, if
the connection is lost (SQLSTATE 08006), a request cannot be written, or
a skipping batch cannot be undone to its savepoint, released or
committed.

### EXAMPLES

[Pipelines and Pools](../user/08-pipelines-and-pools.md).
