# Appendix A: Types

[Contents](README.md)

This appendix maps every data type and value tag of the native
protocol, 1 to 27, to the C type the driver gives and takes, with the
bounds of each, such as the exact number's units and scale, a
timestamp's displacement and an interval's qualifier.

The driver reads and writes every type and value below, with the bounds
given; a reply that breaks one fails the connection, SQLCODE -919,
SQLSTATE 08006. [Values](../client/23-values.md) gives the functions
that read each value, and those that make each parameter.

## Data types

| Tag | Type | What it carries |
|---|---|---|
| 1 | CHARACTER | length `uint32_t`; units 0 characters or 1 octets |
| 2 | exact numeric | precision and scale, `uint32_t` |
| 3 | approximate numeric | precision `uint32_t` |
| 4 | a plug-in's type | its name, UTF-8; at most 2 parameters, `uint32_t` |
| 5 | CHARACTER VARYING | as CHARACTER |
| 6, 7 | BINARY, BINARY VARYING | length `uint32_t` |
| 8 to 12 | DATE, TIME, TIME WITH TIME ZONE, TIMESTAMP, TIMESTAMP WITH TIME ZONE | precision `uint32_t` |
| 13 | INTERVAL | start and end fields, 0 YEAR, 1 MONTH, 2 DAY, 3 HOUR, 4 MINUTE, 5 SECOND; leading and fractional precision |
| 14 | BOOLEAN | nothing |
| 15 | DECFLOAT | precision `uint32_t` |
| 16 | array | maximum cardinality, 1 to 65535; element type |
| 17 | row | one or more fields, each a name and a type |
| 18, 19 | CLOB, BLOB | length `uint64_t`; units |
| 22 | multiset | element type |
| 23 | structured type | its schema and name; its proper supertypes |
| 24 | reference type | the referenced type's names; its scope, if scoped |
| 25 | JSON | nothing |
| 26 | XML | primary modifier DOCUMENT, CONTENT or SEQUENCE; secondary UNTYPED, ANY or XMLSCHEMA, and of XMLSCHEMA the schema's names, its namespace and element if named; SEQUENCE is untyped |
| 27 | UUID | nothing |

Arrays, rows and multisets nest at most 16 deep, as turborel's types do.

## Values

| Tag | Value | C type | Bounds |
|---|---|---|---|
| 0 | null | none | |
| 1 | character string | UTF-8 bytes and their length | UTF-8 |
| 2 | exact number | `trl_decimal` | units the whole of `trl_int128`; scale the whole of `uint32_t` |
| 3 | approximate number | `double`, its 64 bits kept | every bit kept: NaN, -0.0 and the infinities too |
| 4 | a plug-in's value | its type's name, its text, its bytes | kept and sent back unchanged |
| 6 | binary string | bytes and their length | |
| 8, 9, 11 | date, time, timestamp | `trl_datetime`: zoned `bool`, precision `uint8_t`, displacement in minutes `int16_t`, instant in UTC microseconds `int64_t` | a displacement of 16 bits |
| 5, 7, 10, 12 | the types' own tags | read as 1, 6, 9 and 11, as turborel-protocol's reader takes them: a character string, a binary string, a time and a timestamp, a zone told inside; the driver sends only 1, 6, 9 and 11 | |
| 13 | interval | `trl_interval`: its qualifier, then units `trl_int128`: months of a year-month interval, microseconds of a day-time one | fields 0 to 5 |
| 14 | truth value | `bool` | 0 or 1; unknown is null |
| 15 | DECFLOAT | `trl_decfloat`: coefficient `trl_int128`, exponent `int32_t` | canonical: at most 34 digits, no trailing zero, zero of exponent 0, adjusted exponent -6143 to 6144 |
| 16, 17 | array, row | a list of values | nested at most 16 deep |
| 20 | large object whole | kind, character or binary; its octets | a character one's UTF-8 |
| 18, 19 | a large object's stored form, which no server sends | refused: the statement fails, SQLCODE -916, SQLSTATE TB916, the connection kept (RULES.md D8) | |
| 21 | large object by locator | kind; locator `uint32_t`; octets and characters `uint64_t` | |
| 22 | multiset | a list of values | at most 65535 elements |
| 23 | structured value | its type's schema and name; its attributes | |
| 24 | reference | bytes | |
| 25 | JSON | the stored form of turborel's RULES.md D89, written as JSON text and made from it | checked whole; nested at most 128 deep |
| 26 | XML | its serialization as CONTENT, UTF-8 | |
| 27 | UUID | 16 octets | exactly 16 |

A JSON value's text is written as turborel writes it: no white space,
members in the order written, an exact number's digits, a DECFLOAT plain,
an approximate number as its shortest numeral with an exponent. A
datetime within a JSON value, or an approximate number that is not
finite, has no JSON text: SQLSTATE 22032. A JSON text made into a value
takes a number without an exponent as exact, one of more than 18
digits, as many as 64 bits always hold, as a DECFLOAT, though an exact
number holds 38, and one with an exponent as approximate; a number none of them holds exactly is SQLSTATE 22003.
