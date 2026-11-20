# Settings

[Contents](README.md)

## Options

### NAME

`trl_options`, `trl_options_new`, `trl_options_set`, `trl_options_get`,
`trl_options_from_uri`, `trl_options_failure`, `trl_options_free`: a
layer of connection settings, as text.

### SYNOPSIS

```c
#include <turborel/turborel.h>

#define TRL_SETTING_HOST "host"
/* and TRL_SETTING_PORT, _USER, _PASSWORD, _TLS_MODE, _CHANNEL_BINDING,
   _APPLICATION_NAME, _CONNECT_TIMEOUT, _SERVICE, _SERVICE_FILE, _PASSFILE,
   _TARGET, _LOAD_BALANCE, _TLS_ROOTS, _TLS_SYSTEM_ROOTS, _TLS_CERTIFICATE,
   _TLS_KEY, _SERVER_NAME */
#define TRL_DEFAULT_PORT 7422
#define TRL_DEFAULT_HOST "localhost"
#define TRL_URI_SCHEME "turborel://"

trl_status trl_options_new(trl_options **out);
trl_status trl_options_set(trl_options *options, const char *key, const char *value);
const char *trl_options_get(const trl_options *options, const char *key);
trl_status trl_options_from_uri(const char *uri, trl_options **out);
const trl_failure *trl_options_failure(const trl_options *options);
void trl_options_free(trl_options *options);
```

### DESCRIPTION

A layer holds settings by name, each a setting of
[Appendix C](../appendix/c-settings.md), its value text, copied
[CLIENT Where settings come from]. `trl_options_from_uri` reads a
connection URI into one [CLIENT The connection URI]:

```text
turborel://[user[:password]@][host[:port][,host[:port]]...][/][?setting=value[&setting=value]...]
```

Its user, password, hosts and values are percent-encoded, so `p%40ss`
is the password `p@ss` and `%2Frun%2Fturborel.sock` a Unix socket; an
IPv6 address is written in brackets. A server serves one database, so
the URI names none, and has no path but `/`.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer; `TRL_NO_MEMORY`. `TRL_FAILED`,
SQLSTATE 08001, the layer's failure: a key that names no setting; of a
URI, another scheme, a path, a pair without `=`, a `%` not followed by
two hexadecimal digits, or octets not UTF-8 once decoded.

## Settings

### NAME

`trl_settings`, `trl_variable`, `trl_settings_resolve`,
`trl_settings_resolve_in`, `trl_settings_from_uri`,
`trl_settings_failure`, `trl_settings_host_count`, `trl_settings_host`,
`trl_settings_user`, `trl_settings_get`, `trl_settings_tls_mode`,
`trl_settings_channel_binding`, `trl_settings_connect_timeout`,
`trl_settings_target`, `trl_settings_load_balance`,
`trl_settings_tls_system_roots`, `trl_settings_password_for`,
`trl_settings_key_cache`, `trl_settings_free`: settings laid.

### SYNOPSIS

```c
typedef const char *(*trl_variable)(void *context, const char *name);

typedef enum trl_tls_mode { TRL_TLS_DISABLE = 0, TRL_TLS_PREFER, TRL_TLS_REQUIRE, TRL_TLS_VERIFY_CA,
                            TRL_TLS_VERIFY_FULL } trl_tls_mode;
typedef enum trl_target_role { TRL_TARGET_ANY = 0, TRL_TARGET_READ_WRITE, TRL_TARGET_READ_ONLY } trl_target_role;
typedef enum trl_load_balance { TRL_LOAD_BALANCE_DISABLE = 0, TRL_LOAD_BALANCE_RANDOM } trl_load_balance;

trl_status trl_settings_resolve(const trl_options *options, trl_settings **out);
trl_status trl_settings_resolve_in(const trl_options *options, trl_variable variable, void *context,
                                   trl_settings **out);
trl_status trl_settings_from_uri(const char *uri, trl_settings **out);
const trl_failure *trl_settings_failure(const trl_settings *settings);
size_t trl_settings_host_count(const trl_settings *settings);
const char *trl_settings_host(const trl_settings *settings, size_t host, uint16_t *port, bool *local);
const char *trl_settings_user(const trl_settings *settings);
const char *trl_settings_get(const trl_settings *settings, const char *key);
trl_tls_mode trl_settings_tls_mode(const trl_settings *settings);
trl_channel_binding trl_settings_channel_binding(const trl_settings *settings);
uint32_t trl_settings_connect_timeout(const trl_settings *settings);
trl_target_role trl_settings_target(const trl_settings *settings);
trl_load_balance trl_settings_load_balance(const trl_settings *settings);
bool trl_settings_tls_system_roots(const trl_settings *settings);
bool trl_settings_password_for(const trl_settings *settings, size_t host, char *out, size_t capacity,
                               size_t *length);
trl_key_cache *trl_settings_key_cache(const trl_settings *settings);
void trl_settings_free(trl_settings *settings);
```

### DESCRIPTION

`trl_settings_resolve` lays the program's layer, null for none, over
the section of the service file it or the environment names, over the
environment's variables, over the defaults, each setting taken from the
first layer that has it [CLIENT Where settings come from]; the process's
environment is read once, as the settings are laid.
`trl_settings_resolve_in` reads it through `variable` instead.
`trl_settings_from_uri` lays a URI's layer. The settings are made even
when they cannot be laid, holding why.

The service file is `service_file`, else `~/.turborel_service.conf`:
sections, each `[name]` on a line of its own, then lines
`setting = value`; a line beginning with `#` a comment, blank lines
skipped. A section cannot set `service` or `service_file`
[CLIENT The service file].

`host` names the servers, comma-separated, each its own port as
`name:port`, an IPv6 address as `[address]:port`, a path a Unix socket
(on Windows, a named pipe); a host of no port takes the port of the same
place of `port`, or the one port it gives, or 7422.
`trl_settings_host` gives each, its port 0 and `local` true of a socket.
`trl_settings_get` gives a setting as laid, but `password`, which it
never gives. `trl_settings_connect_timeout` gives milliseconds, 30 000
unless told, 0 for no limit, at most about 49 days.

`trl_settings_password_for` gives the password to prove at a server:
the settings', else the password file's for the server, its port and
the user [CLIENT The password file]. The password file is `passfile`,
else `~/.turborel_pass`: lines `host:port:user:password`, a field `*`
matching anything, `\` taking the character after it as it is, `#`
beginning a comment; the first line that matches is taken, a Unix socket
matched by its path and port 7422. It is read only if it is a regular
file, or a link to one; its owner answers for keeping it private.

`trl_settings_key_cache` gives the SCRAM key cache every connection the
settings make shares [CLIENT The SCRAM key cache].

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer; `TRL_NO_MEMORY`, nothing made.
`TRL_FAILED`, SQLSTATE 08001, the settings' failure: a setting that
cannot be read, as a word no setting takes ("tls_mode is always, not one
of disable, prefer, require, verify-ca, verify-full"), a port that is no
number, a host empty or an IPv6 address unclosed; a service file that
cannot be read, a malformed line of it, naming its file and line, or no
section of the name; no user ("no user is named: by the program, a
service, or TURBOREL_USER"). The accessors give their defaults of null
settings.

### EXAMPLES

[Settings](../user/09-settings.md) lays each layer.

## Connecting by settings

### NAME

`trl_settings_connect`, `trl_connect_uri`, `trl_server_role`,
`trl_conn_role`: a connection made as settings, or a URI, say, and the
role of its server.

### SYNOPSIS

```c
typedef enum trl_server_role { TRL_SERVER_READ_WRITE = 0, TRL_SERVER_READ_ONLY, TRL_SERVER_UNKNOWN } trl_server_role;

trl_status trl_settings_connect(const trl_settings *settings, trl_conn **out);
trl_status trl_connect_uri(const char *uri, trl_conn **out);
trl_status trl_conn_role(trl_conn *conn, trl_server_role *out);
```

### DESCRIPTION

`trl_settings_connect` tries each host in the order given, or with
`load_balance=random` in an order drawn at random for each connection,
and each address a host's name resolves to, every step within the
connect timeout [CLIENT Several servers]. A server reached, logged in to
and of the role `target` asks is kept; one that cannot be reached,
refuses the user, or is of another role is left, and the next tried. A
password is proved by SCRAM-SHA-256, its channel bound as
`channel_binding` says, its keys kept in the settings' cache; with none,
the certificate `tls_certificate` names is presented; else the user is
trusted. A secret mechanism is refused over plain TCP before anything is
sent. The connection asks for every capability but compression, and
gives `application_name` as the connection attribute `application`.

TLS is used as `tls_mode` says [CLIENT TLS by mode]: `disable` never;
`prefer` if the server takes it, else plain TCP, a server reached whose
handshake fails connected to again over plain TCP; `require` always;
and these three do not check the server's certificate but that the
server holds its key. `verify-ca` checks the certificate against the
authorities `tls_roots` and `tls_system_roots` trust, `verify-full` its
name too, `server_name`'s, else the host's. A Unix socket or a named pipe
uses none. `channel_binding=require` refuses, before anything is sent,
a connection not over TLS or a password not proved by SCRAM.

Settings may connect from several threads at once; their connections
share their key cache, which takes every thread (RULES.md D11).

`trl_connect_uri` is the standard API's way to a connection, as libpq's
PQconnectdb: the URI laid with the rest of the settings, as
`trl_settings_from_uri` lays it, then connected by
`trl_settings_connect`. Its connection is made on `TRL_FAILED` too,
holding why, a URI that cannot be read among the reasons.

`trl_conn_role` gives the role of a connection's server as its session
sees it: as SERVER_READ_ONLY last told it; of a server that does not
tell it, as INFORMATION_SCHEMA.REPLICAS shows it, asked as a statement
that is its own transaction. A server alone, which has no REPLICAS,
takes changes; a replica's role is shown to the database's owner alone,
so another session finds it unknown.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer, or settings not laid;
`TRL_NO_MEMORY`, nothing made. `TRL_FAILED`, the connection made and
holding why: of one server tried, its own failure; of several, SQLSTATE
08001 naming each address and why, as "no server was kept: 127.0.0.1
(127.0.0.1:1): cannot connect: ...", or 28000 where the last refused
the user; SQLSTATE 08006 of TLS that cannot be made, as "no certificate
authority is trusted: name a file of them, or the platform's" or "a
client certificate is presented with its key, and none is named";
SQLSTATE 28000 of a secret over plain TCP, or of channel binding
required that cannot bind ("channel binding is required: SCRAM over TLS
alone binds one"); SQLSTATE 08001 of a server of another role ("the
server is read-write, and read-only is asked").

`trl_conn_role`: `TRL_INVALID_ARGUMENT` of a null pointer; `TRL_FAILED`
with the connection's failure, the query's, but of a server alone.

### EXAMPLES

[Settings](../user/09-settings.md) connects to several servers, and
checks a role.
