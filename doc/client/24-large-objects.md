# Large Objects

[Contents](README.md)

## Values and ranges

### NAME

`trl_lob_info`, `trl_lob_range`, `trl_value_lob`, `trl_lob_read`: a
large object value, whole or by its locator, and a range of it read.

### SYNOPSIS

```c
#include <turborel/turborel.h>

#define TRL_LOB_CHUNK_BYTES 262144
#define TRL_LOB_INLINE_BYTES 65536

typedef struct trl_lob_info {
    bool character;
    bool located;
    uint64_t octets;
    uint64_t characters;
} trl_lob_info;

typedef struct trl_lob_range {
    uint64_t from;
    uint64_t count;
    bool characters;
} trl_lob_range;

trl_status trl_value_lob(const trl_value *value, trl_lob_info *out);
trl_status trl_lob_read(trl_conn *conn, const trl_value *lob, trl_lob_range range, uint8_t *out, size_t *length);
```

### DESCRIPTION

A CLOB or BLOB value of at most SERVER.LOB_INLINE_BYTES, 64 KiB by
default, comes whole in its row, value tag 20; a longer one as its
locator and lengths, tag 21 [PROTOCOL Values]. `trl_value_lob` says
which, whether it is a character large object, and its octets and, of a
character one, its characters [CLIENT Large objects].

`trl_lob_read` reads a range: of a value whole, from the value; of one by
its locator, by a Lob read, at most `TRL_LOB_CHUNK_BYTES`
[PROTOCOL Lob 'L']. The range counts octets from 0, or, with
`characters`, whole characters of a character large object. `*length`
holds the octets `out` has room for on entry, and those read on return;
a range in characters reads at most a quarter as many characters as
`out` holds octets. Fewer than asked come only at the object's end, or a
chunk's.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer, a value of another kind, or
`out` too short for one octet or character, nothing sent. `TRL_FAILED`
with the connection's failure: SQLSTATE 0F001 of a locator no longer
valid, the connection kept; SQLSTATE 08006 of the connection lost or a
reply out of turn.

### EXAMPLES

[Large Objects](../user/06-large-objects.md) reads each.

## Readers

### NAME

`trl_lob_reader`, `trl_lob_reader_new`, `trl_lob_reader_read`,
`trl_lob_reader_free`: a large object read from its beginning to its end.

### SYNOPSIS

```c
trl_status trl_lob_reader_new(trl_conn *conn, const trl_value *lob, trl_lob_reader **out);
trl_status trl_lob_reader_read(trl_lob_reader *reader, uint8_t *out, size_t capacity, size_t *length);
void trl_lob_reader_free(trl_lob_reader *reader);
```

### DESCRIPTION

A reader copies the value and reads it a chunk a request, so memory
holds a chunk whatever its length; it uses the connection until freed,
and is the caller's to free. `trl_lob_reader_read` reads at most
`capacity` octets, `*length` 0 at the end.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer or a value of another kind;
`TRL_NO_MEMORY`. `TRL_FAILED` with the connection's failure, as
`trl_lob_read` fails, or SQLSTATE 22000 of a server that sent less than
the value's length.

## Locators

### NAME

`trl_lob_free_locator`, `trl_lob_hold`: a locator freed, or kept past its
transaction.

### SYNOPSIS

```c
trl_status trl_lob_free_locator(trl_conn *conn, const trl_value *lob);
trl_status trl_lob_hold(trl_conn *conn, const trl_value *lob);
```

### DESCRIPTION

A locator is valid until its transaction ends. `trl_lob_free_locator`
frees it, and the value it names, by a Lob free; `trl_lob_hold` keeps it
valid past its transaction's commit, until it is freed or a transaction
is rolled back, by a Lob hold [PROTOCOL Lob 'L'].

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer or a value of another kind.
`TRL_FAILED` with the connection's failure: SQLSTATE 22000 of a large
object that came whole, which has no locator, nothing sent; SQLSTATE
0F001 of a locator not valid.

## Writers

### NAME

`trl_lob_writer`, `trl_lob_writer_new`, `trl_lob_write`,
`trl_lob_writer_finish`, `trl_lob_writer_free`: a new large object
written a piece at a time.

### SYNOPSIS

```c
trl_status trl_lob_writer_new(trl_conn *conn, bool character, trl_lob_writer **out);
trl_status trl_lob_write(trl_lob_writer *writer, const uint8_t *bytes, size_t length);
trl_status trl_lob_writer_finish(trl_lob_writer *writer, trl_params *params);
void trl_lob_writer_free(trl_lob_writer *writer);
```

### DESCRIPTION

`trl_lob_writer_new` creates an empty large object on the server by a
Lob create, a character one if `character`, its octets UTF-8.
`trl_lob_write` sends what it is given by Lob appends of a chunk as it
comes, a character large object's chunk cut where a character ends, so a
write may end within a character; the server checks a character large
object's octets as UTF-8 as each Append comes. `trl_lob_writer_finish`
sends the rest and adds the object to `params` as its next value, by its
locator, valid until its transaction ends and used once
[PROTOCOL Lob 'L']. A writer uses the connection until freed; one not
finished leaves its object to its transaction's end.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer, or a writer finished;
`TRL_NO_MEMORY`. `TRL_FAILED` with the connection's failure: SQLSTATE
22021 of a character large object whose octets an Append sent are not
UTF-8, every later Append then refused; a character large object that
ends within a character is refused, SQLSTATE 22021, where it is used.

### EXAMPLES

[Large Objects](../user/06-large-objects.md) writes one.
