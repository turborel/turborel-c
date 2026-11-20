# Copying, Importing and Exporting

[Contents](README.md)

Rows can go into a table in bulk, and out of a query as they come. This
chapter shows how a program copies rows into a table, all or nothing;
imports CSV and tab-separated text, a batch committed at a time; and
exports a query's rows as CSV, or as the protocol's own frames read
back with every value as it was. [Copy, Import and Export](../client/25-copy.md)
gives each function in full.

## Copying rows

A copy sends rows into a table with no round trip but its end
[PROTOCOL CopyIn 'I']. `trl_copy_in` begins it, naming the table and
its columns, none for all; `trl_copy_send` sends a row, its values in
the columns' order, the rows going in chunks of about 256 KiB;
`trl_copy_finish` ends it and gives the rows inserted, every one of
them or, if one is refused, none [PROTOCOL CopyEnd 'N']. Until it ends,
the connection sends only its rows, its end and Ping. A copy needs the
capability `copy`:

```c
run(conn, "CREATE TABLE CITIES (NAME VARCHAR(20), PEOPLE INTEGER)");
run(conn, "COMMIT");
static const char *const columns[] = {"NAME", "PEOPLE"};
trl_copy_target into = {"CITIES", columns, 2};
trl_copy *copy = NULL;
trl_params *row = NULL;
if (trl_params_new(&row) == TRL_OK && trl_copy_in(conn, &into, &copy) == TRL_OK) {
    static const char *const names[] = {"Lyon", "Turin", "Graz"};
    static const uint64_t people[] = {522000, 848000, 291000};
    for (int i = 0; i < 3; ++i) {
        trl_decimal count = {{people[i], 0}, 0};
        trl_params_clear(row);
        trl_params_add_text(row, names[i], strlen(names[i]));
        trl_params_add_decimal(row, count);
        trl_copy_send(copy, row);
    }
    uint64_t rows = 0;
    if (trl_copy_finish(copy, &rows) == TRL_OK) {
        printf("%llu rows copied\n", (unsigned long long)rows);
    } else {
        print_failure(conn);
    }
}
trl_copy_free(copy);
trl_params_free(row);
run(conn, "COMMIT");
```

```
3 rows copied
```

A copy is part of the transaction under way, or, under autocommit, its
own. `trl_copy_rows` copies every row a function of the program's
gives, and `trl_copy_abandon` gives a copy up, every row of it undone.

## Importing delimited text

`trl_import_rows` inserts a *source*'s records into a table through one
prepared INSERT, of the columns the source's headings name or else of
the table's, a batch of 1000 committed at a time [CLIENT Importing].
Each value is cast by the server to its column's type, but a string for
a character string column is assigned to it as INSERT assigns, so one
too long is refused, not cut. Where the server grants `copy`, each batch
goes by a copy instead. `trl_delimited_new` reads CSV (RFC 4180) or
tab-separated text from a reader of the program's, a record at a time:

```c
static bool read_text(void *context, uint8_t *into, size_t capacity, size_t *length) {
    const char **text = context;
    size_t left = strlen(*text);
    *length = left < capacity ? left : capacity;
    memcpy(into, *text, *length);
    *text += *length;
    return true;
}
```

```c
const char *text = "NAME,PEOPLE\nOslo,709000\n\"Saint-\xc3\x89tienne\",171000\n";
trl_dialect dialect = trl_dialect_csv();
dialect.header = true;
trl_delimited *delimited = NULL;
trl_imported *imported = NULL;
if (trl_delimited_new(read_text, &text, &dialect, &delimited) == TRL_OK) {
    trl_source source = trl_delimited_source(delimited);
    trl_import how = {"CITIES", TRL_IMPORT_BATCH, false, NULL, NULL};
    if (trl_import_rows(conn, &source, &how, &imported) == TRL_OK) {
        printf("%llu rows imported\n", (unsigned long long)trl_imported_rows(imported));
    }
}
trl_imported_free(imported);
trl_delimited_free(delimited);
```

```
2 rows imported
```

A failure rolls the batch under way back and stops the import, saying
the record, the line it began on, and the rows committed and rolled
back, as `trl_imported_text` writes it; or, if the import skips bad
rows, each refused row is given to a handler and the rest go on:

```c
static void rejected(void *context, const trl_rejected *row) {
    (void)context;
    printf("skipped record %llu: %s\n", (unsigned long long)row->position.record, row->cause.message);
}
```

```c
const char *text = "Nice,342000\nLlanfairpwllgwyngyll-and-more,3000\nBern,134000\n";
trl_dialect dialect = trl_dialect_csv();
trl_delimited *delimited = NULL;
trl_imported *imported = NULL;
if (trl_delimited_new(read_text, &text, &dialect, &delimited) == TRL_OK) {
    trl_source source = trl_delimited_source(delimited);
    trl_import stop = {"CITIES", TRL_IMPORT_BATCH, false, NULL, NULL};
    if (trl_import_rows(conn, &source, &stop, &imported) != TRL_OK) {
        char line[512];
        trl_imported_text(imported, line, sizeof line);
        printf("%s\n", line);
    }
}
trl_imported_free(imported);
trl_delimited_free(delimited);
text = "Nice,342000\nLlanfairpwllgwyngyll-and-more,3000\nBern,134000\n";
imported = NULL;
if (trl_delimited_new(read_text, &text, &dialect, &delimited) == TRL_OK) {
    trl_source source = trl_delimited_source(delimited);
    trl_import skip = {"CITIES", TRL_IMPORT_BATCH, true, rejected, NULL};
    if (trl_import_rows(conn, &source, &skip, &imported) == TRL_OK) {
        printf("%llu imported, %llu skipped\n", (unsigned long long)trl_imported_rows(imported),
               (unsigned long long)trl_imported_rejected(imported));
    }
}
trl_imported_free(imported);
trl_delimited_free(delimited);
```

```
record 2, line 2: SQLCODE -301 (SQLSTATE 22001): a value of 29 characters, not spaces beyond 20, does not fit CHARACTER VARYING(20) [ISO/IEC 9075:1992 §13.8 GR4b iii, §9.2 GR3e ii]; 0 rows committed, 1 rolled back
skipped record 2: a value of 29 characters, not spaces beyond 20, does not fit CHARACTER VARYING(20)
2 imported, 1 skipped
```

A field longer than 64 KiB for a large object column is written as a
new large object, by its locator ([Large Objects](06-large-objects.md)),
and such an import goes by INSERT.

## Exporting a query's rows

`trl_export` runs a query and writes its rows as they come, a batch at a
time, to a writer of the program's: as CSV, a header of the columns'
names then a record a row, each value as `trl_value_to_text` writes it,
a field quoted as it must be and a null an empty field; or as the
protocol's own frames [CLIENT Copying and exporting]:

```c
static bool print_bytes(void *context, const uint8_t *bytes, size_t length) {
    (void)context;
    return fwrite(bytes, 1, length, stdout) == length;
}
```

```c
trl_export_sink sink = {TRL_EXPORT_CSV, print_bytes, NULL};
uint64_t rows = 0;
if (trl_export(conn, "SELECT NAME, PEOPLE, PEOPLE > 500000 AS BIG FROM CITIES ORDER BY NAME", &sink, &rows) !=
    TRL_OK) {
    print_failure(conn);
}
printf("(%llu rows)\n", (unsigned long long)rows);
```

```
NAME,PEOPLE,BIG
Bern,134000,FALSE
Graz,291000,FALSE
Lyon,522000,TRUE
Nice,342000,FALSE
Oslo,709000,TRUE
Saint-Étienne,171000,FALSE
Turin,848000,TRUE
(7 rows)
```

Native frames read back with `trl_read_native` into a result, its
columns and every value of whatever type as it was:

```c
static uint8_t kept[65536];
static size_t kept_length = 0;
static size_t kept_read = 0;

static bool keep_bytes(void *context, const uint8_t *bytes, size_t length) {
    (void)context;
    if (kept_length + length > sizeof kept) {
        return false;
    }
    memcpy(kept + kept_length, bytes, length);
    kept_length += length;
    return true;
}

static size_t give_bytes(void *context, uint8_t *into, size_t capacity) {
    (void)context;
    size_t n = kept_length - kept_read < capacity ? kept_length - kept_read : capacity;
    memcpy(into, kept + kept_read, n);
    kept_read += n;
    return n;
}
```

```c
trl_export_sink sink = {TRL_EXPORT_NATIVE, keep_bytes, NULL};
trl_result *result = NULL;
if (trl_export(conn, "SELECT NAME, CAST(PEOPLE AS DOUBLE PRECISION) FROM CITIES ORDER BY NAME", &sink, NULL) ==
        TRL_OK &&
    trl_read_native(give_bytes, NULL, &result) == TRL_OK) {
    printf("%zu bytes, %zu rows\n", kept_length, trl_result_row_count(result));
    char name[64];
    double people = 0;
    trl_value_to_text(trl_result_value(result, 0, 0), name, sizeof name, NULL);
    trl_value_double(trl_result_value(result, 0, 1), &people);
    printf("%s %.0f\n", name, people);
}
trl_result_free(result);
```

```
316 bytes, 7 rows
Bern 134000
```
