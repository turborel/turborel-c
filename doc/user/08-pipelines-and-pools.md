# Pipelines and Pools

[Contents](README.md)

A request and its reply cost a round trip to the server. A pipeline
sends a batch of requests at once, so the batch costs one round trip,
not one a request. A pool keeps a bounded set of connections that a
program's threads share, so each thread need not log in for itself.
The SQL-agent holds several named connections, perhaps to several
servers, and runs CONNECT, SET CONNECTION and DISCONNECT itself. This
chapter shows each. [Pipelines](../client/27-pipelines.md),
[Pools](../client/28-pools.md) and
[The SQL-agent](../client/29-sql-agent.md) give each function in full.

## A batch of requests

`trl_pipeline_new` makes a pipeline of a connection;
`trl_pipeline_query` and `trl_pipeline_execute` queue requests;
`trl_pipeline_run` writes them and reads one outcome for each, in the
order queued [CLIENT Pipelines]. The server answers each request alone
and in turn, so a failure is its request's own, and the requests after
it run:

```c
run(conn, "CREATE TABLE STAFF (ID INTEGER PRIMARY KEY, NAME VARCHAR(20))");
run(conn, "COMMIT");
trl_statement *insert = NULL;
trl_params *params = NULL;
trl_pipeline *pipeline = NULL;
trl_prepare(conn, "INSERT INTO STAFF (ID, NAME) VALUES (?, ?)", &insert);
trl_params_new(&params);
trl_decimal two = {{2, 0}, 0};
trl_params_add_decimal(params, two);
trl_params_add_text(params, "BOB", 3);
trl_pipeline_new(conn, &pipeline);
trl_pipeline_query(pipeline, "INSERT INTO STAFF (ID, NAME) VALUES (1, 'ANN')");
trl_pipeline_query(pipeline, "INSERT INTO STAFF (ID, NAME) VALUES (1, 'ANN')");
trl_pipeline_execute(pipeline, insert, params);
trl_pipeline_query(pipeline, "SELECT ID, NAME FROM STAFF ORDER BY ID");
if (trl_pipeline_run(pipeline) == TRL_OK) {
    for (size_t i = 0; i < trl_pipeline_count(pipeline); ++i) {
        if (trl_pipeline_outcome(pipeline, i) == TRL_PIPED_DONE) {
            const trl_result *result = trl_pipeline_result(pipeline, i);
            printf("%zu: %s, %zu rows\n", i, trl_result_sqlstate(result), trl_result_row_count(result));
        } else {
            printf("%zu: %s\n", i, trl_failure_message(trl_pipeline_failure(pipeline, i)));
        }
    }
} else {
    print_failure(conn);
}
trl_pipeline_free(pipeline);
trl_params_free(params);
trl_statement_free(insert);
run(conn, "COMMIT");
```

```
0: 00000, 0 rows
1: PRIMARY KEY ANN.STAFF_PRIMARY1 (ID) of ANN.STAFF would be violated: a row of these values is there
2: 00000, 0 rows
3: 00000, 2 rows
```

A pipeline runs once; it is freed after, its outcomes with it. It
borrows its connection, which must not be used for anything else until
the pipeline has run. The requests are written a window of 64 KiB at a
time, `TRL_PIPELINE_WINDOW`, and the window's replies read before the
next is written, so a long batch never leaves client and server each
waiting for the other to read. Under autocommit each request is its own
transaction, as `trl_query` makes it.

## Stopping at the first failure

`trl_pipeline_on_error` with `TRL_ON_ERROR_SKIP` makes a batch stop at
its first failure, the requests before it kept. The driver sets a
savepoint before each request but the first; the requests after a
failure are rolled back to the savepoint set after it, and those of a
window not yet written are never sent. Each is `TRL_PIPED_SKIPPED`:

```c
trl_pipeline *pipeline = NULL;
trl_pipeline_new(conn, &pipeline);
trl_pipeline_on_error(pipeline, TRL_ON_ERROR_SKIP);
trl_pipeline_query(pipeline, "INSERT INTO STAFF (ID, NAME) VALUES (3, 'CAROL')");
trl_pipeline_query(pipeline, "INSERT INTO STAFF (ID, NAME) VALUES (1, 'ANN')");
trl_pipeline_query(pipeline, "INSERT INTO STAFF (ID, NAME) VALUES (4, 'DAN')");
if (trl_pipeline_run(pipeline) == TRL_OK) {
    static const char *const said[] = {"done", "failed", "skipped"};
    for (size_t i = 0; i < trl_pipeline_count(pipeline); ++i) {
        printf("%zu: %s\n", i, said[trl_pipeline_outcome(pipeline, i)]);
    }
}
trl_pipeline_free(pipeline);
trl_result *result = NULL;
trl_query(conn, "SELECT ID FROM STAFF ORDER BY ID", &result);
printf("%zu rows\n", trl_result_row_count(result));
trl_result_free(result);
run(conn, "COMMIT");
```

```
0: done
1: failed
2: skipped
3 rows
```

Such a batch is one transaction. Under autocommit, of a batch that
began none, it is committed as the batch ends; otherwise it stays as a
statement would leave it. What a rollback undoes is undone; a session's
settings, which no rollback undoes, are not. `trl_pipeline_run` fails,
giving no outcome, if the connection is lost (SQLSTATE 08006), or if a
skipping batch cannot be undone.

## A pool of connections

`trl_pool_of_settings` makes a pool of the connections settings make,
and `trl_pool_new` one of the connections a function of the program's
makes [CLIENT Pools]. `trl_pool_get` hands out an idle connection that
answers, or makes one while fewer than the most are made, or waits for
one to come back; `trl_pool_release` gives it back:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_settings *settings = NULL;
    trl_pool *pool = NULL;
    if (trl_settings_from_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable", &settings) != TRL_OK) {
        return 1;
    }
    trl_pool_config config = trl_pool_config_default();
    config.max = 2;
    trl_pool_of_settings(settings, &config, &pool);
    trl_conn *conn = NULL;
    if (trl_pool_get(pool, &conn) == TRL_OK) {
        trl_result *result = NULL;
        trl_query(conn, "SELECT COUNT(*) FROM STAFF", &result);
        char count[16];
        trl_value_to_text(trl_result_value(result, 0, 0), count, sizeof count, NULL);
        printf("%s rows\n", count);
        trl_result_free(result);
        trl_pool_release(pool, conn);
    }
    size_t made = 0;
    size_t idle = 0;
    trl_pool_counts(pool, &made, &idle);
    printf("%zu made, %zu idle\n", made, idle);
    trl_pool_free(pool);
    trl_settings_free(settings);
    return 0;
}
```

```
3 rows
1 made, 1 idle
```

A pool is shared by every thread of the program; `trl_pool_hold` gives
another part of the program a hold of the same pool, each hold freed by
`trl_pool_free`, the last closing the idle connections. The pool starts
no thread: a thread that gets a connection waits, if it must, itself.

`trl_pool_config` says how the pool keeps its connections: the most
made at once and the most kept idle, 10 each by default; how long one
may stay idle, 10 minutes, and how long one lives, an hour; how long
`trl_pool_get` waits, 30 seconds; whether an idle one is checked before
it is handed out, yes; and statements run as each comes back, none.

## Each borrower a fresh session

A connection comes back reset to the session it logged in with, so no
borrower sees another's work. Of a server that grants it, the
protocol's Reset ends the session's transaction, prepared statements,
cursors, temporary tables and settings, and keeps its user; autocommit
is off after:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_settings *settings = NULL;
    trl_pool *pool = NULL;
    trl_settings_from_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable", &settings);
    trl_pool_config config = trl_pool_config_default();
    config.max = 1;
    trl_pool_of_settings(settings, &config, &pool);
    trl_conn *conn = NULL;
    trl_result *result = NULL;
    trl_pool_get(pool, &conn);
    trl_query(conn, "INSERT INTO STAFF (ID, NAME) VALUES (9, 'EVE')", &result);
    trl_result_free(result);
    trl_query(conn, "SET SCHEMA 'ELSEWHERE'", &result);
    trl_result_free(result);
    trl_pool_release(pool, conn);
    trl_pool_get(pool, &conn);
    char count[16];
    trl_query(conn, "SELECT COUNT(*) FROM STAFF", &result);
    trl_value_to_text(trl_result_value(result, 0, 0), count, sizeof count, NULL);
    printf("%s rows\n", count);
    trl_result_free(result);
    trl_pool_release(pool, conn);
    trl_pool_free(pool);
    trl_settings_free(settings);
    return 0;
}
```

```
3 rows
```

The row the first borrower wrote was rolled back, and STAFF was found
in ANN's schema again. A connection that is lost, whose reset fails, or
that has lived its time is closed rather than kept, and one idle too
long, or that does not answer its check, is closed as `trl_pool_get`
meets it and another taken or made.

A pool whose every connection is handed out waits for one to come back,
at most its wait, then fails, SQLSTATE 08001:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_settings *settings = NULL;
    trl_pool *pool = NULL;
    trl_settings_from_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable", &settings);
    trl_pool_config config = trl_pool_config_default();
    config.max = 1;
    config.wait_ms = 50;
    trl_pool_of_settings(settings, &config, &pool);
    trl_conn *held = NULL;
    trl_conn *waited = NULL;
    trl_pool_get(pool, &held);
    if (trl_pool_get(pool, &waited) == TRL_FAILED) {
        char line[512];
        trl_failure_text(trl_conn_failure(waited), line, sizeof line);
        printf("%s\n", line);
    }
    trl_conn_free(waited);
    trl_pool_release(pool, held);
    trl_pool_free(pool);
    trl_settings_free(settings);
    return 0;
}
```

```
SQLCODE -919 (SQLSTATE 08001): no connection of the pool's 1 came back within 50ms [DESIGN]
```

## Several connections by name

The SQL-agent holds a program's connections, each named, one current
[CLIENT The SQL-agent]. `trl_agent_query` runs CONNECT, SET CONNECTION
and DISCONNECT itself, as SQL-92's SQL-agent does, and sends every other
statement to the current connection. CONNECT asks a function of the
program's for the connection, giving it the server and the user the
statement names; here the server is a host and a port:

```c
static trl_status dial(void *context, const char *server, const char *user, trl_conn **out,
                       trl_failure *why) {
    (void)context;
    (void)why;
    char host[64] = "127.0.0.1";
    unsigned port = 17438;
    if (server != NULL) {
        sscanf(server, "%63[^:]:%u", host, &port);
    }
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    trl_target_tcp(host, (uint16_t)port, &target);
    trl_mechanism_trust(&trust);
    trl_status made = trl_connect(target, user != NULL ? user : "ann", trust, NULL, out);
    trl_mechanism_free(trust);
    trl_target_free(target);
    return made;
}

static void say(trl_agent *agent, const char *sql) {
    trl_result *result = NULL;
    if (trl_agent_query(agent, sql, &result) != TRL_OK) {
        char line[512];
        trl_failure_text(trl_agent_failure(agent), line, sizeof line);
        printf("%s\n", line);
    } else if (trl_result_has_rows(result)) {
        char text[64];
        trl_value_to_text(trl_result_value(result, 0, 0), text, sizeof text, NULL);
        printf("%s\n", text);
    } else {
        const char *name = trl_agent_current_name(agent);
        printf("%s, current: %s\n", trl_result_sqlstate(result), name != NULL ? name : "the default");
    }
    trl_result_free(result);
}
```

```c
trl_agent *agent = NULL;
trl_agent_connected(dial, NULL, conn, &agent);
conn = NULL;
say(agent, "CONNECT TO '127.0.0.1:17438' AS 'reports' USER 'bob'");
say(agent, "VALUES CURRENT_USER");
say(agent, "SET CONNECTION DEFAULT");
say(agent, "COMMIT");
say(agent, "SET CONNECTION DEFAULT");
say(agent, "VALUES CURRENT_USER");
say(agent, "SELECT COUNT(*) FROM STAFF");
say(agent, "COMMIT");
say(agent, "DISCONNECT 'reports'");
say(agent, "SET CONNECTION 'reports'");
trl_agent_terminate(agent);
trl_agent_free(agent);
```

```
00000, current: REPORTS
BOB
SQLCODE -900 (SQLSTATE 0A001): the current connection's transaction is active, and none spans servers [ISO/IEC 9075:1992 §15.1 GR1, §15.2 GR1]
00000, current: REPORTS
00000, current: the default
ANN
3
00000, current: the default
00000, current: the default
SQLCODE -919 (SQLSTATE 08003): there is no connection REPORTS [ISO/IEC 9075:1992 §15.2 GR2]
```

A connection's name, and its user, are identifiers, so `'reports'`
names REPORTS, and `'"reports"'` would name another. No transaction
spans connections, so CONNECT and SET CONNECTION are refused while the
current connection's transaction is active, SQLSTATE 0A001, and
DISCONNECT of a connection whose transaction is active, 25000.
