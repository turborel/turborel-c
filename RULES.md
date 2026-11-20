# turborel-c rules

How turborel-c is designed, written and verified. Every change follows
these rules; `cmake --workflow --preset verify` checks those a tool can check, and
review checks the rest.

Rules (R-…) say how code and tests are written. **shall** is mandatory,
and a departure needs a decision recorded here (§7). **should** is
expected, and a reviewer may accept a reasoned exception.

Decisions (D1…) record how the driver is built and why. Change one only
by changing this file in a reviewed commit. A decision says what was
chosen and why, and the module it names holds the mechanism.

Cite both by ID in code, tests, reviews and commit messages, as in
`RULES.md D6` or `R-ERR-2`.

The rules are modelled on Turborel's own
[RULES.md](https://github.com/turborel/turborel/blob/main/RULES.md),
adapted to drivers. Every Turborel driver keeps the same requirement IDs
with the same meaning, and the same decisions D1 to D8, so a rule is
cited the same way in every repository; a driver's own decisions begin
at D9, and its own requirements are in families of their own. Where
C's library conventions and a rule here disagree, the rule is
kept, and D8 lists where.

Before changing code, read §3 (code rules), §4 (test rules) and the
decisions covering your area. What the driver implements is in
[CONFORMANCE.md](CONFORMANCE.md), and open work is in [TODO.md](TODO.md).

[1 Decisions](#1-decisions) · [2 Layout](#2-project-layout) ·
[3 Code](#3-code-rules) · [4 Tests](#4-test-rules) ·
[5 Coverage](#5-coverage) · [6 Process](#6-process-and-tools) ·
[7 The decisions](#7-the-decisions) · [8 References](#8-references)

---

## 1. Decisions

Each is told in full in [§7](#7-the-decisions).

| # | Decision |
|---|---|
| **Shared by every Turborel driver** | |
| D1 | The native protocol, version 1, in layers that follow turborel-client |
| D2 | The language's standard API, over a low-level client of turborel-client's features |
| D3 | The runtime minimum follows the ecosystem's own support |
| D4 | One command runs every check, before each commit and in CI on three platforms |
| D5 | The version is the Turborel release's: 0.0.0 in the alpha, speaking protocol 1 |
| D6 | Tests and code cite Turborel's documents by their headings |
| D7 | A coverage floor, enforced in CI, that only rises |
| D8 | Where these rules depart from C's conventions |
| **This driver's own** | |
| D9 | Failures are status codes, with a failure object of SQLCODE and SQLSTATE |
| D10 | Opaque handles, and ownership said by every function |
| D11 | No global mutable state, and one thread at a time on a handle |
| D12 | Dependencies: the C library, OpenSSL 3.5 or AWS-LC, and zlib |
| D13 | CMake with presets, an installed package, and hidden symbols |
| D14 | Tests run by CTest, on one harness of our own |

---

## 2. Project layout

| Path | What |
|---|---|
| `include/turborel/turborel.h` | the public header a program includes, which includes the interface's parts under `include/turborel/` (D13) |
| `src/codec/` | the `codec` layer: frames, fields, types, values, JSON, the tail, messages, compression, with no I/O |
| `src/auth/` | the `auth` layer: SCRAM-SHA-256 and its channel binding, the key cache, the mechanisms, over the TLS library's hashes |
| `src/net/` | the `net` layer: TCP and Unix sockets, named pipes, TLS over them |
| `src/conn/` | the `conn` layer: targets, options, connections, their login, session and cancel |
| `src/failure.c`, `src/memory.c` | the failure a handle keeps (D9), and the allocations, which a test may fail (D14) |
| `src/` | the other layers, one directory each, as they are written (D1) |
| `test/harness.h`, `test/harness.c` | the one test harness (D14) |
| `test/<layer>/*_test.c` | the tests, one executable per file, `layer.thing` in CTest |
| `test/package/` | a program outside the tree, for the installed package's test |
| `test/server/` | the live tests' server set-up: turboreld started and stopped, and its test certificates made (R-TEST-8) |
| `test/live/` | the live tests, against that server |
| `fuzz/` | the libFuzzer targets of the codec |
| `cmake/` | CMake modules, and the checks tool: `check-source.cmake` with `CheckCitations.cmake` and `CheckManual.cmake`, `check-format.cmake`, `coverage-report.cmake`, `test-package.cmake`, `ci-run.cmake` |
| `CMakeLists.txt`, `CMakePresets.json` | the build, and the workflows every check runs by |
| `.clang-format`, `.clang-tidy`, `Doxyfile` | the formatter, the linters, the API reference |

---

## 3. Code rules

The tools named by "the formatter", "the linters", "the type checker" and
"the checks tool" are D4's.

### R-STYLE: formatting and idiom

| ID | Rule | Checked by |
|---|---|---|
| R-STYLE-1 | Code **shall** be formatted by the formatter. | the formatter |
| R-STYLE-2 | Names **shall** follow the language's own conventions; code implementing a message, field or setting is named after it, as the spec names it. | the linters; review |
| R-STYLE-3 | Library code **shall not** print, or write to a stream it was not given. | the linters or the checks tool |
| R-STYLE-4 | What is not public **shall** be private, by the language's own means, so the public API is what is documented. | review |

### R-SIZE: short functions and files

| ID | Rule | Checked by |
|---|---|---|
| R-SIZE-1 | A function **shall** have at most 50 lines of code, blank lines and comments not counted. | the linters or the checks tool |
| R-SIZE-2 | A source file **shall** have at most 600 lines; split by the spec's sections or by layer. | the linters or the checks tool |
| R-SIZE-3 | A function **shall** take at most 5 parameters; else a structure or an options object. | the linters |
| R-SIZE-4 | A module **should** hold one concept, stated in its doc comment. | review |
| R-SIZE-5 | A function's complexity **shall** be at most 15, and its blocks nested at most 5 deep. | the linters |

### R-DOC: comments and documentation

| ID | Rule | Checked by |
|---|---|---|
| R-DOC-1 | Every public item **shall** have a doc comment in the language's documentation format, saying each parameter, what it returns, and each failure it gives. | the linters; review |
| R-DOC-2 | Comments **shall** say *why*: the section implemented, the choice made, the invariant relied on. | review |
| R-DOC-3 | The API reference, where the language's tools build one, **shall** build without warnings. | the checks tool |
| R-DOC-4 | Behaviour the protocol or turborel-client governs **shall** cite it by its heading, `[PROTOCOL Frames]`, `[CLIENT Pools]`; the driver's own choices cite their decision (D6). | review; the checks tool |
| R-DOC-5 | No commented-out code, and no `TODO`, `FIXME` or `XXX` comments: open work goes in TODO.md, and code never cites TODO.md. | the linters or the checks tool |
| R-DOC-6 | Every source file **shall** begin with a doc comment giving its responsibility and the sections it implements. | review |
| R-DOC-7 | A change a program can see **shall** update the manual, `doc/`, in the same commit; every claim is checked against the code, and every example is run. The manual keeps Turborel's layout, which turborel.com's documentation site reads: each page opens with its `# Title` and a line holding only `[Contents](README.md)`, each volume's README.md lists its pages, links within `doc/` and to other files of this repository are relative links that resolve (the site points the latter at GitHub, at the same tag), a link to Turborel's own manual is its GitHub address under `https://github.com/turborel/turborel/blob/main/doc/`, `user/01-installing.md` keeps its name, and `doc/` holds no images. | review; the checks tool |

### R-CONST: no magic numbers or strings

| ID | Rule | Checked by |
|---|---|---|
| R-CONST-1 | Numbers in library code other than `0` or `1` **shall** be named constants, documenting the value's source. | the linters; review |
| R-CONST-2 | Every protocol tag **shall** be a named constant, defined once: a message's kind, a data type's and a value's tag, a tail tag, a capability's name, a mechanism's name, a status name, and each SQLSTATE the driver raises. | review |
| R-CONST-3 | Strings matched or looked up, such as settings' names, URI parts and environment variables, **shall** be named constants. Messages for people are not. | review |
| R-CONST-4 | Related constants **shall** live together in their layer's module, under a comment naming the section they come from. | review |
| R-CONST-5 | Tests may use literal fixtures, as the spec prints its bytes; an expected value restating a constant **should** use it. | review |

### R-ERR: errors

| ID | Rule | Checked by |
|---|---|---|
| R-ERR-1 | Every failure the library gives **shall** be the driver's error (D2): no stray exception, panic, abort, assertion or other error of the language or its libraries reaches a program, and no library code ends the process. | the linters; tests; review |
| R-ERR-2 | Each failure **shall** carry its SQLCODE, SQLSTATE and message, and, once read from a server, the rest of it: the rule it cites, its position, whether a transaction is still active, and its diagnostics. A failure the driver raises itself carries the SQLCODE and SQLSTATE turborel-client gives the same failure. | tests |
| R-ERR-3 | No failure **shall** be silenced. One caught and given again as the driver's keeps its cause; a result left unread says why it cannot matter. | the linters; review |
| R-ERR-4 | A connection that fails **shall** be closed, and every request waiting on it given that failure; a failed call leaks nothing and leaves nothing waiting. | tests; review |

### R-NUM: integers and numbers

| ID | Rule | Checked by |
|---|---|---|
| R-NUM-1 | Every integer of the protocol **shall** be read and written at its width, little-endian, explicitly: never by the platform's byte order, by casting a buffer, or by bitwise operators on numbers wider than the language's operators keep exact. | review; tests |
| R-NUM-2 | A value out of range for its field, or for the type it is read into, **shall** be refused with the driver's error, never truncated, wrapped or rounded. | tests |
| R-NUM-3 | An exact number (units `i128`, scale `u32`) and a DECFLOAT **shall** never pass through a binary floating-point type. | review; tests |
| R-NUM-4 | An approximate number **shall** keep its bits: NaN and -0.0 are read and written as the double they are, and floats are compared bit for bit, never for equality. | tests; review |
| R-NUM-5 | In a language whose integers are bounded, arithmetic on sizes, counts and offsets **shall** be shown not to overflow before it is done. | the linters; review |

### R-LINT: warnings

| ID | Rule | Checked by |
|---|---|---|
| R-LINT-1 | A warning is an error: of the compiler, the linters and the type checker, and of the runtime itself while the tests run. | the checks tool |
| R-LINT-2 | The strictest standard linters and type checking the language has **shall** be on; a check left off says why in the configuration that leaves it off. | review |
| R-LINT-3 | A warning **shall** be silenced only on its line, naming its rule and saying why; a suppression no longer needed **shall** be removed. | the linters; review |

### R-DEP: dependencies

| ID | Rule | Checked by |
|---|---|---|
| R-DEP-1 | The library **shall** depend on nothing but the language's standard library and runtime, unless a decision here admits a dependency, with its reason, version, license and review. | review |
| R-DEP-2 | A dependency, of the library or of its tools, **shall** be production-grade by its makers' own claims: a stable release they support. | review |
| R-DEP-3 | Tools **shall** be pinned by version, with the ecosystem's lock file under version control where it has one; a change of a tool's version is a commit of its own. | review |

### R-SAFE: what a server sends, and secrets

| ID | Rule | Checked by |
|---|---|---|
| R-SAFE-1 | A length or count a server, or a file the driver reads, gives **shall** be checked against what follows and the limit taken before anything is held for it, so no reply takes the program's memory without bound. | tests; review |
| R-SAFE-2 | What a server sends **shall** never be run, loaded or deserialized as code. | the linters; review |
| R-SAFE-3 | TLS **shall** be used and verified as the settings ask, and a connection **shall not** fall back to less than its mode allows. | tests |
| R-SAFE-4 | A secret (a password, anything derived from one, a SCRAM key or proof, a cancel key) **shall** never be logged, put in a failure's message or an object's display form, or sent where it was refused; where the language allows, it is cleared from memory once used. | tests; review |
| R-SAFE-5 | No module-level mutable state: what lasts belongs to an object a program made, such as a connection, a pool or a key cache. | review |
| R-SAFE-6 | The library **shall** start no thread, timer or task but for an object a program made, and a closed connection, cursor or pool leaves none behind. | tests; review |

### R-PLAT: platforms

| ID | Rule | Checked by |
|---|---|---|
| R-PLAT-1 | The driver **shall** run on Linux, macOS and Windows, on every runtime D3 supports; CI checks each (D4). | CI |
| R-PLAT-2 | Code of one platform, such as a Unix socket or a named pipe, **shall** be chosen by the platform, and elsewhere refused with the driver's error, saying so; never silently something else. | tests; review |
| R-PLAT-3 | Paths **shall** be made by the language's path functions, and files kept with LF line endings (`.gitattributes`). | review |
| R-PLAT-4 | Code built for one platform only **shall** say, when handed back, that it was not run on the others. | review |

### R-MEM: memory

| ID | Rule | Checked by |
|---|---|---|
| R-MEM-1 | No `strcpy`, `strcat`, `sprintf`, `gets`, `strtok`, `alloca` or `atoi` and their kin; copies are bounded by the destination's size. | the checks tool |
| R-MEM-2 | Every allocation **shall** be checked, and its failure given as the driver's error, SQLCODE -918, SQLSTATE TB918 (D9). | review; tests |
| R-MEM-3 | What the library allocates it frees, through its own functions, as a program and a DLL on Windows may not share an allocator (D10). | review; sanitizers |
| R-MEM-4 | No variable-length arrays, and nothing of C17 that MSVC lacks (D3). | `-Wvla`; MSVC in CI |
| R-MEM-5 | Every read **shall** be bounded by the bytes available. | AddressSanitizer; fuzzing |

### R-CF: control flow

| ID | Rule | Checked by |
|---|---|---|
| R-CF-1 | A boolean decision **should** have at most 4 conditions; else a named predicate. | review |
| R-CF-2 | A `switch` on an enumeration **shall** list every constant it handles by name. | `-Wswitch-enum` |
| R-CF-3 | `goto` only jumps forward, to the one cleanup at a function's end. | review |
| R-CF-4 | Recursion **shall** be bounded by a named depth constant, failing past it. | review; tests |

---

## 4. Test rules

### R-TEST: levels and layout

| ID | Rule | Checked by |
|---|---|---|
| R-TEST-1 | A test **shall** sit at the lowest level that shows its requirement: a codec test of bytes, a test of a layer over a stream the test controls, a test of the API, or a live test against a server. | review |
| R-TEST-2 | Tests **shall** derive from the spec and turborel-client, not from the code: normal cases, boundaries included, and robustness cases with the exact SQLCODE and SQLSTATE expected. | review |
| R-TEST-3 | A test's name **shall** say the behaviour checked. | review |
| R-TEST-4 | Test code may assert, print and use literal fixtures; every other rule applies. | the linters' configuration |
| R-TEST-5 | Tests **shall** be deterministic and independent: no clock, randomness, network beyond loopback, or order between them. A case generator uses a fixed seed. A live test runs against a server on loopback that its own set-up starts, on its own port and data directory. | review |
| R-TEST-6 | Every fixed defect **shall** get a regression test that fails without the fix, its doc comment saying so. | review |
| R-TEST-7 | A fixture or helper shared by tests **shall** be written once, in one place. Tests use the one framework D9 or later names, no other. | review |
| R-TEST-8 | Live tests **shall** run against `ghcr.io/turborel/turborel:nightly` in CI, set up by the repository's own server set-up, with test certificates, SCRAM users and SERVER.TRUST_LOCAL. | CI |
| R-TEST-9 | SCRAM **shall** be tested by RFC 7677's test vector, and settings by the URI, service file, password file and environment cases of turborel-client's settings tests, so every driver reads settings as turborel-client does. | review |

### R-TRACE: traceability

| ID | Rule | Checked by |
|---|---|---|
| R-TRACE-1 | Every test **shall** cite what it verifies, by its doc comment, beginning `Verifies:`, beginning with a tag of D6: `[PROTOCOL <heading>]`, `[CLIENT <heading>]`, `[DESIGN] D<n>` or `[TOOL] R-<id>`. | the checks tool |
| R-TRACE-2 | A citation **shall** name a heading, decision or rule that exists. | the checks tool |
| R-TRACE-3 | Code implementing a requirement **shall** cite it (R-DOC-4). | review |
| R-TRACE-4 | CONFORMANCE.md **shall** change with the change that implements or changes a row. A row is marked done only where the driver's code for it exists and tests citing its section pass, or, of a driver built over another, as CONFORMANCE.md says. | review |

---

## 5. Coverage

| ID | Rule | Checked by |
|---|---|---|
| R-COV-1 | Coverage **shall** be measured by `cmake --workflow --preset coverage`, over the library and every test. | the checks tool |
| R-COV-2 | Coverage **shall not** fall below the floor (D7); a change raising it **should** raise the floor. | the checks tool, in CI |
| R-COV-3 | A gap is closed by a test derived from a requirement, or by removing dead code; never by a test of no requirement, and never by excluding the code. | review |

---

## 6. Process and tools

| ID | Rule | Checked by |
|---|---|---|
| R-PROC-1 | `cmake --workflow --preset verify` **shall** pass before every commit; CI adds every platform, every supported runtime and the coverage floor (D4). | pre-commit hook; CI |
| R-PROC-2 | Every change **shall** be reviewed against the checklist below. | review |
| R-PROC-3 | Commits **should** be small and single-purpose, in CONTRIBUTING.md's form (`area: summary`, then why), citing the rules applied or changed. | review |
| R-PROC-4 | A threshold change **shall** change RULES.md and the configuration that holds it together. | review |
| R-PROC-5 | Project automation **shall** be written in the language's own tooling, never shell scripts; the git hook only hands over to the checks tool. | review |
| R-PROC-6 | CI's actions **shall** be pinned by their full commit SHA, with the version in a comment. | review |
| R-PROC-7 | A release **shall** be published only as VERSIONING.md says, by the owner (D5). | review |

The review checklist:

- the spec or turborel-client cited in code (R-DOC-4) and tests
  (R-TRACE-1);
- normal and robustness tests (R-TEST-2), and a failing-first test for a
  fix (R-TEST-6);
- every failure the driver's, carrying its condition (R-ERR);
- no magic numbers or strings, protocol tags named (R-CONST);
- every length a server sends bounded, no secret disclosed (R-SAFE);
- comments explain why (R-DOC-2);
- CONFORMANCE.md (R-TRACE-4), the manual (R-DOC-7) and TODO.md updated
  where they change.

---

## 7. The decisions

### Shared by every Turborel driver

#### D1. The native protocol, version 1, in layers that follow turborel-client

The driver speaks Turborel's native protocol, version 1, as
`doc/client/05-protocol.md` in Turborel's repository lays it out,
itself, in C17, with no Rust code of Turborel's linked in. Where that page and turborel-protocol's code differ,
the code is right, and the driver follows it; CONFORMANCE.md names each
such place.

The reference is turborel-client. The driver does all it does before it
is called done, row by row in CONFORMANCE.md, and is built in the same
layers as turborel-client's modules, so a reviewer checks one against
the other. Each layer is a commit area of CONTRIBUTING.md.

| Layer | What it covers | turborel-client |
|---|---|---|
| `codec` | the 5-byte frame header; fields; the tail and its tag order; the data types and values; JSON values; zlib `z` frames within the length bound; the 16 MiB body limit | `protocol/src` |
| `transport` | TCP on 7422; Unix sockets; the Windows named pipe `\\.\pipe\turborel`; TLS begun on the same port; the connect and read timeouts | `target.rs`, `timeouts.rs`, `pipe.rs` |
| `auth`, `tls` | trust, password, certificate, SCRAM-SHA-256 and -PLUS; secrets refused over plain TCP; the SCRAM key cache; TLS | `mechanism.rs`, `scram.rs`, `tls.rs` |
| `session` | Hello, Ready, Status, notices, conditions, diagnostics, autocommit, cancel, terminate, ping, reset, change-user, query attributes, multi-statement | `connection.rs`, `session.rs`, `cancel.rs`, `told.rs` |
| `statements` | queries and streamed queries; prepare, execute, fetch, close, deallocate; numbered cursors; describe; completions | `statements.rs`, `frames.rs` |
| `lobs`, `copy` | locators, their reads, writes and streams; copy, import and export | `lobs.rs`; `copy.rs`, `import/`, `export.rs` |
| `settings` | the URI, options, variables, service and password files, several hosts, roles, TLS modes, channel binding | `settings/`, `failover.rs`, `role.rs` |
| `pipeline`, `pool` | pipelines; pools; the SQL-agent | `pipeline.rs`; `pool/`, `agent.rs` |
| `api` | the standard API (D2) | the language's own |

The `codec` layer is `src/codec/`; each later layer is a directory of
`src/` of its own name. Every layer is internal, its symbols hidden
(D13); the public interface is `include/turborel/turborel.h` alone. TLS,
SHA-256, HMAC and PBKDF2 come from OpenSSL 3.5 or later, or AWS-LC, and
compressed frames from zlib (D12). SCRAM-SHA-256-PLUS binds by
`tls-exporter` over TLS 1.3 and `tls-server-end-point` over TLS 1.2, as
OpenSSL gives both. turborel-cpp wraps this library's C API and speaks
no protocol of its own.

#### D2. The language's standard API, over a low-level client of turborel-client's features

The driver's standard interface is a libpq-style C API, and an ODBC 3.x driver over it, so a program, and the
tools built for C's database interface, use it as they use
any other driver. It sits over a low-level client that has every feature
of turborel-client. What the standard interface has no place for, such
as pipelines, pools, copy, large objects by locator, notices and the
session's status, is reached through the low-level client or the
driver's own extensions of the standard interface.

Every failure is the driver's error, carrying its SQLCODE and SQLSTATE
(R-ERR). The standard interface is built at milestone M7 and checked in
M8's audit; until then its names may change.

The C API is in the manner of PostgreSQL's libpq, the C interface most
database programmers know: a connection handle made from settings or a
URI, results read row by row and column by column, a value as text or by
a typed accessor, and every call answered by a status (D9). Its names
are its own, `trl_`. An exact number is a `trl_decimal`, its units a
`trl_int128` of two 64-bit halves and its scale a `uint32_t`, with
`trl_decimal_text` and `trl_decimal_parse` between it and its text.

An ODBC 3.x driver, `libturborelodbc`, is built over the C API in its own
CMake target, so that Excel, Power BI, Tableau and LibreOffice, which
load an ODBC driver as it is, reach Turborel. It is written from
Microsoft's public ODBC reference, the *ODBC Programmer's Reference*. It
claims no part of ISO/IEC 9075-3 (SQL/CLI) and cites none. It starts once
the C API reaches M4, and is built beside the C API's M5 to M8.

#### D3. The runtime minimum follows the ecosystem's own support

The driver supports a C17 compiler, GCC, Clang or MSVC, and CMake 3.25 or later. The rule is the ecosystem's own:
support every release its makers still support, and drop each at its
end of life. Where a major framework of the language sets a higher
floor, the driver matches it. The minimum changes in a commit of its
own, which changes this decision, the package's metadata, CI and
README.md together.

The library is ISO C17, without extensions (`C_EXTENSIONS OFF`), built by
GCC, Clang and MSVC. C17 is the newest C that all three compile
entirely; MSVC's C23 is not complete. Within C17, nothing optional is
used that MSVC lacks: no variable-length arrays, no `_Atomic` and no
`<threads.h>`. MSVC has no 128-bit integer, so the protocol's `i128` is
two 64-bit halves (D2). CMake 3.25 is the first with workflow presets,
which run each check from one command (D4). The compilers supported are
those CI runs: GCC and Clang on Linux, Apple's Clang on macOS, and MSVC
on Windows.

#### D4. One command runs every check, before each commit and in CI on three platforms

`cmake --workflow --preset verify` runs every check a change must pass, stopping at the
first failure: the formatter, the linters, the type checker where the
language has one, the checks tool's own rules (lengths a linter does not
measure, citations and their targets, open-work markers, the manual's
titles, `[Contents](README.md)` lines and links), and every test.
`cmake --workflow --preset coverage` measures coverage and holds it to its floor (D7).

`.githooks/pre-commit` hands over to the same command. It is a script
only because git runs hooks as scripts. CI runs it on Linux, macOS and
Windows, over every runtime D3 supports, with the live tests against the
nightly image and coverage on Linux. Every action is pinned by its full
commit SHA, its version in a comment, and Dependabot proposes each bump.

The checks are CMake's: the presets of `CMakePresets.json`, `debug`,
`release`, `asan-ubsan`, `coverage`, `lint` and `fuzz`, each a workflow
that configures, builds and tests, and `verify` is the `lint` workflow.

- **The formatter** is clang-format, with `.clang-format`, run by the
  test `rules.format`.
- **The linters** are the compilers' warnings, every one an error:
  `-Wall -Wextra -Wpedantic -Werror` and those of
  `cmake/TrlWarnings.cmake` on GCC and Clang, `/W4 /WX` on MSVC; and
  clang-tidy, with `.clang-tidy`, its `bugprone`, `cert`,
  `clang-analyzer`, `concurrency`, `misc`, `performance`, `portability`
  and `readability` checks, every one an error, as each file compiles.
  C has no type checker beyond its compilers.
- **The checks tool** is the CMake scripts of `cmake/`, run as the test
  `rules.source`: file lengths, doc comments, banned calls, open-work
  markers, citations and their targets, and the manual's titles,
  `[Contents](README.md)` lines and links. Being CMake script, it runs
  the same on every platform with no interpreter beside CMake.
- **The tests** run under CTest (D14); AddressSanitizer and
  UndefinedBehaviorSanitizer run them in CI on Linux and macOS; and
  libFuzzer fuzzes the codec's decoders in CI, built by LLVM's Clang.
- **The API reference** is built by Doxygen in CI, every warning an
  error.

clang-format and clang-tidy are pinned by `.github/lint-requirements.txt`,
installed from the Python Package Index's builds of LLVM's tools into a
virtual environment, as formatting and lints change from one LLVM to the
next; they link nothing into the library.

#### D5. The version is the Turborel release's: 0.0.0 in the alpha, speaking protocol 1

The driver has no version scheme of its own. Its version is the number
of the Turborel release it is made for, the same as the server's: a
Turborel release and each driver's release for it share the number,
**MAJOR.MINOR.PATCH**, as Turborel's VERSIONING.md numbers it. The alpha
is 0.0.0, as Turborel's is, and speaks protocol version 1.

The number is written once, in `TRL_VERSION_MAJOR`, `TRL_VERSION_MINOR` and `TRL_VERSION_PATCH`, in `include/turborel/turborel.h`. Nothing is
published, and no release is tagged, until every Turborel driver is
done; then the owner publishes them together (VERSIONING.md).

#### D6. Tests and code cite Turborel's documents by their headings

A test cites what it verifies, and code what it implements, by the
heading of the section that says it, as printed:

- `[PROTOCOL <heading>]`, a heading of The Native Protocol,
  `doc/client/05-protocol.md`, as `[PROTOCOL Frames]`;
- `[CLIENT <heading>]`, a heading of The Client Library,
  `doc/client/02-library.md`, or of Settings, Pipelines and Pools,
  `doc/client/02-library-connections.md`, which share no heading, as
  `[CLIENT Pipelines]`;
- `[DESIGN] D<n>`, a decision of this file;
- `[TOOL] R-<id>`, a rule the checks tool holds, for the tool's own
  tests.

What else a test relies on follows the tag, as an RFC by its section:
`[PROTOCOL Mechanisms] [RFC 7677 §3]`. The heading `Lob 'L'` is printed
twice in The Native Protocol, under Requests and under Replies, so a
citation of it says which it means.

The checks tool keeps the headings of those three documents, copied at
the Turborel commit CONFORMANCE.md names, and refuses a citation of a
heading, decision or rule that is not there (R-TRACE-2). In C,
a test cites by its doc comment, beginning `Verifies:`.

#### D7. A coverage floor, enforced in CI, that only rises

`cmake --workflow --preset coverage` measures the library's coverage over every test,
and CI fails below the floor. In every Turborel driver the floor is at
least 90 % of lines, and of branches where the tool measures them. It
only rises: a change that raises coverage well above it raises it. A
floor below 100 % says why.

The floor is 95 % of lines and functions, and 90 % of regions and
branches, of `src/`, as Clang's source-based coverage counts them over
the tests (`cmake/CoverageFloors.cmake`, `cmake/coverage-report.cmake`).
It is below 100 % for code of one platform, measured on Linux alone, and
for the failures of allocation, which a test reaches only in part.

#### D8. Where these rules depart from C's conventions

The driver follows its language's library conventions for layout,
naming, packaging, documentation and testing. Where one conflicts with a
rule here, the rule is kept:

- Libraries commonly leave the length of functions and files to taste.
  These are bounded (R-SIZE).
- Libraries commonly fail an argument out of range with the language's
  own error. Here every failure is the driver's, an argument's too, with
  the SQLCODE and SQLSTATE turborel-client gives it (R-ERR-1), so a
  program handles one kind of failure.

- A C library commonly uses `assert`; this one never does, nor anything
  else that ends the process (R-ERR-1), since a library's caller decides
  what a failure means.
- libpq answers some calls with a pointer or an integer whose meaning
  varies; every call here that can fail returns a `trl_status` (D9).
- C has no namespaces, so every name the library gives the linker begins
  with `trl_`, the hidden ones too, so a static library clashes with
  nothing; and a type ends in no `_t`, which POSIX reserves.
- Doc comments are Doxygen's, the convention of C libraries, checked by
  Clang's `-Wdocumentation`, by `rules.source` and by Doxygen.

Where the driver departs from turborel-client itself:

- turborel-client parses an XML value's serialization with Turborel's
  own XML parser, which reads a document type declaration and expands
  the entities it declares. This driver parses it as well-formed
  content, by XML 1.0's rules or 1.1's, and does not read a document
  type declaration: text holding one is refused, SQLSTATE 2200N, saying
  so. A name is checked by XML's ASCII rules, any character past ASCII
  taken as a name character, where Turborel checks each against the
  tables of XML 1.0 and 1.1. Its bounds, 512 deep, 2^20 nodes and
  16 MiB of text, and their failure, SQLCODE -900, are Turborel's.

### This driver's own

#### D9. Failures are status codes, with a failure object of SQLCODE and SQLSTATE

A function that can fail returns a `trl_status`, `TRL_OK` for success, so
a caller tests every call the same way. `TRL_FAILED` says what the
failure was is a failure object the handle keeps: its SQLCODE, its
SQLSTATE, its message, the rule it cites, its position in the statement,
whether a transaction is still active, and the diagnostics of tags 7 to
15, as turborel-client's `Failure` has them. It is valid until the next
call on the same handle. A function that makes a handle gives it even
when it fails, holding the failure, as libpq's `PQconnectdb` gives a
connection; only memory not had for the handle itself is
`TRL_NO_MEMORY`, with nothing made. A bad argument is
`TRL_INVALID_ARGUMENT`, and changes nothing.

The driver's own failures carry the codes turborel-client gives them,
and where it has no such case, those of the drivers' shared table: a
connection lost, timed out, or a malformed reply is SQLCODE -919,
SQLSTATE 08006 [PROTOCOL Frames], as is a capability not granted, the
connection kept; a secret refused over plain TCP, or a login refused,
is 28000; memory not had is SQLCODE -918, SQLSTATE TB918, as Turborel
reports resources it cannot grant; what is past Turborel's limits, or
not built in, as TLS in a library built without a TLS library, is
SQLCODE -900, SQLSTATE 0A000; a misuse turborel-client has no case for,
as a canceller of a connection over the program's own stream, is
SQLCODE -910, SQLSTATE TB910. Query attributes past their bounds are
refused before sending with SQLSTATE 08006, the code the server would
fail the connection with, the connection kept. XML text that is not
well-formed content is SQLCODE -301, SQLSTATE 2200N, as Turborel gives
it, citing ISO/IEC 9075-14:2006 draft §10.16 GR6a ii. Each failure the
driver raises cites the rule `DESIGN`, but that one, with the severity
FATAL if it ends the connection, else ERROR, as turborel-client's do. A
null handle or buffer passed in is refused with a status, never
dereferenced, and a parameter refused as it is added, as text that is
not UTF-8, is a status too, nothing sent.

A statement's failure is the connection's; several statements run as
one request give their outcomes and the failure that stopped them, the
call itself succeeding. Warnings are not failures: a completion's
SQLSTATE of class 01, its conditions, and notices are kept on the
connection, apart from its failure.

#### D10. Opaque handles, and ownership said by every function

Each object the library makes, a connection or a result, is an opaque
handle: its struct is declared in the header and defined only in `src/`,
so its layout can change without breaking a program. Each kind of handle
is made by its own functions and released by one `_free` function, which
takes a null handle and does nothing.

Each function's doc comment says what the caller owns afterwards. Unless
it says otherwise, a string or array the library returns is borrowed,
valid until the next call on the same handle or until it is freed, and
what the caller passes in is copied, not kept, but where a function says
it holds a handle: a TLS target holds its TLS settings, and a SCRAM
mechanism its key cache, each until it is freed, so the program may free
its own at once. A mechanism keeps the state of its exchange, so it
serves one login at a time. What the library
allocates it frees. Text is UTF-8, ended by a null byte; binary data
comes with its length. Types are named `trl_name`, without `_t`, which
POSIX reserves.

A result, a prepared statement, the outcomes of several statements, a
parameter list and an XML tree are the caller's, each freed by its own
`_free`, and none depends on the connection after it is made: a result
copies what it holds out of the reply it was read from, so it outlives
the connection's later calls. A batch given to a handler, and a notice
given to one, last only until the handler returns. A cursor is no
handle but a plain struct naming a statement and a number. What the last
completion told, its conditions and the notices kept are the
connection's, valid until the next statement.

#### D11. No global mutable state, and one thread at a time on a handle

The library keeps no global mutable state and needs no initialization
call (R-SAFE-5). A handle is used by one thread at a time; different
handles may be used on different threads at once. Two depart from that
by design, as their doc comments say: a SCRAM key cache, which every
thread may use at once, behind a lock of its own (POSIX threads'
mutex, or a Windows SRW lock), its holds counted under it; and a
canceller, made of a connection on the connection's thread, then used on
another to cancel its statement. The library starts no thread
(R-SAFE-6), and a callback the program gives, as a stream's, runs on
the thread that made the call. On Windows each socket starts and cleans
up Winsock itself, which counts its users, so no call of the program's
must come first.

#### D12. Dependencies: the C library, OpenSSL 3.5 or AWS-LC, and zlib

The C standard library comes first. Beyond it the library uses two
libraries, each production-grade by its own maintainers' account
(R-DEP-1, R-DEP-2):

| Library | For | Version | License | Why |
|---|---|---|---|---|
| OpenSSL | TLS, SHA-256, HMAC, PBKDF2, random numbers | 3.5 or later | Apache-2.0 | 3.5 is OpenSSL's long-term support release; 3.0's support ended in 2026-09. It gives `SSL_export_keying_material` for SCRAM's `tls-exporter` binding. |
| AWS-LC | the same, in OpenSSL's place | its current release | Apache-2.0 and ISC | AWS's library, API-compatible with OpenSSL, for those who need its FIPS-validated builds. |
| zlib | compressed frames, RFC 1950 | 1.2.11 or later | zlib | The reference implementation of the format the protocol's `z` frames carry. |

Each platform's own libraries come with it: Winsock and the certificate
store's crypt32 on Windows; the Security and Core Foundation frameworks
on macOS, for the keychain's authorities; POSIX threads elsewhere, for
the key cache's lock.

`TRL_TLS` chooses OpenSSL, AWS-LC or none, and `TRL_ZLIB` whether zlib is
looked for (`cmake/TrlDependencies.cmake`). A library not found is not
required: a build without one builds, and refuses at run time what needs
it, with a failure saying the library was built without it.

#### D13. CMake with presets, an installed package, and hidden symbols

The library is `libturborel` on every platform, shared unless
`BUILD_SHARED_LIBS` is `OFF`; static, it is used with `TRL_STATIC`
defined, which the CMake package and the pkg-config file define for the
program. Its interface is one header to include,
`include/turborel/turborel.h`, which includes the interface's parts,
each a header of its own: `base.h`, `numbers.h`, `failure.h`,
`connect.h` and `connection.h`.
Symbols are hidden by default (`C_VISIBILITY_PRESET hidden`), and only
those marked `TRL_API` are exported, so what is not in the header cannot
be linked to (R-STYLE-4).

`cmake --install` installs the library, the header, a CMake package
(`find_package(turborel)`, target `turborel::turborel`), a relocatable
pkg-config file (`turborel.pc`) and the license. The package asks for an
exact version, as a release's driver goes with that release's server
(D5). The `release` preset tests that a program outside the tree builds
against what was installed, both ways (`cmake/test-package.cmake`).

The library's sources build one object library, `turborel_objects`, of
which the library is made and which the tests link, so a test reaches
the internal codec the library hides.

#### D14. Tests run by CTest, on one harness of our own

Tests are C programs that CTest runs, one executable for each file,
named `layer.thing` (`codec.frame`), on one harness, `test/harness.h`
(R-TEST-7). No test framework is a dependency: the harness is what the
tests need, a case's checks and a report of each that failed, and
nothing more. A case alone runs by naming it as its executable's
argument. The checks tool runs as tests too, `rules.source` everywhere
and `rules.format` under `lint`, so `ctest` runs every check a build can
make.

The live tests, `live.connections`, start the server the environment
variable `TRL_TEST_SERVER` names, a path of turboreld or a command that
runs it, as CI's `docker run` of the nightly image, on port 17438, with
certificates made for the run (`test/server/`); without it they are
skipped (CTest's code 77), so the other tests stay deterministic
(R-TEST-5). Tests of real sockets and TLS without a server run against a
fake server of their own on a thread (`test/net/fake.c`).

A build of `TRL_FAULTS`, which the `debug`, `asan-ubsan`, `coverage` and
`lint` presets make and no package ships, lets a test fail any
allocation of the library's, and of the TLS library's, on purpose: the
test `api.faults` fails each allocation of a whole session in turn, and
checks that every one fails its call as memory not had, or as the
connection lost, and leaks nothing (R-MEM-2, R-ERR-4). The count it
keeps is a thread's own, in that build alone, the one departure from
R-SAFE-5.

---

## 8. References

- Turborel, `doc/client/05-protocol.md`, *The Native Protocol*,
  version 1: the requirements of the codec and every message.
- Turborel, `doc/client/02-library.md`, *The Client Library*, and
  `doc/client/02-library-connections.md`, *Settings, Pipelines and
  Pools*, and the crates turborel-client and turborel-protocol: the
  features to match, and the reference where the spec is silent or
  differs.
- Turborel, `RULES.md` D89, the JSON value's binary tree, and D184, the
  UUID's tag.
- RFC 1950, *ZLIB Compressed Data Format Specification*; RFC 5802 and
  RFC 7677, SCRAM and SCRAM-SHA-256; RFC 5929 and RFC 9266, the
  `tls-server-end-point` and `tls-exporter` channel bindings.
- The standards of C and of its database interface, named in
  D2 and D3.
