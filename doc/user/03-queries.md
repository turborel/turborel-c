# Running Queries

[Contents](README.md)

A program runs a statement by its text, and reads what comes back: a
query's columns and rows, or another statement's completion. This
chapter shows how, how rows come a batch at a time, the values the
driver gives for each type, and what a completion tells besides.
[Statements and Cursors](../client/22-statements.md) and
[Values](../client/23-values.md) give each function in full.

## A first query

`trl_query` sends a statement and waits for all its outcome. A query
gives a *result* of its columns and rows; any other statement a result
of no rows, with its completion's SQLCODE and SQLSTATE:

```c
#include <turborel/turborel.h>

#include <stdio.h>

int main(void) {
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    trl_conn *conn = NULL;
    trl_result *result = NULL;
    if (trl_target_tcp("127.0.0.1", 17438, &target) != TRL_OK || trl_mechanism_trust(&trust) != TRL_OK ||
        trl_connect(target, "ann", trust, NULL, &conn) != TRL_OK) {
        return 1;
    }
    trl_query(conn, "CREATE TABLE STAFF (ID INTEGER PRIMARY KEY, NAME VARCHAR(20), SALARY DECIMAL(8,2))", &result);
    trl_result_free(result);
    trl_query(conn, "INSERT INTO STAFF VALUES (1, 'Ada', 4200.50), (2, 'Brian', 3900), (3, 'Chen', NULL)", &result);
    printf("SQLCODE %d, SQLSTATE %s\n", trl_result_sqlcode(result), trl_result_sqlstate(result));
    trl_result_free(result);
    if (trl_query(conn, "SELECT ID, NAME, SALARY FROM STAFF ORDER BY ID", &result) == TRL_OK) {
        for (size_t column = 0; column < trl_result_column_count(result); ++column) {
            printf("%-8s", trl_result_column_name(result, column));
        }
        printf("\n");
        for (size_t row = 0; row < trl_result_row_count(result); ++row) {
            for (size_t column = 0; column < trl_result_column_count(result); ++column) {
                const trl_value *value = trl_result_value(result, row, column);
                char text[TRL_DECIMAL_TEXT_BYTES];
                trl_decimal number;
                size_t length = 0;
                const char *chars = trl_value_text(value, &length);
                if (trl_value_is_null(value)) {
                    printf("%-8s", "null");
                } else if (chars != NULL) {
                    printf("%-8.*s", (int)length, chars);
                } else if (trl_value_decimal(value, &number) == TRL_OK &&
                           trl_decimal_text(&number, text, sizeof text) == TRL_OK) {
                    printf("%-8s", text);
                }
            }
            printf("\n");
        }
    }
    trl_result_free(result);
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    trl_target_free(target);
    return 0;
}
```

```
SQLCODE 0, SQLSTATE 00000
ID      NAME    SALARY  
1       Ada     4200.50 
2       Brian   3900.00 
3       Chen    null    
```

A result is the program's own: it lasts, its values with it, until
`trl_result_free`, whatever the connection does meanwhile. Rows and
columns are counted from 0. A query that returns no row completes with
SQLCODE 100 and SQLSTATE 02000. A statement that fails gives
`TRL_FAILED` and no result; the connection holds the failure
([Errors and Diagnostics](10-errors.md)). Each statement begins a
transaction if none is active, which lasts until COMMIT or ROLLBACK
([Transactions](05-transactions.md)).

The examples after this one are the body of such a program, after its
connection `conn`; `run` there runs a statement and frees its result,
and `print_failure` prints the connection's failure as
`trl_failure_text` writes it.

## Columns and their types

Each column has a name, unless the query gave it none, and a type.
`trl_type_kind` gives a type's kind, and `trl_type_length`,
`trl_type_precision` and `trl_type_scale` its sizes:

```c
trl_result *result = NULL;
run(conn, "CREATE TABLE ITEMS (CODE CHAR(4), PRICE NUMERIC(9,2), WEIGHT DOUBLE PRECISION, ADDED TIMESTAMP(3))");
if (trl_query(conn, "SELECT CODE, PRICE, WEIGHT, ADDED, PRICE * 2 FROM ITEMS", &result) == TRL_OK) {
    for (size_t column = 0; column < trl_result_column_count(result); ++column) {
        const trl_data_type *type = trl_result_column_type(result, column);
        const char *name = trl_result_column_name(result, column);
        printf("%-7s kind %2d length %llu precision %u scale %u\n", name == NULL ? "(none)" : name,
               (int)trl_type_kind(type), (unsigned long long)trl_type_length(type), trl_type_precision(type),
               trl_type_scale(type));
    }
    printf("%d %s\n", trl_result_sqlcode(result), trl_result_sqlstate(result));
}
trl_result_free(result);
```

```
CODE    kind  1 length 4 precision 0 scale 0
PRICE   kind  2 length 0 precision 9 scale 2
WEIGHT  kind  3 length 0 precision 53 scale 0
ADDED   kind 11 length 0 precision 3 scale 0
(none)  kind  2 length 0 precision 18 scale 2
100 02000
```

## Declared types

A type's kind can stand for several declared types: SMALLINT, INTEGER,
DECIMAL and NUMERIC are all exact, INTEGER of precision 10 and scale 0;
REAL, DOUBLE PRECISION and FLOAT are all approximate; a distinct type
is its source type. A connection asks for the capability
`declared-types`, as it asks for every capability but compression, and
the server then tells each column's declared type:
`trl_result_column_type_name` gives its name as the Information
Schema's DATA_TYPE gives it, and `trl_result_column_type_written` the
whole type as SQL writes it. Where the server did not tell it, the name
is null, and the type is written as its kind and sizes say, as
`trl_type_to_text` writes a type, so INTEGER is NUMERIC(10,0)
[PROTOCOL Declared types]:

```c
trl_result *result = NULL;
run(conn, "CREATE TYPE MONEY AS DECIMAL(9,2) FINAL");
run(conn, "CREATE TABLE STOCK (N INTEGER, SMALL SMALLINT, COST MONEY, RATIO FLOAT(20), BINS INTEGER ARRAY[3])");
if (trl_query(conn, "SELECT N, SMALL, COST, RATIO, BINS, N + 1 FROM STOCK", &result) == TRL_OK) {
    for (size_t column = 0; column < trl_result_column_count(result); ++column) {
        const char *name = trl_result_column_type_name(result, column);
        char tag[64];
        trl_type_to_text(trl_result_column_type(result, column), tag, sizeof tag, NULL);
        printf("%-12s %-18s %s\n", name == NULL ? "(none)" : name, trl_result_column_type_written(result, column),
               tag);
    }
}
trl_result_free(result);
```

```
INTEGER      INTEGER            NUMERIC(10,0)
SMALLINT     SMALLINT           NUMERIC(5,0)
USER-DEFINED ANN.MONEY          NUMERIC(9,2)
FLOAT        FLOAT(24)          FLOAT(24)
ARRAY        INTEGER ARRAY[3]   NUMERIC(10,0) ARRAY[3]
NUMERIC      NUMERIC(18,0)      NUMERIC(18,0)
```

## Values

A value is read by its kind, `trl_value_kind`, through the function of
that kind; a function given a value of another kind fails, or gives
null. Every value the server sends is given whole, with nothing lost:

| Kind | Read by | As |
|---|---|---|
| `TRL_KIND_NULL` | `trl_value_is_null` | the null value, of any type |
| `TRL_KIND_CHARACTER` | `trl_value_text` | UTF-8 and its length, not ended by a null |
| `TRL_KIND_BINARY` | `trl_value_bytes` | the bytes and their count |
| `TRL_KIND_EXACT` | `trl_value_decimal` | a `trl_decimal`, 128-bit units and a scale, which `trl_decimal_text` writes |
| `TRL_KIND_APPROXIMATE` | `trl_value_double` | a `double` |
| `TRL_KIND_DECFLOAT` | `trl_value_decfloat` | a coefficient of 128 bits and an exponent |
| `TRL_KIND_BOOLEAN` | `trl_value_boolean` | a `bool` |
| `TRL_KIND_DATE`, `_TIME`, `_TIMESTAMP` | `trl_value_datetime` | microseconds in UTC from 0001-01-01 00:00:00, of a time from midnight, and, of one WITH TIME ZONE, `zoned` and the zone's displacement in minutes |
| `TRL_KIND_INTERVAL` | `trl_value_interval` | months, or microseconds, and its qualifier |
| `TRL_KIND_ARRAY`, `_ROW`, `_MULTISET`, `_STRUCTURED` | `trl_value_count`, `trl_value_at` | its elements, fields or attributes, each a value |
| `TRL_KIND_PLUGIN` | `trl_value_text`, `trl_value_bytes` | a plug-in type's value, its text and its bytes |
| `TRL_KIND_JSON` | `trl_value_json_text` | its JSON text |
| `TRL_KIND_XML` | `trl_value_text`, `trl_value_xml` | its serialization, or its nodes |
| `TRL_KIND_UUID` | `trl_value_uuid`, `trl_uuid_text` | its 16 octets, or its text |

Large objects, CLOB and BLOB, come whole or as a locator, as
[Large Objects](06-large-objects.md) tells. An array or a row holds
other values:

```c
trl_result *result = NULL;
if (trl_query(conn, "VALUES (ARRAY[1.5, 2.25], ROW('a', TRUE), 7.5E0, INTERVAL '3' DAY)", &result) == TRL_OK) {
    const trl_value *array = trl_result_value(result, 0, 0);
    for (size_t i = 0; i < trl_value_count(array); ++i) {
        trl_decimal number;
        char text[TRL_DECIMAL_TEXT_BYTES];
        trl_value_decimal(trl_value_at(array, i), &number);
        trl_decimal_text(&number, text, sizeof text);
        printf("element %zu: %s\n", i, text);
    }
    const trl_value *row = trl_result_value(result, 0, 1);
    bool truth = false;
    size_t length = 0;
    const char *field = trl_value_text(trl_value_at(row, 0), &length);
    trl_value_boolean(trl_value_at(row, 1), &truth);
    printf("row: %.*s, %s\n", (int)length, field, truth ? "true" : "false");
    double approximate = 0;
    trl_value_double(trl_result_value(result, 0, 2), &approximate);
    printf("double: %g\n", approximate);
    trl_interval interval;
    trl_value_interval(trl_result_value(result, 0, 3), &interval);
    printf("interval: %lld microseconds\n", (long long)interval.units.low);
} else {
    print_failure(conn);
}
trl_result_free(result);
```

```
element 0: 1.50
element 1: 2.25
row: a, true
double: 7.5
interval: 259200000000 microseconds
```

### JSON, XML and UUIDs

A JSON value comes in the form the server stores it in; its JSON text is
had by `trl_value_json_text`. An XML value comes as its serialization,
which `trl_value_text` gives, and `trl_value_xml` parses into a tree of
nodes ([Values](../client/23-values.md)). A UUID comes as its 16 octets,
and `trl_uuid_text` writes them as text:

```c
trl_result *result = NULL;
run(conn, "CREATE TABLE ORDERS (ID UUID, BODY XML, DATA JSON)");
run(conn, "INSERT INTO ORDERS VALUES (CAST('919108f7-52d1-4320-9bac-f847db4148a8' AS UUID), "
          "XMLPARSE(CONTENT '<order no=\"18\"><item>jam</item></order>' PRESERVE WHITESPACE), JSON('{\"items\": [1, 2]}'))");
if (trl_query(conn, "SELECT ID, BODY, DATA FROM ORDERS", &result) == TRL_OK) {
    uint8_t octets[TRL_UUID_BYTES];
    char uuid[TRL_UUID_TEXT_BYTES];
    trl_value_uuid(trl_result_value(result, 0, 0), octets);
    trl_uuid_text(octets, uuid);
    printf("%s\n", uuid);
    trl_xml *xml = NULL;
    if (trl_value_xml(trl_result_value(result, 0, 1), &xml) == TRL_OK) {
        const trl_xml_node *order = trl_xml_node_child(trl_xml_root(xml), 0);
        const trl_xml_node *no = trl_xml_node_attribute(order, 0);
        printf("<%s> %s=%s\n", trl_xml_node_name(order), trl_xml_node_name(no), trl_xml_node_value(no));
    }
    trl_xml_free(xml);
    char json[64];
    trl_value_json_text(trl_result_value(result, 0, 2), json, sizeof json, NULL);
    printf("%s\n", json);
} else {
    print_failure(conn);
}
trl_result_free(result);
```

```
919108f7-52d1-4320-9bac-f847db4148a8
<order> no=18
{"items":[1,2]}
```

A value the server cannot serialize as XML, such as an attribute
standing alone, fails the query with SQLSTATE 2200W.

## Rows a batch at a time

`trl_query` keeps every row of a query in its result. A query of many
rows is better read by `trl_query_batches`, which gives each batch of
rows to a function of the program's as it comes, and keeps none: a batch
lasts only until the function returns. The result it gives at the end
has the columns and the completion, and no rows. The function first:

```c
static void count(void *context, const trl_result *batch) {
    size_t *rows = context;
    *rows += trl_result_row_count(batch);
}
```

Then the query:

```c
size_t rows = 0;
trl_result *result = NULL;
if (trl_query_batches(conn, "SELECT X FROM (VALUES (1), (2), (3)) AS V(X)", count, &rows, &result) == TRL_OK) {
    printf("%zu rows, %zu kept\n", rows, trl_result_row_count(result));
}
trl_result_free(result);
```

```
3 rows, 0 kept
```

A failure in the middle of the rows ends the call with `TRL_FAILED`,
the batches before it given.

## What a completion tells

A statement's completion tells more than its SQLCODE and SQLSTATE, and
the connection keeps what the last one told until the next statement:

| Function | Gives |
|---|---|
| `trl_conn_rows_affected` | the rows an INSERT, UPDATE or DELETE changed, or a query returned |
| `trl_conn_generated_count`, `trl_conn_generated_at` | each identity value an INSERT generated, as SQL writes it, in the order of its rows |
| `trl_conn_server_time` | the server's time for the statement, in microseconds: in all, queued, and waiting for locks |
| `trl_conn_commit_position` | the log position of the last commit the session made |
| `trl_conn_warning` | the completion's SQLSTATE if it is a warning, of class 01 |

```c
run(conn, "CREATE TABLE TICKETS (ID INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY, TITLE VARCHAR(20))");
run(conn, "INSERT INTO TICKETS (TITLE) VALUES ('door'), ('lamp')");
uint64_t rows = 0;
trl_timing timing;
if (trl_conn_rows_affected(conn, &rows)) {
    printf("%llu rows\n", (unsigned long long)rows);
}
for (size_t i = 0; i < trl_conn_generated_count(conn); ++i) {
    printf("generated %s\n", trl_conn_generated_at(conn, i));
}
printf("timed: %s\n", trl_conn_server_time(conn, &timing) && timing.total >= timing.queued ? "yes" : "no");
run(conn, "COMMIT");
uint64_t position = 0;
printf("committed: %s\n", trl_conn_commit_position(conn, &position) ? "yes" : "no");
```

```
2 rows
generated 1
generated 2
timed: yes
committed: yes
```

A statement's warnings, its *conditions*, and the notices the server
sends are told in [Errors and Diagnostics](10-errors.md).

## Several statements at once

`trl_query_many` sends several statements, separated by semicolons, as
one request. They run one by one, each as if sent alone, and the first
that fails stops the rest; what each that ran gave is kept, in order,
with the failure:

```c
trl_ran *ran = NULL;
if (trl_query_many(conn, "CREATE TABLE T (N INTEGER); INSERT INTO T VALUES (1); SELECT N FROM NOPE; "
                         "INSERT INTO T VALUES (2)", &ran) == TRL_OK) {
    printf("%zu ran\n", trl_ran_count(ran));
    if (trl_ran_failure(ran) != NULL) {
        printf("stopped: %s\n", trl_failure_message(trl_ran_failure(ran)));
    }
}
trl_ran_free(ran);
```

```
2 ran
stopped: table ANN.NOPE is not defined
```

It needs the server's capability `multi-statement`; without it, the call
fails before it sends anything.
