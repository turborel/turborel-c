# Connecting

[Contents](README.md)

A program connects to a server by three things: where the server is, a
*target*; the user it connects as; and a *mechanism* that proves the
user. A connection is a session of the server's, held until the program
ends it. This chapter shows each target and mechanism, TLS, the
timeouts, what a connection is told, and how it ends or its statement is
cancelled. The standard API's way of connecting comes with it, at
milestone M7. [Connections](../client/21-connections.md) gives each
function in full.

## A first connection

A server listens on TCP port 7422 unless its operator chose another. A
server whose setting SERVER.TRUST_LOCAL is ON takes a client of its own
machine as the user it names, by the mechanism `trust`:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    trl_conn *conn = NULL;
    if (trl_target_tcp("127.0.0.1", 17438, &target) != TRL_OK || trl_mechanism_trust(&trust) != TRL_OK) {
        return 1;
    }
    if (trl_connect(target, "ann", trust, NULL, &conn) == TRL_OK) {
        printf("%s, as %s\n", trl_conn_server(conn), trl_conn_authorization(conn));
        printf("time zone %s\n", trl_conn_setting(conn, "SESSION.TIME_ZONE"));
        trl_conn_terminate(conn);
    } else {
        char line[256];
        trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        printf("%s\n", line);
    }
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    trl_target_free(target);
    return 0;
}
```

```
turborel 0.0.0 (alpha), as ANN
time zone +00:00
```

The user is an identifier, so `ann` is ANN. Each handle is freed by its
own `_free` function, and a connection is freed even when `trl_connect`
fails: it then holds the failure, which `trl_conn_failure` gives.

## Targets

| Function | Reaches the server |
|---|---|
| `trl_target_tcp(host, port, &target)` | over TCP, at each address the host resolves to, in turn |
| `trl_target_local(path, &target)` | over a Unix socket on Linux and macOS, or a named pipe on Windows, as `TRL_DEFAULT_PIPE`, `\\.\pipe\turborel` |
| `trl_target_tls(host, port, server_name, tls, &target)` | over TLS on TCP, the server's certificate checked against `server_name`, or the host if it is null |

A Unix socket or a named pipe reaches only the server's own machine, so
it needs no TLS.

## Mechanisms

| Function | Proves the user by |
|---|---|
| `trl_mechanism_trust` | nothing: the server trusts a client of its own machine, if SERVER.TRUST_LOCAL is ON |
| `trl_mechanism_password(password, ...)` | the password, sent whole |
| `trl_mechanism_certificate` | the certificate TLS presents, its common name the user's |
| `trl_mechanism_scram(password, ...)` | SCRAM-SHA-256: the password never travels, and the server must prove it knows the user's verifier |

SCRAM is what a password should be proved by. A password, or SCRAM, is
sent only over TLS, a Unix socket or a named pipe: to a target over
plain TCP, `trl_connect` refuses them before it connects. A server whose
SERVER.SCRAM_WITHOUT_TLS is ON takes SCRAM over plain TCP all the same,
and `trl_mechanism_scram_in_the_clear` lets the program send it so.

```c
trl_target_tcp("127.0.0.1", 17438, &target);
trl_mechanism_password("ann's secret", &password);
if (trl_connect(target, "ann", password, NULL, &conn) == TRL_FAILED) {
    trl_failure_text(trl_conn_failure(conn), line, sizeof line);
    printf("%s\n", line);
}
trl_conn_free(conn);
trl_target_free(target);
trl_target_local("turborel.sock", &target);
if (trl_connect(target, "ann", password, NULL, &conn) == TRL_OK) {
    printf("%s over the socket\n", trl_conn_authorization(conn));
}
trl_conn_free(conn);
trl_mechanism_free(password);
trl_mechanism_password("not ann's", &password);
if (trl_connect(target, "ann", password, NULL, &conn) == TRL_FAILED) {
    trl_failure_text(trl_conn_failure(conn), line, sizeof line);
    printf("%s\n", line);
}
```

```
SQLCODE -919 (SQLSTATE 28000): password is sent only over TLS, a Unix socket or a named pipe, never over plain TCP [DESIGN]
ANN over the socket
SQLCODE -919 (SQLSTATE 28000): the user or the password is wrong
```

## TLS

TLS settings, a `trl_tls`, say which certificate authorities are
trusted: those of a PEM file, by `trl_tls_trust_file`; the platform's,
by `trl_tls_trust_platform`, which are those of the files SSL_CERT_FILE
and SSL_CERT_DIR name if either is set; or both. `trl_tls_present` names
the certificate chain and key a client presents, for the mechanism
`certificate`. TLS is 1.2 or 1.3, and the server's certificate must be
signed by an authority trusted and be of the name checked.

Over TLS, SCRAM is SCRAM-SHA-256-PLUS, bound to the connection: over TLS
1.3 by `tls-exporter`, over TLS 1.2 by the certificate the server
presented, so a server in the middle cannot relay the exchange.
`trl_mechanism_scram_channel_binding` can require the binding, or
disable it.

A login derives SCRAM's keys from the password slowly, on purpose. A
key cache keeps them, so a later login to the same server skips that:

```c
if (trl_tls_new(&tls) != TRL_OK || trl_tls_trust_file(tls, "ca.pem") != TRL_OK ||
    trl_target_tls("localhost", 17438, NULL, tls, &target) != TRL_OK ||
    trl_key_cache_new(&cache) != TRL_OK || trl_mechanism_scram("ann's secret", &scram) != TRL_OK ||
    trl_mechanism_scram_key_cache(scram, cache) != TRL_OK) {
    return 1;
}
for (int login = 0; login < 2; ++login) {
    if (trl_connect(target, "ann", scram, NULL, &conn) == TRL_OK) {
        printf("%s, as %s, over TLS\n", trl_conn_server(conn), trl_conn_authorization(conn));
    }
    trl_conn_free(conn);
}
uint64_t hits = 0;
uint64_t misses = 0;
trl_key_cache_lookups(cache, &hits, &misses);
printf("hits %llu, misses %llu\n", (unsigned long long)hits, (unsigned long long)misses);
```

```
turborel 0.0.0 (alpha), as ANN, over TLS
turborel 0.0.0 (alpha), as ANN, over TLS
hits 1, misses 1
```

A key cache may be shared by every thread of a program at once.

## What a connection asks for, and waits

Options, a `trl_conn_options`, say what a connection asks the server for
and how long it waits; `trl_connect` takes null for the defaults. A
connection asks for every capability but compression, which
`trl_conn_options_ask(options, TRL_CAPABILITY_COMPRESSION)` adds;
`trl_conn_options_ask_none` asks for none; `trl_conn_options_attribute`
adds a connection attribute, as `application`, which the database's
owner sees in INFORMATION_SCHEMA.CONNECTIONS. `trl_conn_granted` says
what the server granted.

Connecting, TLS and each step of the login wait 30 seconds at most; each
read or write after waits as long as it must, as a statement may run
long. `trl_conn_options_timeouts` sets both, and
`trl_conn_set_read_timeout` changes a connection's read timeout. A wait
that runs out loses the connection (SQLSTATE 08006), saying the server
did not answer in time.

## What the server tells

The server tells a connection its session's settings as it logs in, and
again as a statement changes one. `trl_conn_setting` gives one as the
server last told it, with no round trip: SESSION.TIME_ZONE,
TRANSACTION.ISOLATION, AUTHORIZATION, SCHEMA, READ_ONLY, SERVER_VERSION,
SERVER_READ_ONLY, CATALOG and OWNER among them.

## Ending, and cancelling

`trl_conn_terminate` ends the session, its transaction as the setting
SESSION.COMMIT_AT_END says, and gives the failure of a commit that
fails then; `trl_conn_free` closes a connection without it.

A canceller, made of a connection by `trl_canceller_new`, cancels the
statement it is running from another thread: `trl_cancel` sends the
connection's key over a connection of its own, and the statement fails
with SQLCODE -920, SQLSTATE TB920.
