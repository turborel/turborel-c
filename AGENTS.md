# Guide for coding agents

How to work on turborel-c as a coding agent, with any tool. If this file
disagrees with [RULES.md](RULES.md) or [CONTRIBUTING.md](CONTRIBUTING.md),
follow those.

turborel-c is the C driver for Turborel, a relational
database system. It speaks Turborel's native protocol, version 1,
itself, in C17, with no Rust code of Turborel's linked in, and is built to do all turborel-client does. See
[README.md](README.md).

## Before you change anything

Read RULES.md §3 (code), §4 (tests), §6 (process) and the decisions for
your area. Read CONTRIBUTING.md for how a change is made and the extra
checks for changes that touch the protocol. Read the section of the
protocol, or of the client documents, that your change implements, and
the rows of CONFORMANCE.md it marks done.

## Build and check

Every change must pass `cmake --workflow --preset verify` and `cmake --workflow --preset coverage`
before it is handed back.

```
python3 -m venv .venv-lint                                   # the pinned clang-format and clang-tidy
.venv-lint/bin/pip install -r .github/lint-requirements.txt  # on Windows: .venv-lint\Scripts
git config core.hooksPath .githooks                          # run verify before each commit
cmake --workflow --preset verify                             # every check a change must pass
cmake --workflow --preset coverage                           # coverage, held to its floor
cmake --workflow --preset asan-ubsan                         # the tests under the sanitizers
cmake --workflow --preset fuzz                               # the libFuzzer targets (LLVM's Clang)
ctest --preset debug -R codec.frame                          # one test executable, after the debug workflow
cmake --workflow --preset release                            # the package, built, installed and tested
```

`cmake --workflow --preset verify` stops at the first failure (RULES.md D4). CI runs it
on Linux, macOS and Windows, on each supported runtime, and coverage on
Linux. The coverage floor only rises (D7, R-COV-2).

Project automation is the language's own tooling, never shell scripts
(R-PROC-5).

## Where things are

| Path | What |
|---|---|
| `include/turborel/turborel.h` | the public header, the whole interface (D13) |
| `src/codec/` | the `codec` layer: frames, fields, types, values, JSON, the tail, messages, compression, with no I/O |
| `src/` | the other layers, one directory each, as they are written (D1) |
| `test/harness.h`, `test/harness.c` | the one test harness (D14) |
| `test/<layer>/*_test.c` | the tests, one executable per file, `layer.thing` in CTest |
| `test/package/` | a program outside the tree, for the installed package's test |
| `fuzz/` | the libFuzzer targets of the codec |
| `cmake/` | CMake modules, and the checks tool: `check-source.cmake` with `CheckCitations.cmake` and `CheckManual.cmake`, `check-format.cmake`, `coverage-report.cmake`, `test-package.cmake`, `ci-run.cmake` |
| `CMakeLists.txt`, `CMakePresets.json` | the build, and the workflows every check runs by |
| `.clang-format`, `.clang-tidy`, `Doxyfile` | the formatter, the linters, the API reference |

The protocol is Turborel's `doc/client/05-protocol.md`, the client's
features `doc/client/02-library.md` and `02-library-connections.md`, and
the reference code `protocol/src/` and `client/src/`, all in
[turborel](https://github.com/turborel/turborel). Where the page and the
code differ, the code is right; CONFORMANCE.md names each such place.

## Rules that are broken most often

`cmake --workflow --preset verify` treats every warning as an error (R-LINT-1). RULES.md
§3 and §4 have the full list.

- Every failure is the driver's error, with its SQLCODE and SQLSTATE
  (R-ERR-1, R-ERR-2). No error of the language or a library beneath it
  reaches a program; catch it, and give the driver's, keeping the cause
  (R-ERR-3).
- No printing, and no ending the process, in library code (R-STYLE-3,
  R-ERR-1).
- Check every length or count a server sends against its bound before
  reading or reserving for it (R-SAFE-1).
- No secret in a log, a failure's message or an object's display form
  (R-SAFE-4).
- Integers of the protocol are read and written at their width,
  little-endian; exact numbers never pass through a float (R-NUM).
- Keep functions to 50 lines of code and 5 parameters, and files to 600
  lines (R-SIZE).
- No magic numbers or strings: protocol tags, kinds, capability, status
  and mechanism names are named constants, defined once (R-CONST).
- Every public item has a doc comment (R-DOC-1). Comments say why
  (R-DOC-2). No `TODO` comments or commented-out code (R-DOC-5).
- A silenced warning names its rule and says why (R-LINT-3).
- No dependency without a decision in RULES.md (R-DEP-1).
- Code of one platform is chosen by the platform and refused elsewhere
  with the driver's error (R-PLAT-2).
- Every public name begins `trl_`, every macro `TRL_`, and a type ends in
  no `_t` (D10, R-STYLE-2).
- Every allocation is checked, and memory not had is the driver's error
  (R-MEM-2).
- No variable-length arrays, and nothing of C17 that MSVC lacks (R-MEM-4,
  D3).
- No `assert`, `abort`, `exit`, `printf`, `strcpy` or the like in
  `src/`; `rules.source` refuses them (R-ERR-1, R-MEM-1).
- A silenced clang-tidy check is `NOLINTNEXTLINE(check)`, with a comment
  giving its reason (R-LINT-3).

## Tests

Every test cites what it verifies, by its doc comment, beginning `Verifies:`, beginning with a
tag (R-TRACE-1, D6):

- `[PROTOCOL <heading>]`, a section of 05-protocol.md, as
  `[PROTOCOL Frames]`;
- `[CLIENT <heading>]`, a section of 02-library.md or
  02-library-connections.md, as `[CLIENT Pipelines]`;
- `[DESIGN] D<n>`, a decision of RULES.md;
- `[TOOL] R-<id>`, a rule the checks tool holds.

The checks tool refuses a heading, decision or rule that is not there
(R-TRACE-2). Tests are deterministic, with no clock, randomness or
network beyond loopback (R-TEST-5). A shared fixture is written once
(R-TEST-7). A fixed defect gets a regression test that fails without the
fix (R-TEST-6). A live test runs its own server on its own port and data
directory, and stops it.

## Citing the spec

Check every citation against the document before writing it, by its
heading as printed. Never cite from memory. Code never cites TODO.md.

## Documents

A finished feature updates these in the same change:

- RULES.md, if it made a decision, including the row in §1's table;
- CONFORMANCE.md, each row it implements (R-TRACE-4), marked done only
  where its code exists and its tests pass;
- TODO.md, which only holds open work;
- [doc/](doc/README.md), for anything a program sees (R-DOC-7), every
  example run: each page keeps its `# Title` and its
  `[Contents](README.md)` line, links to this repository's files are
  relative (the docs site points those outside `doc/` at GitHub, at the
  same tag), links to Turborel's manual are GitHub addresses,
  `user/01-installing.md` keeps its name, and `doc/` holds no images;
- README.md, if what the driver is or needs changes.

No document gives timings or speedups; numbers go in a commit message.

## Commits

- Commit only when the maintainer asks, and never push without approval.
- Follow CONTRIBUTING.md's form, with one logical change per commit.
- Add no AI or agent attribution to a commit, a pull request, a file or
  the package's metadata. That means no `Co-Authored-By` for an agent,
  no "Generated with" and no `Signed-off-by`.
- Stage with an explicit path list, `git add -- <path> ...`. Never use
  `git add -A` or `git add .` on the whole tree.
- Never stage scratch files, or a live test's data directory.
- Code compiled for one platform only can't be verified locally on
  another. Say so when handing it back (R-PLAT-4).
