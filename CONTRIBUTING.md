# Contributing to turborel-c

turborel-c carries the data of programs that depend on it, and the
secrets that open a database. So a change is judged first on whether it
keeps values exact, secrets private and the program it runs in safe, and
then on what it adds. The process follows the Linux kernel's, adapted to
pull requests and to a database driver.

[Before you start](#before-you-start) · [Setting up](#setting-up) ·
[Making a change](#making-a-change) ·
[Changes that touch the wire](#changes-that-touch-the-wire) ·
[Commit messages](#commit-messages) · [Pull requests](#pull-requests) ·
[Review](#review) · [Reporting issues](#reporting-issues) ·
[Security](#security) · [License](#license)

---

## Before you start

Read [RULES.md](RULES.md) §3 (code), §4 (tests), §7 (process) and the
decisions for your area. Every rule applies to every change, and
`cmake --workflow --preset verify` checks those a tool can.
[CONFORMANCE.md](CONFORMANCE.md) says what the driver implements of the
protocol and of turborel-client, and [TODO.md](TODO.md) lists open work.

Open an issue first for anything larger than a fix. That covers new
features, changes to the C API, changes to a decision in RULES.md, and
new dependencies (R-DEP-1). Agreeing on the design first saves everyone a
rewrite.

The protocol starts from its spec, turborel's
[`doc/client/05-protocol.md`](https://github.com/turborel/turborel/blob/main/doc/client/05-protocol.md),
and from turborel-client, whose code prevails where the two differ. Cite
the spec by its headings, as `[PROTOCOL Frames]`, and turborel-client's
manual as `[CLIENT Pipelines]`. Never cite from memory.

## Setting up

You need a C17 compiler (GCC, Clang or MSVC), CMake 3.25 or later, and,
for the `verify` workflow, the clang-format and clang-tidy versions
`.github/lint-requirements.txt` pins (D12), in a virtual environment of
their own:

```
python3 -m venv .venv-lint
.venv-lint/bin/pip install -r .github/lint-requirements.txt
export PATH="$PWD/.venv-lint/bin:$PATH"     # on Windows: .venv-lint\Scripts
git config core.hooksPath .githooks         # run verify before each commit
cmake --workflow --preset verify            # every check a change must pass
```

The other workflows are each one command:

```
cmake --workflow --preset debug         # build and test, warnings as errors
cmake --workflow --preset release       # the same optimized, and the installed package tested
cmake --workflow --preset asan-ubsan    # the tests under AddressSanitizer and UBSan
cmake --workflow --preset coverage      # Clang's coverage, held to its floors
cmake --workflow --preset fuzz          # the libFuzzer targets (LLVM's Clang)
ctest --preset debug -R codec.frame     # one test executable
build/debug/test/codec.frame needs_five_bytes   # one case of it
```

CI runs the tests on Linux (GCC and Clang), macOS (Clang) and Windows
(MSVC), and clang-tidy, formatting, the sanitizers, coverage and fuzzing
on Linux. A change that passes on one platform and fails on another isn't
done.

## Making a change

Keep one logical change per commit, and make each commit pass `verify` on
its own, because anyone bisecting may land on it. Move code in one commit
and change it in another. Don't mix a reformat or rename with a change of
behaviour.

Tests come with the change, at the lowest level that shows it
(R-TEST-1): the codec's bytes before a live server. Cover normal cases,
boundaries, and robustness cases with the exact status, SQLCODE and
SQLSTATE expected (R-TEST-2). Each case's doc comment starts with
`Verifies:` and its citation (R-TRACE-1). A fixed defect gets a test that
fails without the fix (R-TEST-5).

Documents change in the same commit. Update CONFORMANCE.md for a row
implemented or changed (R-TRACE-3), RULES.md for a decision made or
changed, TODO.md for open work done or found, and `doc/` for anything a
program sees (R-DOC-7). Code never cites TODO.md.

Some rules deserve a reminder.

- No `assert`, `abort` or `exit`, and no printing, in the library
  (R-ERR-1); every failure is a status with its failure object (D4).
- Every allocation is checked (R-MEM-2), and nothing a server sends
  makes the library reserve memory before the bytes arrive (R-MEM-3).
- No dependency without a decision in RULES.md (R-DEP-1).
- Coverage floors only rise (R-COV-2).

## Changes that touch the wire

These need more than passing tests. Say in the commit message what was
done for each that applies.

For the codec, test every new message or value at its edges, and its
malformed forms, which must fail the connection (SQLCODE -919, SQLSTATE
08006), and add it to a fuzz target.

For authentication and TLS, say what an attacker on the network, or a
server of their own, could learn or change, and show it refused.

For a capability, say what a server that does not grant it sees, and
that a client not granted it never sends what it adds.

## Commit messages

The form is Linux's.

```
area: summary in the imperative, at most 75 characters

The problem: what goes wrong, or is missing, and who it affects. A
wrong value, a secret disclosed, a crash or a refusal is said first,
with the call that shows it.

Why this change, and how it solves the problem. Mention the choices
made and the alternatives rejected, the rules and headings applied
(RULES.md D4, R-MEM-3, [PROTOCOL Frames]), and what was measured,
with numbers.

Fixes: 54a4f0239f2e ("codec: refuse a frame longer than the limit")
Closes: <link to the issue>
Reported-by: A Reporter <reporter@example.org>
```

The subject is `area: summary`, at most 75 characters, in the imperative
("refuse a frame...", not "refused"), with no full stop. It says what
changes, isn't a file name, and isn't reused within a series.

The body is wrapped at 75 columns and written for someone reading `git log`
years later with no context. Say what the change does in plain prose, since
the diff shows how. Don't write "as discussed" or "v2".

Pick the area where most of the change is.

| Area | Covers |
|---|---|
| `codec` | `src/codec/`: frames, fields, the tail, types and values, JSON, compression |
| `transport` | TCP, Unix sockets, named pipes, timeouts |
| `auth` | the mechanisms, SCRAM and its key cache |
| `tls` | TLS, its modes, channel binding |
| `session` | Hello, Ready, Status, notices, cancel, terminate, ping, reset, change-user |
| `statements` | queries, prepared statements, cursors, describe, completions |
| `lobs` | large objects, locators and streams |
| `copy` | copy, import and export |
| `settings` | the URI, options, variables, the service and password files, several hosts |
| `pipeline` | pipelines |
| `pool` | pools |
| `api` | `include/turborel/turborel.h` and what it declares |
| `odbc` | the ODBC driver |
| `test` | the harness and tests that change alone |
| `doc` | `doc/`, README.md, RULES.md, CONFORMANCE.md, TODO.md, AGENTS.md, CLAUDE.md, this file |
| `ci` | `.github/`, `.githooks/` |
| `build` | CMakeLists.txt, CMakePresets.json, `cmake/`, `.clang-format`, `.clang-tidy`, `fuzz/` |
| `treewide` | a mechanical change across many areas |

Trailers go at the end, one per line, never wrapped.

| Trailer | Meaning |
|---|---|
| `Fixes: <12+ hex digits> ("<subject>")` | the commit that introduced the defect fixed |
| `Closes: <link>` | the issue this commit resolves |
| `Link: <link>` | background, such as a discussion or an issue fixed only in part |
| `Reported-by:` | who reported the defect fixed, with `Closes:` |
| `Suggested-by:` | who suggested the idea |
| `Co-authored-by:` | a co-author, as GitHub credits one |
| `Reviewed-by:` | a reviewer who checked the change and is satisfied with it |
| `Acked-by:` | a maintainer of the area who approves it without a full review |
| `Tested-by:` | someone who tested it, saying where if that matters |

`Reported-by:` and `Suggested-by:` may be added without asking. The others
name someone who said so on the record. `git config core.abbrev 12` gives
12-digit hashes, and `git log -1 --pretty='Fixes: %h ("%s")' <commit>` writes
the `Fixes:` line.

No trailer or line credits an AI or a coding agent: no `Co-authored-by:`
for one, no "Generated with", and no `Signed-off-by:`, which this project
does not use. Whoever submits a change answers for it.

Stage a commit by an explicit list of its paths, as
`git add -- <path> ...`, never `git add -A` or `git add .` on the whole
tree, so nothing unintended goes in.

## Pull requests

A pull request is a patch series, and its description is the cover letter.
The template asks for what and why, the series commit by commit, testing,
compatibility and performance. Each commit message still stands alone.

Base it on `main` and keep it rebased, without merging `main` into it. When
revising, rewrite the commits so the series is again what should be merged,
with no "fix review comments" commits. Force-push, and say in a comment what
changed since the last version.

Trailers given in review go into the commits they cover. Drop them from a
commit that later changes substantially, and say so.

Keep a series to about 15 commits. Split a larger one into series that each
stand alone.

## Review

Reviewers check each commit against RULES.md §7's checklist, and that the
manual is updated (R-DOC-7). For
[changes that touch the wire](#changes-that-touch-the-wire), they also
check that the malformed forms, the attacker's view and the capability
checks were done. Review comments are about the change, never the person.
A reviewer who is satisfied gives `Reviewed-by:`.

## Reporting issues

Use the issue forms, **Bug**, **Conformance**, **Performance** or
**Feature**. Try the latest `main` first, and add to an existing issue that
matches rather than opening another.

Reproduce with the smallest program that shows it, with the server's
schema and data it needs, and the settings it connects with (never a real
password).

State the impact first. Put it in the title's first word when it is severe.

| Tag | Meaning |
|---|---|
| `[WRONG VALUE]` | a value read or sent differs from what the server holds or was given |
| `[CRASH]` | a crash, an abort or a hang of the program |
| `[REGRESSION]` | worked in an earlier commit (give the last good commit and, if you can, the first bad one) |

Give the commit (`git rev-parse --short=12 HEAD`), the platform and
architecture, the compiler and its version, the TLS library and its
version, and the turborel server's release or commit.

## Security

Don't open a public issue for a vulnerability. Report it privately, as
[SECURITY.md](SECURITY.md) says.

## License

turborel-c is under the BSD-2-Clause Plus Patent License in
[LICENSE](LICENSE), and a contribution is made under that license.
Contributors keep their copyright and own their changes. Whoever
submits a change answers for all of it, however it was written.
