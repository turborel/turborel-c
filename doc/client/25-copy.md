# Copy, Import and Export

[Contents](README.md)

## Copies

### NAME

`trl_copy_target`, `trl_copy`, `trl_copy_in`, `trl_copy_send`,
`trl_copy_finish`, `trl_copy_abandon`, `trl_copy_free`,
`trl_copy_rows`: rows copied into a table in bulk.

### SYNOPSIS

```c
#include <turborel/turborel.h>

#define TRL_COPY_CHUNK_BYTES 262144

typedef struct trl_copy_target {
    const char *table;
    const char *const *columns;
    size_t column_count;
} trl_copy_target;

typedef enum trl_source_status { TRL_SOURCE_ROW = 0, TRL_SOURCE_END, TRL_SOURCE_FAILED } trl_source_status;
typedef trl_source_status (*trl_row_source)(void *context, trl_params *row);

trl_status trl_copy_in(trl_conn *conn, const trl_copy_target *target, trl_copy **out);
trl_status trl_copy_send(trl_copy *copy, const trl_params *row);
trl_status trl_copy_finish(trl_copy *copy, uint64_t *rows);
trl_status trl_copy_abandon(trl_copy *copy);
void trl_copy_free(trl_copy *copy);
trl_status trl_copy_rows(trl_conn *conn, const trl_copy_target *target, trl_row_source source, void *context,
                         uint64_t *rows);
```

### DESCRIPTION

`trl_copy_in` sends CopyIn of the table, as SQL writes its name, and its
columns, none for every column of the table, its `ends` set under
autocommit [PROTOCOL CopyIn 'I']; it needs `copy`. Until the copy ends,
the connection sends only its rows, its end and Ping, anything else
refused before it is sent. `trl_copy_send` adds a row, its values in
the columns' order, and sends the rows written in a CopyData once they
reach `TRL_COPY_CHUNK_BYTES`, with no reply [PROTOCOL CopyData 'J'].
With `columns` granted *(extension)*, a row whose every value is null or
a character string is gathered by column instead, each cell its string's
text, a null a bit set, and the rows so gathered are sent in a
CopyColumns once they reach `TRL_COPY_CHUNK_BYTES`, or when a row that
does not go so comes: one holding a value of another type, or another
count of values, which goes in a CopyData in its turn, so the rows go
in order [PROTOCOL CopyColumns 'O']. The server takes each cell as it
takes the string in a CopyData, so the rows inserted and the failures
are those of a copy by row.
`trl_copy_finish` sends the rest and CopyEnd, answered by a Complete of
the rows inserted, every row of the copy, or by its failure, none of
them [PROTOCOL CopyEnd 'N']; `trl_copy_abandon` sends CopyEnd abandoned,
every row undone. `trl_copy_free` frees a copy, one not ended abandoned
first so the connection takes requests again. `trl_copy_rows` copies
every row `source` gives into `row`, emptied each time, as these do
[CLIENT Copying and exporting].

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer, a column not named, or a
copy ended; `TRL_NO_MEMORY`. `TRL_FAILED` with the connection's
failure: SQLSTATE 08006 of `copy` not granted, nothing sent, the
connection kept; SQLSTATE TB910 of a request other than the copy's,
Ping's or its end while it is under way; a value the protocol cannot
carry, SQLSTATE 22000 or another of class 22; the copy's failure, every
row undone, a table or column not found, a row of too many or too few
values, a value its column refuses, its detail naming the row, as
"row 7 of the copy"; SQLSTATE TB910 of a source that failed, the copy
abandoned; the connection lost.

### EXAMPLES

[Copying, Importing and Exporting](../user/07-copying.md) copies rows.

## Imports

### NAME

`trl_source`, `trl_import`, `trl_import_rows`, `trl_imported`,
`trl_imported_rows`, `trl_imported_rejected`, `trl_imported_cause`,
`trl_imported_position`, `trl_imported_rolled_back`,
`trl_imported_text`, `trl_imported_free`: a source's records inserted
into a table.

### SYNOPSIS

```c
#define TRL_IMPORT_BATCH 1000

typedef struct trl_position { uint64_t record; uint64_t line; } trl_position;
typedef struct trl_source_error { trl_position position; const char *message; } trl_source_error;
typedef struct trl_headings { const char *const *names; size_t count; } trl_headings;
typedef enum trl_source_read { TRL_READ_OK = 0, TRL_READ_END, TRL_READ_FAILED } trl_source_read;

typedef struct trl_source {
    void *context;
    trl_source_read (*headings)(void *context, trl_headings *out, trl_source_error *error);
    trl_source_read (*record)(void *context, trl_params *values, trl_position *at, trl_source_error *error);
} trl_source;

typedef struct trl_cause { bool sql; const char *message; const trl_failure *failure; } trl_cause;
typedef struct trl_rejected { trl_position position; trl_cause cause; } trl_rejected;
typedef void (*trl_rejected_handler)(void *context, const trl_rejected *row);

typedef struct trl_import {
    const char *table;
    uint64_t batch;
    bool skip;
    trl_rejected_handler rejected;
    void *context;
} trl_import;

trl_status trl_import_rows(trl_conn *conn, const trl_source *source, const trl_import *how, trl_imported **out);
uint64_t trl_imported_rows(const trl_imported *imported);
uint64_t trl_imported_rejected(const trl_imported *imported);
bool trl_imported_cause(const trl_imported *imported, trl_cause *cause);
bool trl_imported_position(const trl_imported *imported, trl_position *position);
uint64_t trl_imported_rolled_back(const trl_imported *imported);
size_t trl_imported_text(const trl_imported *imported, char *out, size_t capacity);
void trl_imported_free(trl_imported *imported);
```

### DESCRIPTION

`trl_import_rows` inserts the source's records into `how->table`, as
SQL writes its name, written as delimited identifiers
[ISO/IEC 9075:1992 §5.4], through one prepared INSERT: of the columns
the source's headings name, a heading that is one identifier naming it,
or else of the table's columns, as a query of them describes them
[CLIENT Importing]. Each value is cast by the server to its column's
type, but a string for a character string column is assigned to it as
INSERT assigns [ISO/IEC 9075:1992 §13.8 GR4b iii], padded to a CHARACTER
column's length, its spaces past the length cut, and refused if longer,
SQLSTATE 22001, where CAST would cut it; a field longer than 64 KiB for
a large object column is sent ahead as a new large object, by its
locator. `how->batch` rows are committed at a time, 0 for all at once.
The session must have no transaction active.

Where the server grants `copy`, no row is skipped and no column is a
large object's, each batch goes by a copy, its rows checked as the
INSERT's parameters take them; a copy refused at a row stops the import
at that row's record [CLIENT Copying and exporting].

A failure rolls the batch under way back and stops the import; with
`skip`, a row the server refuses, the row alone undone, or one of too
many or too few fields, or too long for its column, is skipped and given
to `how->rejected`, if not null, and the import goes on. What it did is
`*out`'s, made even on `TRL_FAILED`: the rows committed, those skipped,
why it stopped, an SQL condition or the input's, where the record that
stopped it began, and the rows of its batch rolled back.
`trl_imported_text` writes why it stopped as one line, as turborel's
own client displays it: "record 3, line 4: cause; 2 rows committed, 1
rolled back", ended by a null and cut short to fit; it gives the whole
text's bytes, 0 of an import that did not fail.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer, nothing made; `TRL_NO_MEMORY`,
nothing made. `TRL_FAILED` with `*out` holding why: a transaction
active, "[DESIGN] an import begins with no transaction active, as it
commits its batches"; a table
that is no table name; the source's failure; an SQL condition, the
server's or a string too long; a record of too many or too few fields;
the connection lost.

## Delimited text

### NAME

`trl_dialect`, `trl_dialect_csv`, `trl_dialect_tsv`, `trl_text_read`,
`trl_delimited_new`, `trl_delimited_source`, `trl_delimited_free`: CSV
and tab-separated text as a source.

### SYNOPSIS

```c
#define TRL_DELIMITED_MAX_RECORD 16777216

typedef struct trl_dialect {
    char delimiter;
    bool quoted;
    bool header;
    const char *null;
    size_t max_record;
} trl_dialect;

typedef bool (*trl_text_read)(void *context, uint8_t *into, size_t capacity, size_t *length);

trl_dialect trl_dialect_csv(void);
trl_dialect trl_dialect_tsv(void);
trl_status trl_delimited_new(trl_text_read read, void *context, const trl_dialect *dialect, trl_delimited **out);
trl_source trl_delimited_source(trl_delimited *delimited);
void trl_delimited_free(trl_delimited *delimited);
```

### DESCRIPTION

Delimited text is read a record at a time from `read`, so memory holds a
record whatever the input's length: a record a line, a line break CR LF
or LF, the last one's optional; UTF-8, a byte order mark beginning it
passed over. CSV (RFC 4180) splits fields at commas, a field enclosed in
quotes free to hold the delimiter, a line break and a quote written
twice; tab-separated text splits at tabs, never quoted. A field not
enclosed in quotes that is `null`'s text, "" by default, is null; any
other is a character string, cast to its column's type as it is
inserted. With `header`, the first record names the fields. A record
longer than `max_record` bytes, its line break counted, is refused once
that many are read. Records are numbered from 1, a header not counted,
each with the line it began on [CLIENT Importing].

### ERRORS

`trl_delimited_new`: `TRL_INVALID_ARGUMENT` for a null pointer, or a
delimiter not one ASCII character other than a quote, CR or LF;
`TRL_NO_MEMORY`. A record the source cannot read fails the import,
saying where: a quote in a field not enclosed in quotes, more after a
closing quote, a quoted field not closed [RFC 4180 §2 rule 5], a field
not UTF-8, a record too long, no header, or a reader that failed; the
source then gives no more.

## Exports

### NAME

`trl_export_format`, `trl_export_sink`, `trl_export`,
`trl_read_native`, `trl_result_failure`: a query's rows written as they
come, and native rows read back.

### SYNOPSIS

```c
typedef enum trl_export_format { TRL_EXPORT_CSV = 0, TRL_EXPORT_NATIVE } trl_export_format;
typedef bool (*trl_export_write)(void *context, const uint8_t *bytes, size_t length);

typedef struct trl_export_sink {
    trl_export_format format;
    trl_export_write write;
    void *context;
} trl_export_sink;

typedef size_t (*trl_native_read)(void *context, uint8_t *into, size_t capacity);

trl_status trl_export(trl_conn *conn, const char *sql, const trl_export_sink *sink, uint64_t *rows);
trl_status trl_read_native(trl_native_read read, void *context, trl_result **out);
const trl_failure *trl_result_failure(const trl_result *result);
```

### DESCRIPTION

`trl_export` sends Query and writes the rows as each batch comes, so
memory holds a batch whatever the result's size
[CLIENT Copying and exporting]. As CSV: a header of the columns' names, then a record a
row, each ended by CR LF, each value as `trl_value_to_text` writes it, a
field quoted where it is empty or holds a comma, a quote or a line
break, or begins or ends with white space, Unicode's, as U+00A0 or
U+3000 as well as a space or a tab, a quote in it written twice,
a null an empty field. As native: the frames the server sent, a
Description then Rows, and a Complete. `*rows`, if not null, gets the
rows written.

`trl_read_native` reads native rows back into a result, its columns and
every value as it was, of whatever type; on `TRL_FAILED` the result is
made, holding its failure, which `trl_result_failure` gives, null of any
other result.

### ERRORS

`trl_export`: `TRL_INVALID_ARGUMENT` for a null pointer, nothing sent.
`TRL_FAILED` with the connection's failure: the query's Error; SQLSTATE
08006, the connection kept, of a statement that is no query ("an export
runs a query") or a write that failed ("cannot write the export"); the
connection lost, or a reply out of turn.

`trl_read_native`: `TRL_INVALID_ARGUMENT` for a null pointer;
`TRL_NO_MEMORY`, nothing made. `TRL_FAILED`, SQLSTATE 08006, of input
that is not native rows: ended before a frame or within one, a frame
longer than the protocol's longest, no Description first, or a frame
other than Rows before the Complete; or a frame that does not decode,
with its own failure.

### EXAMPLES

[Copying, Importing and Exporting](../user/07-copying.md) exports rows
as CSV and as native frames.
