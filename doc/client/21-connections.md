# Connections

[Contents](README.md)

This page describes how the driver gives turborel-client's connections:
targets, TLS, mechanisms and the SCRAM key cache, the options of what a
connection asks for and how long it waits, the connection itself and
what the server tells it, and cancel and terminate; reset and
change-user are on [Statements and Cursors](22-statements.md). The
User's Guide's [Connecting](../user/02-connecting.md) shows them at
work.

## Targets

### NAME

`trl_target_tcp`, `trl_target_local`, `trl_target_tls`,
`trl_target_is_plain_tcp`, `trl_target_is_tls`, `trl_target_host`,
`trl_target_failure`, `trl_target_free`: where a server is.

### SYNOPSIS

```c
#include <turborel/turborel.h>

#define TRL_DEFAULT_PORT 7422
#define TRL_DEFAULT_PIPE "\\\\.\\pipe\\turborel"

trl_status trl_target_tcp(const char *host, uint16_t port, trl_target **out);
trl_status trl_target_local(const char *path, trl_target **out);
trl_status trl_target_tls(const char *host, uint16_t port, const char *server_name, trl_tls *tls,
                          trl_target **out);
bool trl_target_is_plain_tcp(const trl_target *target);
bool trl_target_is_tls(const trl_target *target);
const char *trl_target_host(const trl_target *target);
const trl_failure *trl_target_failure(const trl_target *target);
void trl_target_free(trl_target *target);
```

### DESCRIPTION

A TCP target reaches each address its host resolves to, in turn, each
within the connect timeout. A local target is a Unix socket on Linux and
macOS, a named pipe on Windows. A TLS target begins TLS on TCP, the
server's certificate checked against `server_name`, or the host if it is
null; it holds `tls` from then on [PROTOCOL The Native Protocol].
`trl_target_is_plain_tcp` and `trl_target_is_tls` say which a target is,
and `trl_target_host` gives the host of one over TCP or TLS, null of a
local one.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer or an empty host or path;
`TRL_NO_MEMORY`. `trl_target_tls` gives `TRL_FAILED`, the target made
and holding the failure, if the TLS settings trust no authority or have
failed.

## TLS

### NAME

`trl_tls_new`, `trl_tls_trust_file`, `trl_tls_trust_platform`,
`trl_tls_present`, `trl_tls_failure`, `trl_tls_free`: TLS settings.

### SYNOPSIS

```c
trl_status trl_tls_new(trl_tls **out);
trl_status trl_tls_trust_file(trl_tls *tls, const char *path);
trl_status trl_tls_trust_platform(trl_tls *tls);
trl_status trl_tls_present(trl_tls *tls, const char *chain, const char *key);
const trl_failure *trl_tls_failure(const trl_tls *tls);
void trl_tls_free(trl_tls *tls);
```

### DESCRIPTION

Settings of TLS 1.2 and 1.3 that check the server's certificate and
name, of the TLS library the driver was built with (OpenSSL 3.5 or
later, or AWS-LC). `trl_tls_trust_file` trusts the authorities of a PEM
file; `trl_tls_trust_platform` the platform's, as turborel-client
takes them: on macOS the keychain's trust settings, the user's over the
administrator's over the system's; on Windows the system store; the TLS
library's own file and directory elsewhere; or, on any, those of the
files SSL_CERT_FILE and SSL_CERT_DIR name if either is set, SSL_CERT_DIR
a list of directories apart by the platform's separator, `:` or `;`,
each regular file of each read, one that cannot be read passed over.
None at all trusted is a failure. `trl_tls_present` presents a certificate chain and its key, of
two PEM files [CLIENT Connecting]. A target made of the settings holds
them; they change no more after.

### ERRORS

`TRL_FAILED`, SQLSTATE 08006, of a file that cannot be read, one of no
certificate, a key that does not fit its certificate, or no platform
authority; SQLCODE -910, SQLSTATE TB910, of settings a target holds;
SQLCODE -900, SQLSTATE 0A000, from `trl_tls_new` in a library built
without a TLS library.

## Mechanisms

### NAME

`trl_mechanism_trust`, `trl_mechanism_password`,
`trl_mechanism_certificate`, `trl_mechanism_scram`,
`trl_mechanism_scram_in_the_clear`, `trl_mechanism_scram_channel_binding`,
`trl_mechanism_scram_key_cache`, `trl_mechanism_bind`,
`trl_mechanism_name`, `trl_mechanism_failure`, `trl_mechanism_free`:
what proves the user.

### SYNOPSIS

```c
#define TRL_MECHANISM_TRUST "trust"
#define TRL_MECHANISM_PASSWORD "password"
#define TRL_MECHANISM_CERTIFICATE "certificate"

typedef enum trl_channel_binding {
    TRL_CHANNEL_BINDING_PREFER = 0, TRL_CHANNEL_BINDING_DISABLE, TRL_CHANNEL_BINDING_REQUIRE,
} trl_channel_binding;

trl_status trl_mechanism_trust(trl_mechanism **out);
trl_status trl_mechanism_password(const char *password, trl_mechanism **out);
trl_status trl_mechanism_certificate(trl_mechanism **out);
trl_status trl_mechanism_scram(const char *password, trl_mechanism **out);
trl_status trl_mechanism_scram_in_the_clear(trl_mechanism *mechanism);
trl_status trl_mechanism_scram_channel_binding(trl_mechanism *mechanism, trl_channel_binding binding);
trl_status trl_mechanism_scram_key_cache(trl_mechanism *mechanism, trl_key_cache *cache);
trl_status trl_mechanism_bind(trl_mechanism *mechanism, const uint8_t *certificate, size_t certificate_length,
                              const uint8_t *exporter, size_t exporter_length);
const char *trl_mechanism_name(const trl_mechanism *mechanism);
const trl_failure *trl_mechanism_failure(const trl_mechanism *mechanism);
void trl_mechanism_free(trl_mechanism *mechanism);
```

### DESCRIPTION

The mechanisms of [PROTOCOL Mechanisms]. `trust` sends nothing of its
own; `password` sends the password's UTF-8; `certificate` relies on the
certificate TLS presents. SCRAM-SHA-256 sends client-first in Hello,
reads server-first, proves the password by client-final, and verifies
the server's signature in server-final before its last, empty Answer;
a login whose server never proves it knows the user's verifier is
refused. Over TLS it is SCRAM-SHA-256-PLUS: bound by `tls-exporter` over
TLS 1.3, by `tls-server-end-point` over TLS 1.2, as its GS2 header
names; with no binding to be had, it says it could bind, as `y`.
`TRL_CHANNEL_BINDING_REQUIRE` refuses a login not bound;
`TRL_CHANNEL_BINDING_DISABLE` binds nothing. `trl_mechanism_bind` binds a
TLS stream the program opened itself, for `trl_connect_stream`. `trl_mechanism_name` gives the name a mechanism is sent by,
`TRL_MECHANISM_TRUST`, `TRL_MECHANISM_PASSWORD` or
`TRL_MECHANISM_CERTIFICATE` of those three.

`password`, and SCRAM unless `trl_mechanism_scram_in_the_clear` says
otherwise, are secrets: `trl_connect` refuses them to a target over
plain TCP before it connects. A mechanism keeps its exchange's state, so
it serves one login at a time; freed, its password is cleared from
memory first.

### ERRORS

Of a login: SQLSTATE 28000 of a secret over plain TCP, of a binding
required and not had, or of a server's SCRAM message refused, as one out
of turn, of a nonce that does not extend the client's, or of a wrong
signature; SQLSTATE 08006 of a server that sent Ready unproved.
`trl_mechanism_scram` gives `TRL_FAILED`, SQLCODE -900, SQLSTATE 0A000,
in a library built without a TLS library's hashes. The SCRAM setters give
`TRL_INVALID_ARGUMENT` of a mechanism not SCRAM.

## The SCRAM key cache

### NAME

`trl_key_cache_new`, `trl_key_cache_size`, `trl_key_cache_lookups`,
`trl_key_cache_clear`, `trl_key_cache_free`: SCRAM's keys, kept.

### SYNOPSIS

```c
#define TRL_KEY_CACHE_ENTRIES 64U

trl_status trl_key_cache_new(trl_key_cache **out);
size_t trl_key_cache_size(trl_key_cache *cache);
void trl_key_cache_lookups(trl_key_cache *cache, uint64_t *hits, uint64_t *misses);
void trl_key_cache_clear(trl_key_cache *cache);
void trl_key_cache_free(trl_key_cache *cache);
```

### DESCRIPTION

The keys a password derives, kept so a later login of the same salt and
iterations skips the derivation [CLIENT The SCRAM key cache]. An entry
is found by the salt, the iterations and an HMAC of the password under a
random secret of the cache's own, so it holds nothing a password could
be guessed from offline. It keeps 64 entries at most, the oldest dropped
first, drops an entry when a login that used it fails, and lives in the
process's memory alone. Every thread may use one at once. A mechanism
given it holds it, and the last holder frees it.

## Options

### NAME

`trl_conn_options_new`, `trl_conn_options_ask_none`,
`trl_conn_options_ask`, `trl_conn_options_attribute`,
`trl_conn_options_compress_from`, `trl_conn_options_timeouts`,
`trl_conn_options_free`: what a connection asks for, and how long it
waits.

### SYNOPSIS

```c
#define TRL_NO_LIMIT 0U
#define TRL_CONNECT_WITHIN_MS 30000U
#define TRL_COMPRESS_FROM_BYTES 1024U

trl_status trl_conn_options_new(trl_conn_options **out);
trl_status trl_conn_options_ask_none(trl_conn_options *options);
trl_status trl_conn_options_ask(trl_conn_options *options, const char *capability);
trl_status trl_conn_options_attribute(trl_conn_options *options, const char *name, const char *value);
trl_status trl_conn_options_compress_from(trl_conn_options *options, size_t bytes);
trl_status trl_conn_options_timeouts(trl_conn_options *options, uint32_t connect_ms, uint32_t read_ms);
void trl_conn_options_free(trl_conn_options *options);
```

### DESCRIPTION

Made, options ask for every capability but compression
[CLIENT What a connection asks for], carry no connection attribute,
compress frames of 1,024 bytes or more once compression is granted, wait
30 seconds to connect, begin TLS and for each step of the login, and
have no read timeout [CLIENT Timeouts]. `TRL_CAPABILITY_` names each
capability. A connection attribute is carried in Hello's tail, which
every server reads [PROTOCOL Hello 'H']. A library built without zlib
never asks for compression.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer, an empty capability, or an
attribute past the protocol's limits: 32 of them, a name of 1 to 64
bytes, a value of at most 1,024; `TRL_NO_MEMORY`.

## Connections

### NAME

`trl_connect`, `trl_connect_stream`, `trl_conn_failure`,
`trl_conn_is_lost`, `trl_conn_target`, `trl_conn_server`, `trl_conn_authorization`,
`trl_conn_granted`, `trl_conn_granted_count`, `trl_conn_granted_at`,
`trl_conn_setting`, `trl_conn_setting_count`, `trl_conn_setting_at`,
`trl_conn_set_read_timeout`, `trl_conn_read_timeout`,
`trl_conn_set_max_body`, `trl_conn_ping`, `trl_conn_terminate`,
`trl_conn_free`: a connection and its session.

### SYNOPSIS

```c
typedef struct trl_stream {
    void *context;
    int64_t (*read)(void *context, uint8_t *into, size_t capacity);
    int64_t (*write)(void *context, const uint8_t *bytes, size_t length);
    void (*close)(void *context);
    bool (*bound)(void *context, uint32_t ms);
} trl_stream;

trl_status trl_connect(const trl_target *target, const char *user, trl_mechanism *mechanism,
                       const trl_conn_options *options, trl_conn **out);
trl_status trl_connect_stream(const trl_stream *stream, const char *user, trl_mechanism *mechanism,
                              const trl_conn_options *options, trl_conn **out);
const trl_failure *trl_conn_failure(const trl_conn *conn);
bool trl_conn_is_lost(const trl_conn *conn);
const trl_target *trl_conn_target(const trl_conn *conn);
const char *trl_conn_server(const trl_conn *conn);
const char *trl_conn_authorization(const trl_conn *conn);
bool trl_conn_granted(const trl_conn *conn, const char *capability);
size_t trl_conn_granted_count(const trl_conn *conn);
const char *trl_conn_granted_at(const trl_conn *conn, size_t index);
const char *trl_conn_setting(const trl_conn *conn, const char *name);
size_t trl_conn_setting_count(const trl_conn *conn);
bool trl_conn_setting_at(const trl_conn *conn, size_t index, const char **name, const char **value);
trl_status trl_conn_set_read_timeout(trl_conn *conn, uint32_t read_ms);
uint32_t trl_conn_read_timeout(const trl_conn *conn);
trl_status trl_conn_set_max_body(trl_conn *conn, uint32_t bytes);
trl_status trl_conn_ping(trl_conn *conn);
trl_status trl_conn_terminate(trl_conn *conn);
void trl_conn_free(trl_conn *conn);
```

### DESCRIPTION

`trl_connect` opens the target and logs in
[PROTOCOL Opening a connection]: Hello, of version 1, the user, the
mechanism's name and first bytes, the capabilities asked for and the
connection's attributes; then each Challenge answered, until Ready, or
an Error that refuses the login. Ready gives the session's authorization
identifier, the key that cancels its statements, which the connection
keeps secret, the capabilities granted, in the protocol's order, a name
the protocol does not have passed over, and the server's product. Ready is never compressed; once compression is
granted, frames are compressed both ways, and a compressed frame
received when it was not loses the connection
[PROTOCOL Compressed frames].

`trl_connect_stream` logs in over a stream the program opened, as a TLS
stream of its own, through its functions, and the caller answers for its
privacy. A stream whose `bound` is given bounds itself: it logs in
within the connect timeout the options give, then is bounded by their
read timeout, and its read timeout is set as a socket's is; one whose `bound` is null waits as its maker set
it to, Terminate too, and its read timeout cannot be set
[CLIENT Timeouts].

`trl_conn_target` gives where the connection reached its server: its
target, of TCP or TLS the address it reached in place of the host's
name, as turborel-client keeps it, so its canceller reaches the same
server however the name resolves after; null of a program's own stream.

Each Status the server sends, at login and after a statement, is kept:
`trl_conn_setting` gives a setting as the server last told it, by its
name as the server spells it, with no round trip
[CLIENT What the server tells]. A Notice is kept, or given to a handler ([Statements and Cursors](22-statements.md)).

The read timeout bounds each read or write after the login; a wait that
runs out loses the connection. A read, a write, a connect or a wait that
a signal interrupts is made again within what is left of its timeout,
and is no failure of the connection, as turborel-client makes it again. A reply is taken up to 16 MiB, or as many
bytes as `trl_conn_set_max_body` says; a longer one loses the connection
[CLIENT Timeouts]. A named pipe is bounded only as it is opened, while
it is busy.

`trl_conn_ping` sends Ping, answered by Pong, with `ping` granted
[PROTOCOL Ping 'G']. A method that needs a capability not granted fails
before it sends anything, the connection kept.

`trl_conn_terminate` sends Terminate: the session ends, its transaction
as SESSION.COMMIT_AT_END says, and the server closes; a commit that
fails then is its failure, a Notice or a Status before it read past, a
compressed one inflated, and a frame the server broke off as it closed
the connection's failure. It
waits at most the read timeout, else 60 seconds, on a stream that bounds
itself [PROTOCOL Terminate 'X']. The connection is lost after.
`trl_conn_free` closes a connection, over TLS with TLS's closing
message, without Terminate.

### ERRORS

`TRL_FAILED` with the connection's failure: the server's Error, kept
whole; or the driver's own, SQLSTATE 08006 of a connection that cannot
be made, is lost, times out, or gets a malformed reply or one out of
turn, and of a capability not granted; SQLSTATE 28000 of a secret
refused over plain TCP. An Error of class 08 but 08003, or of class 28
as the connection is made, loses the connection [PROTOCOL Error 'E'];
every call on a lost connection fails as it was lost.

### EXAMPLES

[Connecting](../user/02-connecting.md) runs each.

## Cancel

### NAME

`trl_canceller_new`, `trl_cancel`, `trl_cancel_within`,
`trl_canceller_failure`, `trl_canceller_free`: a statement cancelled
from another thread.

### SYNOPSIS

```c
#define TRL_CANCEL_WITHIN_MS 10000U

trl_status trl_canceller_new(trl_conn *conn, trl_canceller **out);
trl_status trl_cancel(trl_canceller *canceller);
trl_status trl_cancel_within(trl_canceller *canceller, uint32_t within_ms);
const trl_failure *trl_canceller_failure(const trl_canceller *canceller);
void trl_canceller_free(trl_canceller *canceller);
```

### DESCRIPTION

A canceller is made of a connection on the connection's thread, and
used on another. `trl_cancel` opens a connection of its own to the
address the connection reached, as `trl_conn_target` gives it, sends
Cancel with the key Ready gave as its first and only
message, and reads until the server closes; the statement cancelled
fails with SQLCODE -920, SQLSTATE TB920 [PROTOCOL Cancel 'Z']. It gives
up after 10 seconds, or `within_ms` [CLIENT Running statements].

### ERRORS

`trl_canceller_new` gives `TRL_FAILED`, SQLCODE -910, SQLSTATE TB910,
in the connection's failure, of a connection over the program's own
stream or one that never logged in. `trl_cancel` gives `TRL_FAILED`,
SQLSTATE 08006, of a server it cannot reach or that does not close in
time.
