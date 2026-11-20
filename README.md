<p align="center">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="artwork/turborel-logo-white.svg">
      <img src="artwork/turborel-logo.svg" alt="turborel" width="360">
    </picture>
</p>

> turborel-c is in alpha, meaning, version 0.0.0. Anything may change
> from one build to the next, the API, the protocol and the files alike.
> See [VERSIONING.md](VERSIONING.md).

turborel-c is the C driver for [turborel](https://github.com/turborel/turborel),
a relational database system. Its library, `libturborel`, connects a C
program to a turborel server over turborel's native protocol, which it
speaks itself, with no Rust code of turborel's linked in. It is written
in C17 and builds with GCC, Clang and MSVC on Linux, macOS and Windows.

It is to give a libpq-style C API with every feature of turborel's own
client library, turborel-client, and an ODBC 3.x driver over it, so that
Excel, Power BI, Tableau and LibreOffice reach turborel. turborel-cpp, the
C++ driver, wraps its C API.

A driver's version is the turborel release it is made for, the same
number as the server's. This alpha is 0.0.0, as turborel's is, and speaks
protocol version 1.

## What it has

The repository's kit: the build, its checks, CI on Linux, macOS and
Windows, and the frame header's codec with its tests. The C API, the
ODBC driver and the rest of the protocol are being built, milestone by
milestone, as [TODO.md](TODO.md) lists, and
[CONFORMANCE.md](CONFORMANCE.md) shows what of turborel-client it matches.

## What it needs

- A C17 compiler: GCC, Clang or MSVC.
- CMake 3.25 or later.
- For TLS and SCRAM, once they are built: OpenSSL 3.5 or later, or
  AWS-LC. For compressed frames: zlib. Neither is required yet.

## Installing

No package is published yet; build it from source.

```
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

This is what the C API is to look like once it is built, at M8; none
of it compiles yet, and its names may change until then.

```c
#include <stdio.h>
#include <turborel/turborel.h>

int main(void) {
    trl_conn *conn = NULL;
    if (trl_connect("turborel://ann@localhost:7422", &conn) != TRL_OK) {
        fprintf(stderr, "%s: %s\n", trl_failure_sqlstate(trl_conn_failure(conn)),
                trl_failure_message(trl_conn_failure(conn)));
        trl_conn_free(conn);
        return 1;
    }
    trl_result *result = NULL;
    if (trl_query(conn, "SELECT ID, NAME FROM STAFF ORDER BY ID", &result) == TRL_OK) {
        for (size_t row = 0; row < trl_result_rows(result); ++row) {
            printf("%s %s\n", trl_result_text(result, row, 0), trl_result_text(result, row, 1));
        }
    }
    trl_result_free(result);
    trl_conn_free(conn);
    return 0;
}
```

What works today is the version:

```c
#include <stdio.h>
#include <turborel/turborel.h>

int main(void) {
    printf("libturborel %s, protocol %d\n", trl_version(), TRL_PROTOCOL_VERSION);
    return 0;
}
```

```
libturborel 0.0.0, protocol 1
```

## Documents

- [doc/](doc/README.md): the manual, written as the API is built.
- [VERSIONING.md](VERSIONING.md): the version, and how it is published.
- [CONFORMANCE.md](CONFORMANCE.md): what the driver implements of the
  protocol and of turborel-client.
- [RULES.md](RULES.md): how the driver is designed, written and verified.
- [TODO.md](TODO.md): open work.
- [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md):
  contributing, and reporting an issue or a vulnerability.
- [LICENSE](LICENSE) and [TRADEMARK.md](TRADEMARK.md): the code is
  under the BSD-2-Clause Plus Patent License; the name Turborel and its
  logos are not, and TRADEMARK.md says how they may be used.
