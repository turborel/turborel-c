# Settings

[Contents](README.md)

A program need not name its server, its user and its TLS in code.
Settings say where the servers are, as whom to connect and how, and
come from outside the program: a `turborel://` URI or options it fills,
a service file, the environment's TURBOREL_ variables, and a password
file. This chapter shows how settings are laid and read, how a
connection is made as they say to one of several servers, a primary or
a replica as asked, and how TLS modes and channel binding protect it.
[Settings](../client/26-settings.md) gives each function in full, and
[Appendix C](../appendix/c-settings.md) each setting.

## Connecting by a URI

`trl_settings_from_uri` reads a connection URI and lays it with the
rest; `trl_settings_connect` connects as the settings say, giving a
connection like `trl_connect`'s [CLIENT Connecting by settings]:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_settings *settings = NULL;
    trl_conn *conn = NULL;
    if (trl_settings_from_uri("turborel://ann@127.0.0.1:17438/?application_name=report", &settings) != TRL_OK ||
        trl_settings_connect(settings, &conn) != TRL_OK) {
        char line[512];
        trl_failure_text(conn != NULL ? trl_conn_failure(conn) : trl_settings_failure(settings), line, sizeof line);
        printf("%s\n", line);
    } else {
        printf("%s, as %s\n", trl_conn_server(conn), trl_conn_authorization(conn));
    }
    trl_conn_free(conn);
    trl_settings_free(settings);
    return 0;
}
```

```
turborel 0.0.0 (alpha), as ANN
```

A password the settings give is proved by SCRAM-SHA-256; with none, the
certificate the settings name is presented over TLS; else the user is
trusted, as here. A secret mechanism is refused over plain TCP before
anything is sent, SQLSTATE 28000. `application_name` is sent as the
connection attribute `application`, which the database's owner sees in
INFORMATION_SCHEMA.CONNECTION_ATTRIBUTES.

## Where settings come from

Each setting has a name, the same in options, in a URI's query and in a
service file. Settings come in layers, each setting taken from the first
that has it [CLIENT Where settings come from]:

1. the program's own: options it fills, or a URI;
2. the section of the service file the program or the environment names;
3. the environment's variables, TURBOREL_HOST and the rest;
4. the defaults.

`trl_settings_resolve` reads the process's environment;
`trl_settings_resolve_in` reads it through a function the program gives,
as here, so the example shows each layer:

```c
static const char *environment(void *context, const char *name) {
    (void)context;
    if (strcmp(name, "TURBOREL_SERVICE") == 0) {
        return "reports";
    }
    if (strcmp(name, "TURBOREL_SERVICE_FILE") == 0) {
        return "services.conf";
    }
    return strcmp(name, "TURBOREL_PORT") == 0 ? "17438" : NULL;
}
```

```c
FILE *file = fopen("services.conf", "w");
fputs("# servers\n[reports]\nhost = 127.0.0.1\nuser = ann\ntarget = read-write\n", file);
fclose(file);
trl_options *options = NULL;
trl_settings *settings = NULL;
trl_options_new(&options);
trl_options_set(options, TRL_SETTING_TLS_MODE, "disable");
if (trl_settings_resolve_in(options, environment, NULL, &settings) == TRL_OK) {
    uint16_t port = 0;
    const char *host = trl_settings_host(settings, 0, &port, NULL);
    printf("%s:%u as %s, target %s, tls_mode %s\n", host, port, trl_settings_user(settings),
           trl_settings_get(settings, TRL_SETTING_TARGET), trl_settings_get(settings, TRL_SETTING_TLS_MODE));
    trl_conn *other = NULL;
    if (trl_settings_connect(settings, &other) == TRL_OK) {
        printf("connected as %s\n", trl_conn_authorization(other));
    }
    trl_conn_free(other);
}
trl_settings_free(settings);
trl_options_free(options);
remove("services.conf");
```

```
127.0.0.1:17438 as ann, target read-write, tls_mode disable
connected as ANN
```

The host and user came from the service, the port from the environment,
and the TLS mode from the program. A setting that cannot be read, a
service file that cannot be read or has no section of the name, and
settings that name no user are refused, SQLSTATE 08001
[CLIENT The service file].

## The password file

A password the settings do not give is looked up in the password file,
`~/.turborel_pass` unless `passfile` names another, by the server and
user of each connection: lines `host:port:user:password`, a `*` matching
anything, `\` taking the character after it as it is
[CLIENT The password file]. It is read only if it is a regular file, or
a link to one:

```c
FILE *file = fopen("passwords", "w");
fputs("# servers\n127.0.0.1:17438:ann:ann's secret\n*:*:reporter:report\\:ing\n", file);
fclose(file);
trl_settings *settings = NULL;
if (trl_settings_from_uri("turborel://ann@127.0.0.1:17438/?passfile=passwords", &settings) == TRL_OK) {
    char password[64];
    if (trl_settings_password_for(settings, 0, password, sizeof password, NULL)) {
        printf("a password of %zu characters\n", strlen(password));
    }
    trl_conn *other = NULL;
    if (trl_settings_connect(settings, &other) != TRL_OK) {
        print_failure(other);
    }
    trl_conn_free(other);
}
trl_settings_free(settings);
remove("passwords");
```

```
a password of 12 characters
SQLCODE -919 (SQLSTATE 28000): scram-sha-256 is sent only over TLS, a Unix socket or a named pipe, never over plain TCP [DESIGN]
```

The server of this example takes no TLS, so the default mode, `prefer`,
connected over plain TCP, and SCRAM, a secret, was refused there before
anything was sent.

## Several servers

`host` may name several servers, each with its port. They are tried in
the order given, or with `load_balance=random` in an order drawn at
random for each connection; a name at each address it resolves to, each
step within the connect timeout. One that cannot be reached, refuses
the user, or is not of the role asked is passed for the next; with none
kept, the failure names each address and why [CLIENT Several servers].
`target` asks for a role: `read-write` keeps a server alone or its
group's primary, `read-only` a replica. `trl_conn_role` gives the role a
connection's session sees:

```c
trl_conn *kept = NULL;
trl_settings *settings = NULL;
trl_settings_from_uri("turborel://ann@127.0.0.1:1,127.0.0.1:17438/?tls_mode=disable&target=read-write", &settings);
if (trl_settings_connect(settings, &kept) == TRL_OK) {
    trl_server_role role = TRL_SERVER_UNKNOWN;
    trl_conn_role(kept, &role);
    printf("kept the second, %s\n", role == TRL_SERVER_READ_WRITE ? "read-write" : "not read-write");
}
trl_conn_free(kept);
trl_settings_free(settings);
trl_settings_from_uri("turborel://ann@127.0.0.1:1,127.0.0.1:17438/?tls_mode=disable&target=read-only", &settings);
if (trl_settings_connect(settings, &kept) != TRL_OK) {
    print_failure(kept);
}
trl_conn_free(kept);
trl_settings_free(settings);
```

```
kept the second, read-write
SQLCODE -919 (SQLSTATE 08001): no server was kept: 127.0.0.1 (127.0.0.1:1): cannot connect: Connection refused (os error 61); 127.0.0.1 (127.0.0.1:17438): the server is read-write, and read-only is asked [DESIGN]
```

## TLS by mode

`tls_mode` says how a connection over TCP uses TLS; a Unix socket or a
named pipe uses none [CLIENT TLS by mode]:

| Mode | TLS | The server's certificate |
|---|---|---|
| `disable` | Never | |
| `prefer` | If the server takes it, else plain TCP | Not checked |
| `require` | Always | Not checked |
| `verify-ca` | Always | Signed by an authority trusted |
| `verify-full` | Always | Signed by an authority trusted, and of the server's name |

`prefer` and `require` keep a connection from being overheard, not from
a server in the middle; `verify-full` keeps both. `verify-ca` and
`verify-full` need the authorities trusted, `tls_roots` or
`tls_system_roots`; the name checked is `server_name`, else the host's.
Under `prefer`, a server reached whose TLS handshake fails is connected
to again over plain TCP; one that cannot be reached is not tried twice.

`channel_binding` says whether SCRAM binds its exchange to the TLS
connection carrying it, so a server in the middle cannot relay it:
`prefer` binds where it can, `disable` never, and `require` refuses,
before anything is sent, settings whose connection is not over TLS or
whose password is not proved by SCRAM, SQLSTATE 28000:

```c
trl_conn *refused = NULL;
trl_settings *settings = NULL;
trl_settings_from_uri("turborel://ann:secret@127.0.0.1:17438/?tls_mode=disable&channel_binding=require", &settings);
if (trl_settings_connect(settings, &refused) != TRL_OK) {
    print_failure(refused);
}
trl_conn_free(refused);
trl_settings_free(settings);
```

```
SQLCODE -919 (SQLSTATE 28000): channel binding is required: SCRAM over TLS alone binds one [DESIGN]
```

The settings keep one SCRAM key cache for every connection they make,
so a program's reconnections, and a pool's connections, skip deriving a
password's keys again ([CLIENT The SCRAM key cache]);
`trl_settings_key_cache` gives it.
