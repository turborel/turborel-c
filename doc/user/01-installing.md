# Installing

[Contents](README.md)

This chapter tells what the driver needs, how to build and install it,
how a program finds it, and how to register its ODBC driver. It ends
with a first program.

No package of the driver is published yet. Its package, `libturborel`,
found as the CMake package `turborel` or by `turborel.pc`, goes to
archives on the repository's GitHub releases once every Turborel driver
is done ([Versions](../appendix/d-versions.md)). Until then the driver is
built from its source.

## What the driver needs

- A C17 compiler, GCC, Clang or MSVC, and CMake 3.25 or later, on Linux,
  macOS or Windows.
- For TLS and SCRAM, OpenSSL 3.5 or later, or AWS-LC. A driver built
  without either refuses TLS and SCRAM, saying which library it lacks.
- For compressed frames, zlib.
- For the ODBC driver, ODBC's headers and installer library: unixODBC's
  or iODBC's on Linux and macOS, the Windows SDK's on Windows.
- A Turborel server of the same release, which speaks protocol
  version 1
  ([Running the Server](https://github.com/turborel/turborel/blob/main/doc/operator/02-turboreld.md)).

## Building from source

```
git clone https://github.com/turborel/turborel-c.git
cd turborel-c
cmake --workflow --preset release          # configure, build and test
cmake --install build/release --prefix /usr/local
```

The release workflow configures, builds and runs the tests. Its tests
against a live server run only when `TRL_TEST_SERVER` names a
`turboreld` to start; otherwise they are skipped. These CMake options
change what is built:

| Option | Default | Builds |
|---|---|---|
| `BUILD_SHARED_LIBS` | ON | `libturborel` as a shared library; OFF, a static one |
| `TRL_TLS` | `OpenSSL` | TLS and hashes by OpenSSL, `AWS-LC`, or `none` |
| `TRL_ZLIB` | ON | compressed frames, where zlib is found |
| `TRL_ODBC` | ON | the ODBC driver, `libturborelodbc`, where ODBC's headers are found |
| `TRL_BUILD_TESTS` | ON in the driver's own tree | the tests |

CMake's output says which TLS library, zlib and ODBC were found.

## Using the library

A program finds the installed library with CMake,

```cmake
find_package(turborel REQUIRED)
target_link_libraries(app PRIVATE turborel::turborel)
```

or with pkg-config, as `cc app.c $(pkg-config --cflags --libs turborel)`.
A program that links the static library has `TRL_STATIC` defined for it
by either.

## Installing the ODBC driver

`cmake --install` installs the ODBC driver, `libturborelodbc`, in the
library directory. A driver manager loads it once it is registered. With
unixODBC, the driver is a section of `odbcinst.ini`, in the directory
`odbcinst -j` prints:

```
[Turborel]
Description = Turborel's ODBC driver
Driver      = /usr/local/lib/libturborelodbc.so
```

A data source is then a section of `odbc.ini`, naming the driver and
where the server is:

```
[reports]
Driver   = Turborel
SERVER   = db.example.com
PORT     = 7422
UID      = ann
TLS_MODE = require
```

A program, or a tool such as `isql`, connects by the data source's name.
[The ODBC Driver](../client/01-api-odbc.md) gives every key, and how the
driver is registered with iODBC and on Windows.

## A first program

This program connects by a URI, makes a table, adds rows with their
values given as text, and reads them back. It is built by
`cc first.c $(pkg-config --cflags --libs turborel)`:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_conn *conn = NULL;
    if (trl_connect_uri("turborel://ann@127.0.0.1:17438/?tls_mode=disable", &conn) != TRL_OK) {
        printf("%s %s\n", trl_failure_sqlstate(trl_conn_failure(conn)), trl_failure_message(trl_conn_failure(conn)));
        trl_conn_free(conn);
        return 1;
    }
    trl_result *result = NULL;
    trl_query(conn, "CREATE TABLE PLANETS (NAME VARCHAR(10), MOONS INTEGER)", &result);
    trl_result_free(result);
    static const char *const planets[][2] = {{"Mercury", "0"}, {"Earth", "1"}, {"Mars", "2"}};
    for (size_t i = 0; i < 3; ++i) {
        trl_query_params(conn, "INSERT INTO PLANETS VALUES (?, ?)", 2, planets[i], &result);
        trl_result_free(result);
    }
    const char *const some[] = {"1"};
    if (trl_query_params(conn, "SELECT NAME, MOONS FROM PLANETS WHERE MOONS >= ? ORDER BY NAME", 1, some,
                         &result) == TRL_OK) {
        for (size_t row = 0; row < trl_result_row_count(result); ++row) {
            printf("%s has %s\n", trl_result_text(result, row, 0), trl_result_text(result, row, 1));
        }
    }
    trl_result_free(result);
    trl_conn_terminate(conn);
    trl_conn_free(conn);
    return 0;
}
```

```
Earth has 1
Mars has 2
```

The user `ann` is trusted here because the server's SERVER.TRUST_LOCAL is
ON for a client on its own machine. [Connecting](02-connecting.md) shows
each other way to prove a user, and TLS.
