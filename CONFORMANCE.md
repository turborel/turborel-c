# Conformance

This file is the parity matrix: what turborel-c implements of turborel's
native protocol, version 1, and of turborel-client, row by row. The
driver is done when every row is done (TODO.md, M8).

The rows follow their documents in order: the protocol's spec,
`doc/client/05-protocol.md` in turborel's repository; then the client's
layers; then turborel-client's manual, `doc/client/02-library.md` and
`02-library-connections.md`. Where the spec and turborel-client's code
differ, the code is right, and the row says so. A reference
`[PROTOCOL heading]` is a heading of the spec, and `[CLIENT heading]` one
of the client's manual.

**Status** is one of: **not yet**; **codec only**, encoded and decoded
with its tests, but not yet sent or received; **done**, with where it is
and its tests.

[Frames](#frames) · [Values and fields](#values-and-fields) ·
[Data types](#data-types) · [Values](#values) ·
[Composite fields](#composite-fields) · [The tail](#the-tail) ·
[Opening a connection](#opening-a-connection) ·
[Capabilities](#capabilities) · [Requests](#requests) ·
[Replies](#replies) · [Status names](#status-names) ·
[Errors and notices](#errors-and-notices) · [Transport](#transport) ·
[The client library](#the-client-library) ·
[Settings, pipelines and pools](#settings-pipelines-and-pools)

---

## Frames

| Item | Reference | Status |
|---|---|---|
| The header: kind `u8`, length `u32` little-endian, five bytes | [PROTOCOL Frames] | codec only: `src/codec/frame.c`, `test/codec/frame_test.c` |
| A kind not known, a body cut short, or bytes left over fail the connection (-919, 08006) | [PROTOCOL Frames] | not yet |
| Replies of at most 16 MiB of body taken by default, and a smaller or larger limit set by the program | [PROTOCOL Frames]; [CLIENT Timeouts] | codec only: the limit checked as the header is read |
| Compressed frames `z`: the kind held, its body's length, a zlib stream; inflated to at most that length; none nested | [PROTOCOL Compressed frames] | not yet |
| A threshold of the client's own for compressing what it sends | [PROTOCOL Compressed frames] | not yet |

## Values and fields

| Item | Reference | Status |
|---|---|---|
| `u8`, `u32`, `u64`, little-endian | [PROTOCOL Values and fields] | not yet |
| `i128`, two's complement, little-endian | [PROTOCOL Values and fields] | not yet |
| flag: 0 or 1, any other byte failing the connection | [PROTOCOL Values and fields] | not yet |
| string: length `u32`, then UTF-8 | [PROTOCOL Values and fields] | not yet |
| bytes: length `u32`, then the bytes | [PROTOCOL Values and fields] | not yet |
| list and strings: count `u32`, then each item | [PROTOCOL Values and fields] | not yet |
| key: 16 bytes, no length | [PROTOCOL Values and fields] | not yet |
| A count larger than what follows fails as the body runs out, reserving nothing | [PROTOCOL Values and fields] | not yet |

## Data types

| Tag | Type | Reference | Status |
|---|---|---|---|
| 1 | CHARACTER, length and units | [PROTOCOL Data types] | not yet |
| 2 | exact numeric, precision and scale | [PROTOCOL Data types] | not yet |
| 3 | approximate numeric, precision | [PROTOCOL Data types] | not yet |
| 4 | a plug-in's type, its name and parameters; one the client lacks read by name | [PROTOCOL Data types] | not yet |
| 5 | CHARACTER VARYING, length and units | [PROTOCOL Data types] | not yet |
| 6 | BINARY, length | [PROTOCOL Data types] | not yet |
| 7 | BINARY VARYING, length | [PROTOCOL Data types] | not yet |
| 8 | DATE, precision | [PROTOCOL Data types] | not yet |
| 9 | TIME, precision | [PROTOCOL Data types] | not yet |
| 10 | TIME WITH TIME ZONE, precision | [PROTOCOL Data types] | not yet |
| 11 | TIMESTAMP, precision | [PROTOCOL Data types] | not yet |
| 12 | TIMESTAMP WITH TIME ZONE, precision | [PROTOCOL Data types] | not yet |
| 13 | INTERVAL, its fields 0 YEAR to 5 SECOND and precisions | [PROTOCOL Data types] | not yet |
| 14 | BOOLEAN | [PROTOCOL Data types] | not yet |
| 15 | DECFLOAT, precision | [PROTOCOL Data types] | not yet |
| 16 | array, maximum cardinality and element type | [PROTOCOL Data types] | not yet |
| 17 | row, each field's name and type | [PROTOCOL Data types] | not yet |
| 18 | CHARACTER LARGE OBJECT, length `u64` and units | [PROTOCOL Data types] | not yet |
| 19 | BINARY LARGE OBJECT, length `u64` and units | [PROTOCOL Data types] | not yet |
| 22 | multiset, element type | [PROTOCOL Data types] | not yet |
| 23 | structured type, its names and proper supertypes | [PROTOCOL Data types] | not yet |
| 24 | reference type, the referenced type and its scope | [PROTOCOL Data types] | not yet |
| 25 | JSON | [PROTOCOL Data types] | not yet |
| 26 | XML, its primary and secondary modifiers and XMLSCHEMA's names | [PROTOCOL Data types] | not yet |
| 27 | UUID, no parameters; the spec does not yet list it, turborel's RULES.md D184 and `protocol/src/fields.rs` do | `protocol/src/fields.rs` | not yet |

## Values

| Tag | Value | Reference | Status |
|---|---|---|---|
| 0 | null | [PROTOCOL Values] | not yet |
| 1 | character string, CHARACTER VARYING's too | [PROTOCOL Values] | not yet |
| 2 | exact number, units `i128` and scale `u32`, at the limits of both | [PROTOCOL Values] | not yet |
| 3 | approximate number, an IEEE 754 double's bits, NaN and -0.0 kept | [PROTOCOL Values] | not yet |
| 4 | a plug-in's value: its type's name, its text, its bytes | [PROTOCOL Values] | not yet |
| 6 | binary string | [PROTOCOL Values] | not yet |
| 8 | date, as a timestamp | [PROTOCOL Values] | not yet |
| 9 | time, as a timestamp | [PROTOCOL Values] | not yet |
| 11 | timestamp: zoned, precision, displacement -12:59 to +14:00, microseconds in UTC | [PROTOCOL Values] | not yet |
| 13 | interval: its qualifier, then months or microseconds | [PROTOCOL Values] | not yet |
| 14 | truth value; unknown is null | [PROTOCOL Values] | not yet |
| 15 | decimal floating point, coefficient and exponent | [PROTOCOL Values] | not yet |
| 16 | array, nested to any depth | [PROTOCOL Values] | not yet |
| 17 | row | [PROTOCOL Values] | not yet |
| 20 | large object, whole: kind and octets | [PROTOCOL Values] | not yet |
| 21 | large object, by locator: kind, locator, octets, characters | [PROTOCOL Values] | not yet |
| 22 | multiset, in no set order | [PROTOCOL Values] | not yet |
| 23 | structured value: its most specific type and attributes | [PROTOCOL Values] | not yet |
| 24 | reference, bytes | [PROTOCOL Values] | not yet |
| 25 | JSON, its stored form: turborel's RULES.md D89 binary tree (a version octet, then nodes of tags 0 to 11), which the spec does not yet define; decoded to JSON, and parameters encoded into it | `src/json/binary.rs` | not yet |
| 26 | XML, its serialization as CONTENT | [PROTOCOL Values] | not yet |
| 27 | UUID, `bytes` of a count of 16 then the octets; listed by turborel's RULES.md D184, not yet by the spec | `protocol/src/fields.rs` | not yet |
| | A large object sent whole, or as a locator the client made | [PROTOCOL Values] | not yet |

## Composite fields

| Item | Reference | Status |
|---|---|---|
| row: a list of values | [PROTOCOL Composite fields] | not yet |
| column: named flag, name, data type, not null flag | [PROTOCOL Composite fields] | not yet |
| parameter: varying flag, data type | [PROTOCOL Composite fields] | not yet |
| failure: SQLCODE, SQLSTATE, message, rule cited, position, active, tail 7 to 15 | [PROTOCOL Composite fields] | not yet |

## The tail

| Tag | Field | Reference | Status |
|---|---|---|---|
| | Tags in increasing order; one not taken, or not past the last, fails the connection | [PROTOCOL The tail] | not yet |
| 1 | attributes: at most 32, names 1 to 64 bytes, values at most 1024 | [PROTOCOL The tail] | not yet |
| 2 | multi | [PROTOCOL The tail] | not yet |
| 3 | cursor | [PROTOCOL The tail] | not yet |
| 4 | position | [PROTOCOL The tail] | not yet |
| 5 | more | [PROTOCOL The tail] | not yet |
| 6 | rows | [PROTOCOL The tail] | not yet |
| 7 | severity | [PROTOCOL The tail] | not yet |
| 8 | detail | [PROTOCOL The tail] | not yet |
| 9 | hint | [PROTOCOL The tail] | not yet |
| 10 | context | [PROTOCOL The tail] | not yet |
| 11 | schema | [PROTOCOL The tail] | not yet |
| 12 | table | [PROTOCOL The tail] | not yet |
| 13 | column | [PROTOCOL The tail] | not yet |
| 14 | data type | [PROTOCOL The tail] | not yet |
| 15 | constraint | [PROTOCOL The tail] | not yet |
| 16 | generated | [PROTOCOL The tail] | not yet |
| 17 | conditions, each a failure as bytes | [PROTOCOL The tail] | not yet |
| 18 | time: total, queued, locks | [PROTOCOL The tail] | not yet |

## Opening a connection

| Item | Reference | Status |
|---|---|---|
| Hello, the Challenge and Answer steps, then Status and Ready | [PROTOCOL Opening a connection] | not yet |
| Ready's authorization, cancel key, capabilities granted and server product | [PROTOCOL Opening a connection] | not yet |
| A refusal (08004, 28000) told as the connection's failure | [PROTOCOL Opening a connection] | not yet |
| Mechanism `trust` | [PROTOCOL Mechanisms] | not yet |
| Mechanism `password` | [PROTOCOL Mechanisms] | not yet |
| Mechanism `certificate` | [PROTOCOL Mechanisms] | not yet |
| Mechanism `scram-sha-256`, RFC 5802 and RFC 7677 | [PROTOCOL Mechanisms] | not yet |
| Mechanism `scram-sha-256-plus`, by `tls-exporter` on TLS 1.3 and `tls-server-end-point` on TLS 1.2 | [PROTOCOL Mechanisms] | not yet |

## Capabilities

| Capability | Reference | Status |
|---|---|---|
| `notices` | [PROTOCOL Capabilities] | not yet |
| `status` | [PROTOCOL Capabilities] | not yet |
| `diagnostics` | [PROTOCOL Capabilities] | not yet |
| `describe` | [PROTOCOL Capabilities] | not yet |
| `cursors` | [PROTOCOL Capabilities] | not yet |
| `ping` | [PROTOCOL Capabilities] | not yet |
| `reset` | [PROTOCOL Capabilities] | not yet |
| `change-user` | [PROTOCOL Capabilities] | not yet |
| `query-attributes` | [PROTOCOL Capabilities] | not yet |
| `connection-attributes` | [PROTOCOL Capabilities] | not yet |
| `multi-statement` | [PROTOCOL Capabilities] | not yet |
| `compression` | [PROTOCOL Capabilities] | not yet |
| `commit-position` | [PROTOCOL Capabilities] | not yet |
| `copy` | [PROTOCOL Capabilities] | not yet |
| `row-counts` | [PROTOCOL Capabilities] | not yet |
| `generated-values` | [PROTOCOL Capabilities] | not yet |
| `fetch-ends` | [PROTOCOL Capabilities] | not yet |
| `conditions` | [PROTOCOL Capabilities] | not yet |
| A request or tail field of a capability not granted is never sent | [PROTOCOL Capabilities] | not yet |

## Requests

| Request | Reference | Status |
|---|---|---|
| Hello `H` | [PROTOCOL Hello 'H'] | not yet |
| Answer `A` | [PROTOCOL Answer 'A'] | not yet |
| Query `Q`, with `ends`, attributes and multi | [PROTOCOL Query 'Q'] | not yet |
| Prepare `P` | [PROTOCOL Prepare 'P'] | not yet |
| Execute `E`, with `rows`, `ends`, attributes and cursor | [PROTOCOL Execute 'E'] | not yet |
| Fetch `F` | [PROTOCOL Fetch 'F'] | not yet |
| Close `C` | [PROTOCOL Close 'C'] | not yet |
| Deallocate `D` | [PROTOCOL Deallocate 'D'] | not yet |
| Lob `L`: read, create, append, free, hold | [PROTOCOL Lob 'L'] | not yet |
| Cancel `Z`, on a connection of its own | [PROTOCOL Cancel 'Z'] | not yet |
| Terminate `X` | [PROTOCOL Terminate 'X'] | not yet |
| Describe `Y`, of a statement or a cursor | [PROTOCOL Describe 'Y'] | not yet |
| Ping `G` | [PROTOCOL Ping 'G'] | not yet |
| Reset `W` | [PROTOCOL Reset 'W'] | not yet |
| ChangeUser `U` | [PROTOCOL ChangeUser 'U'] | not yet |
| CopyIn `I` | [PROTOCOL CopyIn 'I'] | not yet |
| CopyData `J` | [PROTOCOL CopyData 'J'] | not yet |
| CopyEnd `N`, with abandon | [PROTOCOL CopyEnd 'N'] | not yet |
| Requests sent before the last is answered, answered in order | [PROTOCOL Requests] | not yet |

## Replies

| Reply | Reference | Status |
|---|---|---|
| Challenge `C` | [PROTOCOL Challenge 'C'] | not yet |
| Ready `R` | [PROTOCOL Ready 'R'] | not yet |
| Prepared `P` | [PROTOCOL Prepared 'P'] | not yet |
| Description `D` | [PROTOCOL Description 'D'] | not yet |
| Rows `B` | [PROTOCOL Rows 'B'] | not yet |
| Suspended `S` | [PROTOCOL Suspended 'S'] | not yet |
| Complete `K` | [PROTOCOL Complete 'K'] | not yet |
| Error `E`, and the errors that end the connection | [PROTOCOL Error 'E'] | not yet |
| Lob `L`: data or locator | [PROTOCOL Lob 'L'] | not yet |
| Notice `N` | [PROTOCOL Notice 'N'] | not yet |
| Status `T` | [PROTOCOL Status 'T'] | not yet |
| Pong `G` | [PROTOCOL Pong 'G'] | not yet |
| The fixed order of a statement's replies | [PROTOCOL Conversations] | not yet |

## Status names

| Name | Reference | Status |
|---|---|---|
| AUTHORIZATION | [PROTOCOL Status 'T'] | not yet |
| SCHEMA | [PROTOCOL Status 'T'] | not yet |
| READ_ONLY, YES or NO | [PROTOCOL Status 'T'] | not yet |
| SERVER_VERSION | [PROTOCOL Status 'T'] | not yet |
| SERVER_READ_ONLY, YES or NO, how a replica is told from a primary; sent by the server, not yet listed by the spec | `server/src/native/status.rs` | not yet |
| CATALOG; sent by the server, not yet listed by the spec | `server/src/native/status.rs` | not yet |
| OWNER, YES or NO; sent by the server, not yet listed by the spec | `server/src/native/status.rs` | not yet |
| Every setting a session may set for itself | [PROTOCOL Status 'T'] | not yet |

## Errors and notices

| Item | Reference | Status |
|---|---|---|
| Severity ERROR, FATAL, WARNING or NOTICE | [PROTOCOL Errors and notices] | not yet |
| A statement's conditions told one way: in Complete with `conditions`, else as Notices | [PROTOCOL Errors and notices] | not yet |
| SQLSTATE 01T01, a transaction rolled back by Reset or ChangeUser | [PROTOCOL Errors and notices] | not yet |
| SQLSTATE TBR01, a replica behind REPLICATION.READ_AFTER | [PROTOCOL Errors and notices] | not yet |
| The worked example's bytes, each frame | [PROTOCOL A worked example] | codec only: the headers, `test/codec/frame_test.c` |

## Transport

| Item | Reference | Status |
|---|---|---|
| TCP, port 7422 unless told another | [PROTOCOL The Native Protocol] | not yet |
| Unix socket | [CLIENT Connecting] | not yet |
| Windows named pipe, `\\.\pipe\turborel` | [CLIENT Connecting] | not yet |
| TLS begun on the same port | [PROTOCOL The Native Protocol] | not yet |
| Connect timeout 30 s for each step of connecting; read timeout unlimited by default | [CLIENT Timeouts] | not yet |

## The client library

| Item | Reference | Status |
|---|---|---|
| Connect over TCP, a Unix socket or a named pipe, or to a target, over TLS or not | [CLIENT Connecting] | not yet |
| SCRAM-SHA-256 of a password; SCRAM-SHA-256-PLUS over TLS; in the clear only where asked | [CLIENT Connecting] | not yet |
| Password sent whole, only over TLS, a Unix socket or a named pipe | [CLIENT Connecting] | not yet |
| Certificate: a TLS that trusts given authorities and presents a chain and key | [CLIENT Connecting] | not yet |
| Trust | [CLIENT Connecting] | not yet |
| Password and SCRAM refused over plain TCP before connecting (28000) | [CLIENT Connecting] | not yet |
| The authorities trusted: a file's, the platform's, or both | [CLIENT Connecting] | not yet |
| Terminate, its commit's failure given, within 60 s | [CLIENT Connecting] | not yet |
| Timeouts of connect and read, set per connection; a wait run out loses the connection (08006) | [CLIENT Timeouts] | not yet |
| A reply limit the program sets | [CLIENT Timeouts] | not yet |
| Query, giving rows and columns, or a completion | [CLIENT Running statements] | not yet |
| A failure's SQLCODE, SQLSTATE, message, rule and position | [CLIENT Running statements] | not yet |
| Rows given to a sink a batch at a time | [CLIENT Running statements] | not yet |
| Every value kind, plug-in types too | [CLIENT Running statements] | not yet |
| Whether a transaction is active | [CLIENT Running statements] | not yet |
| Cancel from another thread, giving up after 10 s, or a limit of its own | [CLIENT Running statements] | not yet |
| Autocommit, each statement its own transaction within one request | [CLIENT Running statements] | not yet |
| Prepare, execute, parameters' types and columns | [CLIENT Prepared statements] | not yet |
| Open a cursor, fetch, close, deallocate | [CLIENT Prepared statements] | not yet |
| Large objects whole up to the server's inline size, else by locator | [CLIENT Large objects] | not yet |
| Read a large object by range, by octets or characters, 256 KiB a chunk | [CLIENT Large objects] | not yet |
| A reader of a whole large object | [CLIENT Large objects] | not yet |
| Free and hold a locator | [CLIENT Large objects] | not yet |
| Write a large object a piece at a time, and pass it as a parameter | [CLIENT Large objects] | not yet |
| XML values, their text, and parameters of their text | [CLIENT XML values] | not yet |
| UUIDs, their octets and text, and parameters of either | [CLIENT UUIDs] | not yet |
| Import from a source of records, in bounded memory | [CLIENT Importing] | not yet |
| CSV and tab-separated sources, with header, delimiter and null text | [CLIENT Importing] | not yet |
| Import's batch (1000 by default), skip, and its failure's place and counts | [CLIENT Importing] | not yet |
| Every capability asked for but compression | [CLIENT What a connection asks for] | not yet |
| An offer of the program's own: none, compressed, attributes | [CLIENT What a connection asks for] | not yet |
| What was granted; a call needing what was not fails before sending | [CLIENT What a connection asks for] | not yet |
| A setting's value as last told, and all of them | [CLIENT What the server tells] | not yet |
| A notice handler, else the last 64 notices kept | [CLIENT What the server tells] | not yet |
| The last statement's conditions | [CLIENT What the server tells] | not yet |
| Rows affected, generated values, server time, commit position | [CLIENT What the server tells] | not yet |
| A failure's diagnostics | [CLIENT What the server tells] | not yet |
| A cursor under autocommit ending its transaction as it ends | [CLIENT What the server tells] | not yet |
| Ping | [CLIENT Controlling the session] | not yet |
| Reset | [CLIENT Controlling the session] | not yet |
| Change user | [CLIENT Controlling the session] | not yet |
| Query attributes on every later statement | [CLIENT Controlling the session] | not yet |
| Several statements in one request, each outcome and the failure that stopped them | [CLIENT Controlling the session] | not yet |
| Describe a statement again; numbered cursors; describe a cursor | [CLIENT Controlling the session] | not yet |
| Copy in: send rows, finish, abandoned if dropped | [CLIENT Copying and exporting] | not yet |
| Copy the rows an iterator gives | [CLIENT Copying and exporting] | not yet |
| Import by copy where granted and no column is a large object | [CLIENT Copying and exporting] | not yet |
| Export as CSV, or as the protocol's frames, and read the native form back | [CLIENT Copying and exporting] | not yet |
| The SQL-agent: CONNECT, SET CONNECTION, DISCONNECT, by a connector the program gives | [CLIENT The SQL-agent] | not yet |

## Settings, pipelines and pools

| Item | Reference | Status |
|---|---|---|
| Connect by settings | [CLIENT Connecting by settings] | not yet |
| A mechanism chosen from the settings: SCRAM or password, certificate, trust | [CLIENT Connecting by settings] | not yet |
| Setting `host`, several, with ports, IPv6 and socket paths | [CLIENT Where settings come from] | not yet |
| Setting `port`, one for all or one each | [CLIENT Where settings come from] | not yet |
| Settings `user` and `password` | [CLIENT Where settings come from] | not yet |
| Settings `tls_mode` and `channel_binding` | [CLIENT Where settings come from] | not yet |
| Setting `application_name`, sent as the attribute `application` | [CLIENT Where settings come from] | not yet |
| Setting `connect_timeout` | [CLIENT Where settings come from] | not yet |
| Settings `service`, `service_file` and `passfile` | [CLIENT Where settings come from] | not yet |
| Settings `target` and `load_balance` | [CLIENT Where settings come from] | not yet |
| Settings `tls_roots`, `tls_system_roots`, `tls_certificate`, `tls_key` and `server_name` | [CLIENT Where settings come from] | not yet |
| The layers: the program's, the service file's, the environment's, the defaults | [CLIENT Where settings come from] | not yet |
| The TURBOREL_* variables, an empty one giving nothing; an environment the program gives | [CLIENT Where settings come from] | not yet |
| Settings refused with 08001 | [CLIENT Where settings come from] | not yet |
| The connection URI, percent-encoded, its refusals | [CLIENT The connection URI] | not yet |
| The service file, its sections, comments and refusals | [CLIENT The service file] | not yet |
| The password file, `*` and `\`, the first match, a regular file only | [CLIENT The password file] | not yet |
| Several servers, tried in order or at random, each address in turn, the failure naming each | [CLIENT Several servers] | not yet |
| `target` read-write or read-only, by SERVER_READ_ONLY, else INFORMATION_SCHEMA.REPLICAS | [CLIENT Several servers] | not yet |
| The role a connection's session sees | [CLIENT Several servers] | not yet |
| TLS modes `disable`, `prefer`, `require`, `verify-ca`, `verify-full`; `prefer`'s retry over TCP | [CLIENT TLS by mode] | not yet |
| Channel binding `disable`, `prefer`, `require` | [CLIENT TLS by mode] | not yet |
| Pipelines, one outcome a request, in order | [CLIENT Pipelines] | not yet |
| A 64 KiB window of requests at a time | [CLIENT Pipelines] | not yet |
| Skip on error, by savepoints | [CLIENT Pipelines] | not yet |
| A pipeline's failure as a whole (08006) | [CLIENT Pipelines] | not yet |
| A pool shared by threads: get, idle, made, or waited for | [CLIENT Pools] | not yet |
| PoolConfig: max 10, max_idle 10, idle_timeout 10 minutes, max_lifetime 1 hour, wait 30 s, check on, reset none | [CLIENT Pools] | not yet |
| A connection returned reset by Reset, checked by Ping; ROLLBACK and a pipeline where neither is granted | [CLIENT Pools] | not yet |
| A pool of settings or of any source; no thread of its own | [CLIENT Pools] | not yet |
| The SCRAM key cache: 64 entries by salt, iterations and a keyed tag; dropped on a failed login | [CLIENT The SCRAM key cache] | not yet |
| The key cache shared by every connection of a set of settings | [CLIENT The SCRAM key cache] | not yet |
