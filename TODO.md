# TODO

Open work, by milestone. Each item says what is missing and why it
matters. How the driver is built is in [RULES.md](RULES.md), and what it
implements of the protocol and of turborel-client is in
[CONFORMANCE.md](CONFORMANCE.md), row by row. Finished work leaves this
file, and git keeps its history.

Code never cites this file (R-DOC-5).

[M1 Codec](#m1-codec) · [M2 Connect](#m2-connect) ·
[M3 Statements](#m3-statements) · [M4 Large objects and bulk](#m4-large-objects-and-bulk) ·
[M5 Settings](#m5-settings) · [M6 Throughput](#m6-throughput) ·
[M7 Standard API](#m7-standard-api) · [M8 Parity audit](#m8-parity-audit) ·
[ODBC driver](#odbc-driver)

---

## M1 Codec

The protocol's whole encoding, without I/O, so every later layer stands on
tested bytes.

- Fields: integers, `i128` as two 64-bit halves (D3), flags, strings,
  bytes, lists, keys, read with every count bounded by the bytes left.
- The tail: tags 1 to 18 in increasing order, each only where granted.
- Every request and reply of protocol 1, each message kind a named
  constant.
- The 27 data types and every value tag, UUID's tag 27 among them, at
  their edges: `i128` limits, scales, NaN and -0.0, DECFLOAT, each
  interval qualifier, displacements -12:59 to +14:00, nested arrays, rows,
  multisets and structured values, locators, XML's modifiers.
- JSON values (tag 25) as RULES.md D89's binary tree in turborel, decoded
  to the C API's JSON value and text, and parameters encoded into it.
- An exact number's type in the C API: two 64-bit halves and a scale,
  with text conversions.
- Compressed frames, `z`, through zlib, bounded by the declared length;
  zlib becomes required for them (D7).
- Malformed frames that must fail the connection (08006), each a test.
- Every codec function in a fuzz target.

## M2 Connect

Reaching a server, so statements have a session to run in.

- TCP on port 7422, a Unix socket, and a Windows named pipe
  `\\.\pipe\turborel`, each where its platform has it (R-PLAT-2).
- TLS begun on the same port, through OpenSSL 3.5 or AWS-LC (D7).
- The mechanisms `trust`, `password`, `certificate`, `scram-sha-256` and
  `scram-sha-256-plus`, by `tls-exporter` on TLS 1.3 and
  `tls-server-end-point` on 1.2; secrets refused over plain TCP (28000).
- The SCRAM key cache: 64 entries, keyed by salt, iterations and a keyed
  tag of the password.
- Hello asking for every capability but compression; Ready; Status.
- Timeouts: connect 30 s, read unlimited by default; terminate within
  60 s; cancel by key on its own connection within 10 s.
- The connection handle and the failure object of D4, in the C API.
- Live tests against `ghcr.io/turborel/turborel:nightly` in CI, set up in
  `test/server/` with test certificates, SCRAM users and TRUST_LOCAL, on
  port 17438 locally; the image is Linux's alone, so macOS and Windows
  either build `turboreld` from a pinned turborel commit or run the codec
  tests alone, as the owner decides.

## M3 Statements

Running SQL, the driver's purpose.

- Query and streamed query; prepare, execute, fetch, close, deallocate;
  numbered cursors; describe.
- Errors with diagnostics, notices (the last 64 kept, or a handler),
  conditions; `in_transaction`; autocommit through `ends`; `fetch-ends`.
- Completions: rows affected, generated values, server time, commit
  position.
- Multi-statement queries, query attributes, ping, reset, change-user.
- The result handle and its accessors, in the C API.

## M4 Large objects and bulk

- Locators; reads in 256 KiB chunks, by octets or characters; create,
  append, free, hold; a stream reader and writer.
- CopyIn, CopyData, CopyEnd and abandon; CSV and tab-separated import;
  CSV and native export.

## M5 Settings

- The `turborel://` URI, options, the TURBOREL_* variables, the service
  file and the password file, as turborel-client reads them, tested by
  the cases of its `settings/tests.rs`.
- Several hosts, `load_balance`, the `target` role by SERVER_READ_ONLY;
  `tls_mode`; `channel_binding`; `application_name`.

## M6 Throughput

- Pipelines with a 64 KiB window, and skip-on-error by savepoints.
- Pools with PoolConfig's defaults, a Ping check and a Reset on return,
  safe to share between threads (D6).
- The SQL-agent: CONNECT, SET CONNECTION and DISCONNECT.

## M7 Standard API

- The libpq-style C API over every layer above, complete (D2).
- The manual, `doc/`, in full, every example run.

## M8 Parity audit

- Every row of CONFORMANCE.md checked against turborel-client and the
  spec, the coverage floors held, and this file empty. Then the driver is
  done.

## ODBC driver

Starts once the C API reaches M4, and is built beside M5 to M8 (D2).

- `libturborelodbc`, an ODBC 3.x driver in its own CMake target, written
  from Microsoft's *ODBC Programmer's Reference*, over the C API.
- Its registration on Windows, and with unixODBC and iODBC.
- Its tests, and its manual pages.
