# Large Objects

[Contents](README.md)

A CHARACTER LARGE OBJECT or BINARY LARGE OBJECT value can be far longer
than any other. This chapter shows how a program reads one, whole from
its row or by its locator, a range or a stream at a time; how long a
locator lasts, freed or held; and how it writes a new one a piece at a
time and passes it as a parameter. [Large Objects](../client/24-large-objects.md)
gives each function in full.

## Whole, or by its locator

A large object of at most 64 KiB, SERVER.LOB_INLINE_BYTES, comes whole
in its row; a longer one comes as its *locator*, a number that names it
on the server, with its lengths [PROTOCOL Values]. `trl_value_lob`
tells which, and how long it is, in octets and, of a character one, in
characters:

```c
run(conn, "CREATE TABLE NOTES (ID INTEGER, BODY CLOB(1M))");
run(conn, "INSERT INTO NOTES VALUES (1, 'short'), (2, RPAD(CAST('' AS CLOB(1M)), 100000, 'long '))");
trl_result *result = NULL;
if (trl_query(conn, "SELECT ID, BODY FROM NOTES ORDER BY ID", &result) == TRL_OK) {
    for (size_t row = 0; row < trl_result_row_count(result); ++row) {
        trl_lob_info info;
        trl_value_lob(trl_result_value(result, row, 1), &info);
        printf("%s, %llu octets, %llu characters\n", info.located ? "by locator" : "whole",
               (unsigned long long)info.octets, (unsigned long long)info.characters);
    }
}
trl_result_free(result);
run(conn, "COMMIT");
```

```
whole, 5 octets, 5 characters
by locator, 100000 octets, 100000 characters
```

A value that came whole reads as text or bytes, as `trl_value_text`
and `trl_value_bytes` read a string; one by its locator holds no octets
itself.

## Reading a range, or a stream

`trl_lob_read` reads a range of either: of one whole, from the value; of
one by its locator, from the server, at most 256 KiB a request. A range
of a character large object may count characters, so no character is
cut:

```c
trl_result *result = NULL;
if (trl_query(conn, "SELECT BODY FROM NOTES WHERE ID = 2", &result) == TRL_OK) {
    const trl_value *body = trl_result_value(result, 0, 0);
    uint8_t out[64];
    size_t length = sizeof out;
    trl_lob_range first = {0, 9, true};
    if (trl_lob_read(conn, body, first, out, &length) == TRL_OK) {
        printf("[%.*s]\n", (int)length, (const char *)out);
    }
    trl_lob_reader *reader = NULL;
    uint64_t total = 0;
    if (trl_lob_reader_new(conn, body, &reader) == TRL_OK) {
        uint8_t piece[4096];
        while (trl_lob_reader_read(reader, piece, sizeof piece, &length) == TRL_OK && length > 0) {
            total += length;
        }
    }
    printf("%llu octets read\n", (unsigned long long)total);
    trl_lob_reader_free(reader);
}
trl_result_free(result);
run(conn, "COMMIT");
```

```
[long long]
100000 octets read
```

A reader reads from the beginning to the end, a chunk a request, so
memory holds a chunk whatever the object's length.

## How long a locator lasts

A locator is valid until its transaction ends. `trl_lob_free_locator`
frees one sooner, so the server holds it no longer; `trl_lob_hold`
keeps one past its transaction's commit, until it is freed or a
transaction is rolled back [CLIENT Large objects]. A locator no longer
valid fails a read with SQLSTATE 0F001:

```c
trl_result *result = NULL;
if (trl_query(conn, "SELECT BODY FROM NOTES WHERE ID = 2", &result) == TRL_OK) {
    const trl_value *body = trl_result_value(result, 0, 0);
    uint8_t out[16];
    size_t length = sizeof out;
    trl_lob_range range = {0, 4, false};
    trl_lob_hold(conn, body);
    run(conn, "COMMIT");
    if (trl_lob_read(conn, body, range, out, &length) == TRL_OK) {
        printf("held: [%.*s]\n", (int)length, (const char *)out);
    }
    trl_lob_free_locator(conn, body);
    length = sizeof out;
    if (trl_lob_read(conn, body, range, out, &length) != TRL_OK) {
        printf("freed: %s\n", trl_failure_sqlstate(trl_conn_failure(conn)));
    }
}
trl_result_free(result);
run(conn, "ROLLBACK");
```

```
held: [long]
freed: 0F001
```

## Writing one

A large object too long for a parameter of its own is written a piece
at a time. `trl_lob_writer_new` creates one on the server, empty;
`trl_lob_write` sends what it is given in chunks as it comes, a write
free to end within a character; `trl_lob_writer_finish` sends the rest
and adds the object to a parameter list, by its locator
[PROTOCOL Lob 'L']. The server checks a character large object's
octets as UTF-8 as each Append comes, SQLSTATE 22021:

```c
trl_statement *insert = NULL;
trl_params *params = NULL;
trl_lob_writer *writer = NULL;
if (trl_prepare(conn, "INSERT INTO NOTES VALUES (?, ?)", &insert) == TRL_OK && trl_params_new(&params) == TRL_OK) {
    trl_decimal id = {{3, 0}, 0};
    trl_params_add_decimal(params, id);
    if (trl_lob_writer_new(conn, true, &writer) == TRL_OK) {
        for (int i = 0; i < 1000; ++i) {
            trl_lob_write(writer, (const uint8_t *)"caf\xc3\xa9 ", 6);
        }
        trl_lob_writer_finish(writer, params);
    }
    trl_result *result = NULL;
    if (trl_execute(conn, insert, params, &result) == TRL_OK) {
        printf("inserted\n");
    } else {
        print_failure(conn);
    }
    trl_result_free(result);
}
trl_lob_writer_free(writer);
trl_params_free(params);
trl_statement_free(insert);
trl_result *check = NULL;
if (trl_query(conn, "SELECT CHAR_LENGTH(BODY), OCTET_LENGTH(BODY) FROM NOTES WHERE ID = 3", &check) == TRL_OK) {
    char characters[32];
    char octets[32];
    trl_value_to_text(trl_result_value(check, 0, 0), characters, sizeof characters, NULL);
    trl_value_to_text(trl_result_value(check, 0, 1), octets, sizeof octets, NULL);
    printf("%s characters, %s octets\n", characters, octets);
}
trl_result_free(check);
run(conn, "COMMIT");
```

```
inserted
5000 characters, 6000 octets
```

A locator a writer made is valid until its transaction ends, and is
used once. An import writes each long field for a large object column
so ([Copying, Importing and Exporting](07-copying.md)).
