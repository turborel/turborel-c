<p align="center">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="artwork/turborel-logo-white.svg">
      <img src="artwork/turborel-logo.svg" alt="turborel" width="360">
    </picture>
</p>

> turborel-c is in alpha, meaning, version 0.0.0. Its version is the
> number of the Turborel release it is made for, and the alpha is made
> for Turborel 0.0.0, which speaks protocol version 1. Anything may
> change from one build to the next, API and behaviour alike. See
> [VERSIONING.md](VERSIONING.md). The latest Turborel alpha is built for
> Linux, macOS and Windows as the pre-release
> [nightly](https://github.com/turborel/turborel/releases/tag/nightly),
> and as the image `ghcr.io/turborel/turborel:nightly`.

turborel-c is the C driver for
[Turborel](https://github.com/turborel/turborel), a relational database
system. It connects a C program to a Turborel server,
`turboreld`, over Turborel's [native
protocol](https://github.com/turborel/turborel/blob/main/doc/client/05-protocol.md),
version 1, which it speaks itself, in C17, with no Rust code of Turborel's linked in.

Its standard interface is a C API of connections, statements and results, and an ODBC 3.x driver over it. Beneath it is a low-level
client built to do all that Turborel's own client library,
turborel-client, does: TCP, Unix sockets and Windows named pipes, TLS,
SCRAM-SHA-256 and SCRAM-SHA-256-PLUS, prepared statements and cursors,
large objects, bulk copy, import and export, settings from a URI, the
environment and service and password files, several servers, pipelines
and pools.

The driver was built in milestones, M0 to M8, each done. It connects to
a server over each transport, by each mechanism, by settings or a URI;
runs statements, prepared or not, and reads their rows a batch at a time
or through cursors; reads and writes large objects; copies, imports and
exports; runs pipelines, pools and the SQL-agent; and gives the standard
API and the ODBC driver. The last milestone's audit checked every row
against turborel-client and the spec.
[CONFORMANCE.md](CONFORMANCE.md) shows each row, and [TODO.md](TODO.md)
what is open.

## Requirements

- a C17 compiler, GCC, Clang or MSVC, and CMake 3.25 or later, on Linux, macOS or Windows ([RULES.md](RULES.md) D3).
- For TLS and SCRAM, OpenSSL 3.5 or later, or AWS-LC; for compressed frames, zlib, which the
  build takes from its pinned release and builds in where none is found, as on Windows.
- A Turborel server of the same release, which speaks protocol version 1
  ([running the server](https://github.com/turborel/turborel/blob/main/doc/operator/02-turboreld.md)).

## Installing

No package is published yet. The driver's package, `libturborel`, found as the CMake package `turborel` or by `turborel.pc`, goes to
archives on the repository's GitHub releases only once every Turborel driver is done, with the others
([VERSIONING.md](VERSIONING.md#publishing)). Until then, build the
driver from source:

```
git clone https://github.com/turborel/turborel-c.git
cd turborel-c
cmake --workflow --preset release          # configure, build and test
cmake --install build/release --prefix /usr/local
```

A program then finds it with CMake,

```cmake
find_package(turborel REQUIRED)
target_link_libraries(app PRIVATE turborel::turborel)
```

or with pkg-config, `pkg-config --cflags --libs turborel`.

## A first query

This is the standard API: a connection made of a URI, a statement run,
and its values read as text.

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_conn *conn = NULL;
    if (trl_connect_uri("turborel://ann@localhost:7422", &conn) != TRL_OK) {
        fprintf(stderr, "%s: %s\n", trl_failure_sqlstate(trl_conn_failure(conn)),
                trl_failure_message(trl_conn_failure(conn)));
        trl_conn_free(conn);
        return 1;
    }
    const char *const values[] = {"3"};
    trl_result *result = NULL;
    if (trl_query_params(conn, "SELECT ID, NAME FROM STAFF WHERE ID < ? ORDER BY ID", 1, values, &result) == TRL_OK) {
        for (size_t row = 0; row < trl_result_row_count(result); ++row) {
            printf("%s %s\n", trl_result_text(result, row, 0), trl_result_text(result, row, 1));
        }
    } else {
        fprintf(stderr, "%s: %s\n", trl_failure_sqlstate(trl_conn_failure(conn)),
                trl_failure_message(trl_conn_failure(conn)));
    }
    trl_result_free(result);
    trl_conn_terminate(conn);
    trl_conn_free(conn);
    return 0;
}
```

The program is built by `cc first.c $(pkg-config --cflags --libs turborel)`.
[Installing](doc/user/01-installing.md) runs a whole first program, and
shows how to register the ODBC driver, `libturborelodbc`, with a driver
manager.

## The manual

[doc/](doc/README.md) is the manual: a User's Guide, an API Reference
and Appendices, published at
https://turborel.com/docs/drivers/c/. The SQL a
program sends, and the server, are described in Turborel's own manual,
[Turborel Documentation](https://github.com/turborel/turborel/blob/main/doc/README.md).
[VERSIONING.md](VERSIONING.md) tells how the driver's releases are
numbered and published.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before making a change: how a
change is made, its commit message, pull requests and issues. Every
change passes `cmake --workflow --preset verify`. [RULES.md](RULES.md) says how the
driver is designed, written and verified,
[CONFORMANCE.md](CONFORMANCE.md) is the parity matrix every Turborel
driver keeps, and [TODO.md](TODO.md) lists open work.

## Security

Report a vulnerability privately, never in a public issue, as
[SECURITY.md](SECURITY.md) says.

## License and trademark

The code is under the BSD-2-Clause Plus Patent License, in
[LICENSE](LICENSE). The name Turborel and its logos are not:
[TRADEMARK.md](TRADEMARK.md) says how they may be used.
