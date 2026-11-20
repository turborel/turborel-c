# turborel-c rules

How turborel-c is designed, written and verified. Every change follows
these rules; `cmake --workflow --preset verify` checks those a tool can
check, and review checks the rest.

Rules (R-…) say how code and tests are written. **shall** is mandatory,
and a departure needs a recorded deviation in this file. **should** is
expected, and a reviewer may accept a reasoned exception.

Decisions (D1…) record how the driver is built and why. Change one only by
changing this file in a reviewed commit. A decision says what was chosen
and why, and the file it names holds the mechanism.

Cite both by ID in code, tests, reviews and commit messages, as in
`RULES.md D4` or `R-ERR-1`.

Before changing code, read §3 (code rules), §4 (test rules) and the
decisions covering your area. What the driver implements of the protocol
and of turborel-client is in [CONFORMANCE.md](CONFORMANCE.md), and open
work is in [TODO.md](TODO.md).

The rules are modelled on turborel's own RULES.md, adapted to C.

[1 Decisions](#1-decisions) · [2 Layout](#2-project-layout) ·
[3 Code](#3-code-rules) · [4 Tests](#4-test-rules) ·
[5 Coverage](#5-coverage) · [6 Platforms](#6-platforms) ·
[7 Process](#7-process-and-tools) · [8 References](#8-references)

---

## 1. Decisions

| # | Decision |
|---|---|
| **The driver** | |
| D1 | Native protocol 1, written in C, with no native core |
| D2 | A libpq-style C API, and an ODBC 3.x driver over it |
| D3 | C17, built by GCC, Clang and MSVC, with CMake 3.25 or later |
| D9 | The version is the turborel release's |
| **The interface** | |
| D4 | Failures are status codes, with a failure object of SQLCODE and SQLSTATE |
| D5 | Opaque handles, and ownership said by every function |
| D6 | No global mutable state, and one thread at a time on a handle |
| **Build and tooling** | |
| D7 | Dependencies: the C library, OpenSSL 3.5 or AWS-LC, and zlib |
| D8 | CMake with presets, an installed package, and hidden symbols |
| D10 | Warnings as errors, clang-tidy, clang-format, sanitizers, coverage and fuzzing |
| D11 | Tests run by CTest, on one harness of our own |
| D12 | clang-format and clang-tidy pinned by version |
| D13 | Automation in CMake, never shell scripts |
| D14 | Where C's conventions and these rules differ |

### The driver

#### D1. Native protocol 1, written in C, with no native core

libturborel speaks turborel's native protocol, version 1, itself, from
`doc/client/05-protocol.md` in turborel's repository. Where that page and
turborel-client's code differ, the code is right, and the difference is
noted in CONFORMANCE.md.

It links no code of turborel's: not turborel-client through a foreign
function interface, nor anything else in Rust. So it builds wherever a
C17 compiler does, with no Rust toolchain, and a program links one C
library. turborel-cpp wraps this library's C API and speaks no protocol
of its own.

Its layers follow turborel-client's modules, so a reviewer checks one
against the other: the codec (`protocol/src`), transport, authentication,
the session, statements, large objects, copy, settings, pipelines, pools,
and the API over them. The codec does no I/O, as turborel-protocol's does
not, so it is tested byte by byte (`src/codec/`).

#### D2. A libpq-style C API, and an ODBC 3.x driver over it

The C API is in the manner of PostgreSQL's libpq, the C interface most
database programmers know: a connection handle made from settings or a
URI, results read row by row and column by column, a value as text or by
a typed accessor, and every call answered by a status (D4). Its names are
its own, `trl_`, and it has every feature of turborel-client
(CONFORMANCE.md).

An ODBC 3.x driver, `libturborelodbc`, is built over the C API in its own
CMake target, so that Excel, Power BI, Tableau and LibreOffice, which load
an ODBC driver as it is, reach turborel. It is written from Microsoft's
public ODBC reference, the *ODBC Programmer's Reference*. It claims no
part of ISO/IEC 9075-3 (SQL/CLI) and cites none. It starts once the C API
reaches M4, its statements, and is built beside the C API's M5 to M8.

#### D3. C17, built by GCC, Clang and MSVC, with CMake 3.25 or later

The library is ISO C17, without extensions (`C_EXTENSIONS OFF`). C17 is
the newest C that GCC, Clang and MSVC all compile entirely; MSVC's C23 is
not complete. Code that one compiler needs and the others refuse is
written under `#if` for that compiler alone, and a feature a platform
lacks is refused there, saying so (R-PLAT-2).

Within C17, nothing optional is used that MSVC lacks: no variable-length
arrays (`-Wvla`), and no `_Atomic` or `<threads.h>`. MSVC has no 128-bit
integer, so the protocol's `i128` is carried as two 64-bit halves.

CMake 3.25 is the first with workflow presets, which run each check from
one command (D8, D13).

The compilers supported are the versions CI runs: GCC and Clang on Linux,
Apple's Clang on macOS, and MSVC on Windows (`.github/workflows/verify.yml`).

#### D9. The version is the turborel release's

A driver's version is the turborel release it is made for, the same
number as the server's (VERSIONING.md). There is no version scheme of the
driver's own. The alpha is 0.0.0, as turborel's is, and speaks protocol
version 1.

The number is written once, as `TRL_VERSION_MAJOR`, `_MINOR` and `_PATCH`
in `include/turborel/turborel.h`. CMake reads it from there for the
project, the shared library's version, the CMake package and the
pkg-config file, and `trl_version()` gives it at run time, so a program
can tell a header and a library of different releases apart. The CMake
package asks for an exact version, as a release's driver goes with that
release's server.

### The interface

#### D4. Failures are status codes, with a failure object of SQLCODE and SQLSTATE

A function that can fail returns a status code, zero for success, so a
caller tests every call the same way. What the failure was is a failure
object the handle keeps: its SQLCODE, its SQLSTATE, its message, the rule
it cites, its position in the statement, whether a transaction is still
active, and the diagnostics of tags 7 to 15, as turborel-client's
`Failure` has them. It is valid until the next call on the same handle.

The driver's own failures carry the codes turborel-client gives them: a
connection lost or a malformed reply is SQLCODE -919, SQLSTATE 08006
[PROTOCOL Frames]; a secret refused over plain TCP is 28000. A failure is
never a crash, an exit or a message printed (R-ERR-1). A null handle or
buffer passed in is refused with a status, never dereferenced. Memory not
had is a status too.

#### D5. Opaque handles, and ownership said by every function

Each object the library makes, a connection or a result, is an opaque
handle: its struct is declared in the header and defined only in `src/`,
so its layout can change without breaking a program. Each kind of handle
is made by its own functions and released by one `_free` function, which
takes a null handle and does nothing.

Each function's doc comment says what the caller owns afterwards. Unless
it says otherwise, a string or array the library returns is borrowed,
valid until the next call on the same handle or until it is freed, and
what the caller passes in is copied, not kept. What the library allocates
it frees, as a program and a DLL on Windows may not share an allocator.
Text is UTF-8, ended by a null byte; binary data comes with its length.

Types are named `trl_name`, without `_t`, which POSIX reserves.

#### D6. No global mutable state, and one thread at a time on a handle

The library keeps no global mutable state and needs no initialization
call. A handle is used by one thread at a time; different handles may be
used on different threads at once. Where a function departs from that, as
the cancel of a running statement does by design, its doc comment says
so. The library starts no thread, and a callback the program gives runs on
the thread that made the call. A server runs many sessions, and a
program's threads each hold their own, so no lock in the library is
shared between handles.

### Build and tooling

#### D7. Dependencies: the C library, OpenSSL 3.5 or AWS-LC, and zlib

The C standard library comes first. Beyond it the library uses two
libraries, each production-grade by its own maintainers' account:

| Library | For | Version | License | Why |
|---|---|---|---|---|
| OpenSSL | TLS, SHA-256, HMAC, PBKDF2, random numbers | 3.5 or later | Apache-2.0 | 3.5 is OpenSSL's long-term support release; 3.0's support ended in 2026-09. It gives `SSL_export_keying_material` for SCRAM's `tls-exporter` binding. |
| AWS-LC | the same, in OpenSSL's place | its current release | Apache-2.0 and ISC | AWS's library, API-compatible with OpenSSL, for those who need its FIPS-validated builds. |
| zlib | compressed frames, RFC 1950 | 1.2.11 or later | zlib | The reference implementation of the format the protocol's `z` frames carry. |

`TRL_TLS` chooses OpenSSL, AWS-LC or none, and `TRL_ZLIB` whether zlib is
looked for (`cmake/TrlDependencies.cmake`). Until the code that uses them
is written, CMake looks for each and reports what it found, and requires
neither. Then a build without one still builds, and refuses at run time
what needs it, with a failure saying the library was built without it.

Any other library linked needs a decision here first (R-DEP-1). Tools
that build and check, and link nothing into the library, are D10's and
D12's.

#### D8. CMake with presets, an installed package, and hidden symbols

The build is CMake's, and its configurations are presets
(`CMakePresets.json`): `debug`, `release`, `asan-ubsan`, `coverage`,
`lint` and `fuzz`, each building into `build/<preset>`, and workflows of
the same names that configure, build and test in one command.

The library is `libturborel` on every platform, shared unless
`BUILD_SHARED_LIBS` is `OFF`; static, it is used with `TRL_STATIC`
defined, which the CMake package and the pkg-config file define for the
program. Its interface is one header, `include/turborel/turborel.h`.
Symbols are hidden by default (`C_VISIBILITY_PRESET hidden`), and only
those marked `TRL_API` are exported, so what is not in the header cannot
be linked to.

`cmake --install` installs the library, the header, a CMake package
(`find_package(turborel)`, target `turborel::turborel`), a pkg-config
file (`turborel.pc`) and the license. The `release` preset tests that a
program outside the tree builds against what was installed, both ways
(`cmake/test-package.cmake`).

The library's sources build one object library, `turborel_objects`, of
which the library is made and which the tests link, so a test reaches the
internal codec the library hides (`CMakeLists.txt`).

#### D10. Warnings as errors, clang-tidy, clang-format, sanitizers, coverage and fuzzing

- **Warnings.** GCC and Clang build with `-Wall -Wextra -Wpedantic
  -Werror` and the warnings of `cmake/TrlWarnings.cmake`, conversions and
  shadowing among them; MSVC with `/W4 /WX`. Clang checks doc comments
  against what they document (`-Wdocumentation`).
- **clang-tidy**, with `.clang-tidy`: the `bugprone`, `cert`,
  `clang-analyzer`, `concurrency`, `misc`, `performance`, `portability`
  and `readability` checks, every one an error, run as each file compiles
  under the `lint` preset.
- **clang-format**, with `.clang-format`, checked by the test
  `rules.format` under `lint`.
- **AddressSanitizer and UndefinedBehaviorSanitizer**, under the
  `asan-ubsan` preset, in CI on Linux and macOS; MSVC has AddressSanitizer
  alone.
- **Coverage**, Clang's source-based coverage, read by `llvm-cov`, under
  the `coverage` preset, held to the floors of `cmake/CoverageFloors.cmake`
  (R-COV). GCC's `gcov` is not used, so coverage is one tool's count.
- **Fuzzing**, libFuzzer targets of the codec under the `fuzz` preset
  (`fuzz/`), built with LLVM's Clang and the sanitizers, and run in CI for
  a bounded time. Apple's Clang does not ship libFuzzer.
- **Documentation**, built by Doxygen in CI, every warning an error, so
  every public item is documented (R-DOC-1).

#### D11. Tests run by CTest, on one harness of our own

Tests are C programs that CTest runs, one executable for each area, named
`area.thing` (`codec.frame`), on one harness, `test/harness.h`. No test
framework is a dependency: the harness is what the tests need, a case's
checks and a report of each that failed, and nothing more. A case alone
runs by naming it as its executable's argument.

The rules a tool checks run as tests too, `rules.source` everywhere and
`rules.format` under `lint`, so `ctest` runs every check a build can make.

#### D12. clang-format and clang-tidy pinned by version

Formatting and lints change from one LLVM to the next, so a contributor
and CI run the same versions: those `.github/lint-requirements.txt` pins,
installed from the Python Package Index's builds of LLVM's tools into a
virtual environment. They link nothing into the library. Dependabot
proposes each new version, and a change of version is its own commit,
with the code it reformats.

#### D13. Automation in CMake, never shell scripts

The project's checks are CMake scripts in `cmake/`, run by CTest or by
`cmake -P`, so they run as they are on Linux, macOS and Windows with no
shell or interpreter beside CMake. The one script of another kind is
`.githooks/pre-commit`, which git runs through `sh` (on Windows, the one
Git for Windows ships), and it only hands over to CMake.

#### D14. Where C's conventions and these rules differ

Where the C libraries of 2026 do one thing and these rules another, the
rule is kept:

- A C library commonly uses `assert`; this one never does, nor anything
  else that ends the process (R-ERR-1), since a library's caller decides
  what a failure means.
- libpq answers some calls with a pointer or an integer whose meaning
  varies; every call here that can fail returns a status (D4).
- Large C files are common; here a file has at most 600 lines and a
  function 50 (R-SIZE).
- C has no namespaces, so every name the library gives the linker begins
  with `trl_`, the hidden ones too, so that a static library clashes with
  nothing.
- Doc comments are Doxygen's, the convention of C libraries, and are
  checked by Clang, by `rules.source` and by Doxygen.

---

## 2. Project layout

```
CMakeLists.txt          the library: objects, target, options (D8)
CMakePresets.json       the configurations and workflows (D8)
cmake/                  CMake modules and the check scripts (D13)
  TrlWarnings.cmake     C17 and the warnings (D3, D10)
  TrlChecks.cmake       clang-tidy, sanitizers, coverage, fuzzing (D10)
  TrlDependencies.cmake OpenSSL or AWS-LC, and zlib (D7)
  TrlInstall.cmake      the installed library, package and pkg-config (D8)
  CoverageFloors.cmake  the coverage floors (R-COV-2)
  check-source.cmake    the source rules, as the test rules.source
  check-format.cmake    formatting, as the test rules.format
  coverage-report.cmake the coverage report and its floors
  test-package.cmake    the installed package, as a program uses it
  ci-run.cmake          a CI step, its failure's output an annotation
.clang-format           formatting (R-STYLE-1)
.clang-tidy             lints (R-LINT-2)
Doxyfile                the API reference, built in CI (R-DOC-1)
include/turborel/       the public header (D8)
src/                    the library; one concept per file
  codec/                the protocol's encoding, without I/O (D1)
test/                   the tests (D11)
  harness.h, harness.c  the harness every test uses (R-TEST-6)
  <area>/               one directory per area, `*_test.c` each
  package/              a program outside the tree, for the package test
fuzz/                   libFuzzer targets (D10)
doc/                    the manual
README.md               what the driver is; building and a first query
RULES.md                this file
CONFORMANCE.md          the parity matrix with turborel-client
TODO.md                 open work, by milestone
VERSIONING.md           the version, and how it is published
CONTRIBUTING.md         changes, commit messages, pull requests, issues
SECURITY.md             how to report a vulnerability
TRADEMARK.md            the name and the logos
AGENTS.md               the guide for coding agents
CLAUDE.md               points Claude Code to AGENTS.md
.github/                CI, issue forms, labels, the pull request template
.githooks/pre-commit    runs `verify` before each commit
artwork/                the logos
```

---

## 3. Code rules

### R-STYLE: formatting and names

| ID | Rule | Checked by |
|---|---|---|
| R-STYLE-1 | Code **shall** be formatted by clang-format with `.clang-format`. | `rules.format` |
| R-STYLE-2 | Every public name **shall** begin `trl_`, every public macro `TRL_`; every name the linker sees, hidden ones too, begins `trl_` (D14). | review; `nm` |
| R-STYLE-3 | Functions, variables and types are `lower_case`; macros and enumeration constants `UPPER_CASE`; types end in no `_t` (D5). | `readability-identifier-naming` |
| R-STYLE-4 | Production code **shall not** print or write to a stream it was not given. | `rules.source` |
| R-STYLE-5 | A header **shall** compile on its own and include what it uses; internal headers live in `src/`, never in `include/`. | build; review |

### R-SIZE: short functions and files

| ID | Rule | Checked by |
|---|---|---|
| R-SIZE-1 | A function **shall** have at most 50 lines. | `readability-function-size` |
| R-SIZE-2 | A source file **shall** have at most 600 lines; split by the protocol's sections or the layers of D1. | `rules.source` |
| R-SIZE-3 | A function **shall** take at most 5 parameters; else a struct. | `readability-function-size` |
| R-SIZE-4 | A file **should** hold one concept, stated in its `@file` comment. | review |

### R-DOC: comments and documentation

| ID | Rule | Checked by |
|---|---|---|
| R-DOC-1 | Every public function, type, enumeration constant and macro **shall** have a Doxygen doc comment; a function's says each parameter, what it returns, and what the caller owns after it (D5). | `rules.source`; `-Wdocumentation`; Doxygen |
| R-DOC-2 | Comments **shall** say *why*: the rule implemented, the choice made, the invariant relied on. | review |
| R-DOC-3 | Behaviour the spec or turborel-client governs **shall** cite it by heading, as `[PROTOCOL Frames]` or `[CLIENT Pipelines]`; the driver's own choices cite their decision, `RULES.md D4`. | review |
| R-DOC-4 | Citations **shall** name a heading the document has; none is cited from memory. | `rules.source` for tests; review |
| R-DOC-5 | No commented-out code, and no `TODO`, `FIXME` or `XXX`: open work goes in TODO.md, and code never cites TODO.md. | `rules.source` |
| R-DOC-6 | Every source file **shall** open with a doc comment, `@file` and its responsibility. | `rules.source` |
| R-DOC-7 | A change a program can see **shall** update the manual, `doc/`, in the same commit, its examples run. | review |
| R-DOC-8 | Every relative link of a document **shall** name a file that exists. | `rules.source` |

### R-CONST: no magic numbers or strings

| ID | Rule | Checked by |
|---|---|---|
| R-CONST-1 | Numbers other than `0` and `1` **shall** be named, by a macro or an enumeration constant, documenting the value's source. | `readability-magic-numbers` |
| R-CONST-2 | Every protocol tag, message kind, capability name and status name **shall** be a named constant, defined once, under the spec heading it comes from. | review |
| R-CONST-3 | Strings matched or looked up (settings, mechanisms, URI parts) **shall** be named constants. Messages for people are not. | review |
| R-CONST-4 | Tests may use literal fixtures, such as the spec's bytes; expected values restating a constant **should** use it. | review |

### R-ERR: failures, not crashes

| ID | Rule | Checked by |
|---|---|---|
| R-ERR-1 | Library code **shall not** end the process or print: no `abort`, `exit`, `assert`, `printf`, `longjmp` or the like. | `rules.source` |
| R-ERR-2 | Every failure **shall** be a status code with a failure object of SQLCODE, SQLSTATE, message and the rest (D4). | review; tests |
| R-ERR-3 | Every status a call returns **shall** be tested, or cast to `(void)` with a comment saying why it cannot matter. | `bugprone-unused-return-value`; review |
| R-ERR-4 | A failed call **shall** leave its handle usable, or say it is lost (08006), and leak nothing. | tests; sanitizers |
| R-ERR-5 | Null pointers passed in **shall** be refused, never dereferenced. | tests |

### R-NUM: arithmetic and conversions

| ID | Rule | Checked by |
|---|---|---|
| R-NUM-1 | No conversion **shall** lose a value or a sign implicitly; a narrowing cast follows a check that the value fits. | `-Wconversion`, `-Wsign-conversion` |
| R-NUM-2 | A size or count computed from a peer's bytes **shall** be checked for overflow before use. | review; UBSan; fuzzing |
| R-NUM-3 | Signed overflow, shifts past a type's width and division by zero **shall** be impossible, shown by checks before the operation. | UBSan; review |
| R-NUM-4 | Integers on the wire **shall** be read and written byte by byte, little-endian, never by casting a buffer to a wider type. | review |
| R-NUM-5 | Floats **shall not** be compared for equality except where the protocol's value is compared bit for bit. | review |

### R-MEM: memory

| ID | Rule | Checked by |
|---|---|---|
| R-MEM-1 | No `strcpy`, `strcat`, `sprintf`, `gets`, `strtok` or `alloca`; copies are bounded by the destination's size. | `rules.source` |
| R-MEM-2 | Every allocation **shall** be checked, and its failure reported as a status (D4). | review; tests |
| R-MEM-3 | A reader **shall** hold what it reads as it goes: a count received never reserves memory before the bytes it counts arrive, and nothing is taken past the body limit [PROTOCOL Values and fields]. | review; fuzzing |
| R-MEM-4 | Every read **shall** be bounded by the bytes available. | ASan; fuzzing |
| R-MEM-5 | A secret (a password, a SCRAM key, a cancel key) **shall** be wiped from memory once used, and never written to a message or a log. | review |

### R-CF: control flow

| ID | Rule | Checked by |
|---|---|---|
| R-CF-1 | A boolean decision **should** have at most 4 conditions; else a named predicate. | review |
| R-CF-2 | Cognitive complexity **shall** be at most 15, nesting at most 5. | `readability-function-cognitive-complexity`, `readability-function-size` |
| R-CF-3 | A `switch` on an enumeration **shall** list every constant, with no `default`. | `-Wswitch-enum` |
| R-CF-4 | `goto` only jumps forward, to the one cleanup at a function's end. | review |
| R-CF-5 | Recursion **shall** be bounded by a named depth constant, failing past it. | review; tests |

### R-SAFE: language subset and resources

| ID | Rule | Checked by |
|---|---|---|
| R-SAFE-1 | No variable-length arrays, and nothing of C17 MSVC lacks (D3). | `-Wvla`; MSVC in CI |
| R-SAFE-2 | No global or static mutable state (D6). | review |
| R-SAFE-3 | No thread **shall** be started by the library (D6). | review |
| R-SAFE-4 | No undefined behaviour, shown by UBSan and ASan in CI and by fuzzing the codec. | `asan-ubsan`; `fuzz` |

### R-LINT: compilers and linters

| ID | Rule | Checked by |
|---|---|---|
| R-LINT-1 | Every target **shall** build with D10's warnings, every one an error, on GCC, Clang and MSVC. | build |
| R-LINT-2 | Every source **shall** pass clang-tidy with `.clang-tidy`, every check an error. | `lint` preset |
| R-LINT-3 | A warning or check **shall** be silenced only by `NOLINTNEXTLINE(check)` or a compiler pragma that names it, with a comment giving the rule and the reason. | review |
| R-LINT-4 | A check is left out of `.clang-tidy` only with its reason written there. | review |

### R-DEP: dependencies

| ID | Rule | Checked by |
|---|---|---|
| R-DEP-1 | A library linked into libturborel **shall** have a decision in §1 giving why, its version, its license and its maintainers' own claim of readiness (D7). | review |
| R-DEP-2 | A tool that builds or checks **shall** be pinned by version where its output depends on it (D12). | review |

---

## 4. Test rules

### R-TEST: levels and layout

| ID | Rule | Checked by |
|---|---|---|
| R-TEST-1 | Each test **shall** sit at the lowest level showing its requirement: the codec's bytes, then a layer, then the API, then a live server. | review |
| R-TEST-2 | Tests **shall** derive from the spec and turborel-client, not the code: normal cases, boundaries, and robustness cases with the exact status, SQLCODE and SQLSTATE expected. | review |
| R-TEST-3 | A case's name **shall** say the behaviour checked: `refuses_a_body_longer_than_taken`. | review |
| R-TEST-4 | Tests **shall** be deterministic and independent: no clock, randomness or network beyond loopback; a live test runs its own server on its own port and directory. | review |
| R-TEST-5 | Every fixed defect **shall** get a regression test that fails without the fix, its doc comment saying so. | review |
| R-TEST-6 | Every test **shall** run on the one harness, `test/harness.h`; a fixture shared by tests is written once beside it. | review |
| R-TEST-7 | Test code may print, and may use literals as fixtures; every other rule applies. | review |

### R-TRACE: traceability

| ID | Rule | Checked by |
|---|---|---|
| R-TRACE-1 | Every test case **shall** have a doc comment beginning `Verifies:` and a citation: `[PROTOCOL <heading>]` of `05-protocol.md`, `[CLIENT <heading>]` of `02-library.md` or `02-library-connections.md`, or `[DESIGN] D<n>` of this file. | `rules.source` |
| R-TRACE-2 | Code implementing a requirement **shall** cite it (R-DOC-3). | review |
| R-TRACE-3 | CONFORMANCE.md **shall** change with the change that implements a row of it. | review |

---

## 5. Coverage

| ID | Rule | Checked by |
|---|---|---|
| R-COV-1 | Coverage **shall** be measured by the `coverage` workflow, over the unit tests, for `src/`. | `coverage` |
| R-COV-2 | Coverage **shall not** fall below the floors in `cmake/CoverageFloors.cmake`, of lines, functions, regions and branches; the floors only rise. | `coverage`, in CI |
| R-COV-3 | Gaps are closed by tests of requirements, or by removing dead code; never by tests that check nothing. | review |

---

## 6. Platforms

| ID | Rule | Checked by |
|---|---|---|
| R-PLAT-1 | Every change **shall** build and pass its tests on Linux, macOS and Windows. | CI |
| R-PLAT-2 | What one platform alone has (a Unix socket's path, a named pipe) is written under `#if` for it, and refused elsewhere with a failure that says so. | review; CI |
| R-PLAT-3 | Code built for one platform only **shall** say, when handed back, that it was not built on the others. | review |

---

## 7. Process and tools

| ID | Rule | Checked by |
|---|---|---|
| R-PROC-1 | `cmake --workflow --preset verify` **shall** pass before every commit: clang-tidy, formatting, the source rules and every test. CI adds every platform, the sanitizers, coverage and fuzzing. | pre-commit hook; CI |
| R-PROC-2 | Every change **shall** be reviewed against the checklist below. | review |
| R-PROC-3 | Commits **should** be small and single-purpose, in CONTRIBUTING.md's form. | review |
| R-PROC-4 | A threshold change **shall** change this file and the file that sets it together. | review |
| R-TOOL-1 | Project automation **shall** be CMake scripts in `cmake/` (D13). | review |
| R-CM-1 | The pinned tools (D12) and the actions CI runs, each by its full commit hash, **shall** change only in a commit of their own. | review |

The review checklist:

- requirement cited in code (R-DOC-3) and tests (R-TRACE-1);
- normal, boundary and robustness tests (R-TEST-2);
- no magic numbers or strings (R-CONST);
- every status tested, every allocation checked (R-ERR-3, R-MEM-2);
- ownership said by each new function (D5);
- comments explain why (R-DOC-2);
- CONFORMANCE.md updated (R-TRACE-3), and TODO.md where open work
  changed.

---

## 8. References

- turborel, `doc/client/05-protocol.md`, *The Native Protocol*, version 1;
  `doc/client/02-library.md`, *The Client Library*, and
  `02-library-connections.md`, *Settings, Pipelines and Pools*; and
  turborel-client's and turborel-protocol's code, which prevails.
- ISO/IEC 9899:2018, *Programming languages — C* (C17).
- Microsoft, *ODBC Programmer's Reference* (D2).
- PostgreSQL, *libpq — C Library*, the manner of the C API (D2).
- RFC 1950, *ZLIB Compressed Data Format Specification*; RFC 5802 and RFC
  7677, SCRAM and SCRAM-SHA-256; RFC 9266, *Channel Bindings for TLS 1.3*.
- CMake's documentation: presets, `install(EXPORT)`, package
  configuration files.
