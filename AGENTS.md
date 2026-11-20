# Guide for coding agents

How to work on turborel-c as a coding agent, with any tool. If this file
disagrees with [RULES.md](RULES.md) or [CONTRIBUTING.md](CONTRIBUTING.md),
follow those.

turborel-c is the C driver for turborel: `libturborel`, a C17 library
that speaks turborel's native protocol itself, with a libpq-style C API
and, from M4, an ODBC 3.x driver over it. See [README.md](README.md).

## Before you change anything

Read RULES.md §3 (code), §4 (tests), §7 (process) and the §1 decisions for
your area. Read CONTRIBUTING.md for how a change is made and the extra
checks for changes that touch the wire. Read the protocol's spec,
turborel's `doc/client/05-protocol.md`, and turborel-client's code
(`client/src/`, `protocol/src/`) for what you build; where they differ,
the code is right.

## Build and check

Every change must pass `cmake --workflow --preset verify` before it is
handed back. It needs the clang-format and clang-tidy that
`.github/lint-requirements.txt` pins on `PATH` (D12).

```
cmake --workflow --preset verify        # clang-tidy, formatting, the rules, every test
cmake --workflow --preset debug         # build and test, warnings as errors
cmake --workflow --preset release       # optimized, and the installed package tested
cmake --workflow --preset asan-ubsan    # the tests under the sanitizers
cmake --workflow --preset coverage      # coverage report and floor check
cmake --workflow --preset fuzz          # libFuzzer targets; LLVM's Clang, not Apple's
ctest --preset debug -R codec.frame     # one test executable
cmake -DSOURCE_DIR=. -P cmake/check-source.cmake    # the source rules alone
clang-format -i FILE                    # format a file
```

A workflow stops at the first failure. CI runs the tests on Linux, macOS
and Windows, and lint, sanitizers, coverage and fuzzing on Linux.
Coverage floors are in `cmake/CoverageFloors.cmake` and only rise
(R-COV-2).

Project automation is CMake script in `cmake/`, never shell scripts (D13).

## Where things are

| Path | What |
|---|---|
| `include/turborel/turborel.h` | the public header, the whole interface |
| `src/` | the library; internal headers live beside their sources |
| `src/codec/` | the protocol's encoding, without I/O |
| `test/harness.h` | the one test harness |
| `test/<area>/*_test.c` | one test executable per area, `area.thing` in CTest |
| `test/package/` | a program outside the tree, for the installed package's test |
| `fuzz/` | libFuzzer targets of the codec |
| `cmake/` | CMake modules and the check scripts |
| `doc/` | the manual |

The library's sources make one object library, `turborel_objects`; the
library and the tests are both made of it (D8).

## Rules that are broken most often

Every compiler warning and clang-tidy check is an error. RULES.md §3 and
§4 have the full list.

- No `assert`, `abort`, `exit`, `printf` or `longjmp` in `src/`
  (R-ERR-1); `rules.source` refuses them.
- Every failure is a status code with a failure object of SQLCODE and
  SQLSTATE (D4). A null pointer passed in is refused, not dereferenced.
- Every public name is `trl_`, every macro `TRL_` (R-STYLE-2); types have
  no `_t` (D5).
- No implicit conversion that loses a value or a sign (`-Wconversion`,
  R-NUM-1); wire integers are read byte by byte, little-endian (R-NUM-4).
- Every allocation is checked (R-MEM-2); a count from the server never
  reserves memory before its bytes arrive (R-MEM-3).
- No variable-length arrays, and nothing of C17 that MSVC lacks (R-SAFE-1).
- No global or static mutable state (D6, R-SAFE-2).
- Functions at most 50 lines and 5 parameters; files at most 600 lines
  (R-SIZE).
- No magic numbers or strings; protocol tags are named constants
  (R-CONST).
- Every public item has a Doxygen doc comment, saying what the caller owns
  after it (R-DOC-1, D5). Every file opens with `/** @file ... */`
  (R-DOC-6).
- Comments say why (R-DOC-2). No `TODO` comments or commented-out code
  (R-DOC-5).
- A silenced check names itself and its reason: `NOLINTNEXTLINE(check)`
  with a comment (R-LINT-3).
- No new dependency without a decision in RULES.md (R-DEP-1).

## Tests

Every test case has a doc comment beginning `Verifies:` and one of the
citations `rules.source` accepts (R-TRACE-1):

- `[PROTOCOL <heading>]`, a heading of `05-protocol.md`, as
  `[PROTOCOL Frames]`;
- `[CLIENT <heading>]`, a heading of `02-library.md` or
  `02-library-connections.md`, as `[CLIENT Pipelines]`;
- `[DESIGN] D<n>`, a decision of RULES.md.

Tests are deterministic, with no clock, randomness or network beyond
loopback (R-TEST-4). Every test is written on `test/harness.h` (R-TEST-6).
A fixed defect gets a regression test (R-TEST-5). A new test executable is
one line in `test/CMakeLists.txt`, `trl_add_test(area.thing area/thing_test.c)`.

## Citing the spec

Check every citation against the document's headings before writing it.
Never cite from memory. `rules.source` holds the headings it accepts, as
the documents have them; a heading added to the spec is added there too.
Committed documents never say whether a standard's text is at hand.

Code never cites TODO.md, and `rules.source` refuses it (R-DOC-5).

## Documents

A finished feature updates these in the same change:

- RULES.md, if it made a decision, including the row in §1's table
- CONFORMANCE.md, for a row implemented or changed (R-TRACE-3)
- TODO.md, which only holds open work
- [doc/](doc/README.md), for anything a program sees (R-DOC-7)
- README.md's "What it has", for a major feature

Every claim in a document matches the code, and every example is run, its
real output pasted. No timings or benchmark figures in documents.

## Commits

- Commit only when the maintainer asks, and never push without approval.
- Follow CONTRIBUTING.md's form, with one logical change per commit.
- Add no AI or agent attribution to a commit or pull request. That means
  no `Co-Authored-By` for an agent, no "Generated with" and no
  `Signed-off-by`.
- Stage with an explicit path list, `git add -- <path> ...`. Never use
  `git add -A` or `git add .` on the whole tree.
- Never stage `LICENSE` or scratch files.
- Code compiled for one platform only can't be verified locally on another.
  Say so when handing it back.
