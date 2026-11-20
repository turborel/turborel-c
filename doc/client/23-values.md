# Values

[Contents](README.md)

This page describes the values the low-level client gives and takes for
each of the protocol's types: exact numbers, every value a result holds
and the functions that read it, a column's or a parameter's type, the
parameters a statement is executed with, UUIDs, and an XML value's
nodes. Large objects have their own page, [Large Objects](24-large-objects.md),
and [Appendix A](../appendix/a-types.md) maps each type and value to
its C type.

## Exact numbers

### NAME

`trl_decimal`, `trl_int128`, `trl_decimal_text`, `trl_decimal_parse`: an
exact number of the protocol, and its text.

### SYNOPSIS

```c
#include <turborel/turborel.h>

typedef struct trl_int128 { uint64_t low; int64_t high; } trl_int128;
typedef struct trl_decimal { trl_int128 units; uint32_t scale; } trl_decimal;

trl_status trl_decimal_text(const trl_decimal *number, char *out, size_t capacity);
trl_status trl_decimal_parse(const char *text, trl_decimal *out);
```

### DESCRIPTION

An exact number is its units divided by ten to the power of its scale,
as the protocol carries it: units a signed 128-bit integer, held as two
64-bit halves, and scale an unsigned 32-bit integer
([PROTOCOL Values]). Every exact number a server sends fits, the least
and greatest units and the greatest scale among them, and nothing is
lost.

`trl_decimal_text` writes a number as turborel writes one: its digits, a
minus sign before them if it is negative, and a decimal point before its
last `scale` digits, with zeros before them where it has fewer. A scale
past 18, which no type of turborel's has, is written as a power of ten
instead, as `5E-40`. `TRL_DECIMAL_TEXT_BYTES` always holds the text.

`trl_decimal_parse` reads an optional sign, digits, optionally a decimal
point and more digits, and optionally `E` or `e` and a signed power of
ten, with no spaces.

### ERRORS

`TRL_INVALID_ARGUMENT` for a null pointer or a buffer too short;
`TRL_INVALID_TEXT` for text not of that form; `TRL_OUT_OF_RANGE` for
units past 128 bits or a scale past 32.

### EXAMPLES

```c
trl_decimal price;
char text[TRL_DECIMAL_TEXT_BYTES];
if (trl_decimal_parse("-12.50", &price) != TRL_OK) {
    return 1;
}
printf("scale %u\n", (unsigned int)price.scale);
if (trl_decimal_text(&price, text, sizeof text) == TRL_OK) {
    printf("%s\n", text);
}
trl_decimal largest;
trl_decimal_parse("170141183460469231731687303715884105727", &largest);
trl_decimal_text(&largest, text, sizeof text);
printf("%s\n", text);
printf("%d\n", trl_decimal_parse("1E39", &largest) == TRL_OUT_OF_RANGE);
```

```
scale 2
-12.50
170141183460469231731687303715884105727
1
```

### STANDARD

[PROTOCOL Values], the exact number's units and scale.

### SEE ALSO

[Appendix A: Types](../appendix/a-types.md)

## Values read

### NAME

`trl_value`, `trl_kind`, `trl_value_kind`, `trl_value_is_null`,
`trl_value_text`, `trl_value_bytes`, `trl_value_decimal`,
`trl_value_double`, `trl_value_boolean`, `trl_value_datetime`,
`trl_value_interval`, `trl_value_decfloat`, `trl_value_count`,
`trl_value_at`, `trl_value_json_text`, `trl_value_uuid`: a value a
result holds, read by its kind.

### SYNOPSIS

```c
typedef struct trl_datetime {
    bool zoned;
    uint8_t precision;
    int16_t zone;
    int64_t utc;
} trl_datetime;

typedef struct trl_interval_qualifier {
    uint32_t start;
    uint32_t end;
    uint32_t leading;
    uint32_t fraction;
} trl_interval_qualifier;

typedef struct trl_interval {
    trl_interval_qualifier qualifier;
    trl_int128 units;
} trl_interval;

typedef struct trl_decfloat {
    trl_int128 coefficient;
    int32_t exponent;
} trl_decfloat;

trl_kind trl_value_kind(const trl_value *value);
bool trl_value_is_null(const trl_value *value);
const char *trl_value_text(const trl_value *value, size_t *length);
const uint8_t *trl_value_bytes(const trl_value *value, size_t *length);
trl_status trl_value_decimal(const trl_value *value, trl_decimal *out);
trl_status trl_value_double(const trl_value *value, double *out);
trl_status trl_value_boolean(const trl_value *value, bool *out);
trl_status trl_value_datetime(const trl_value *value, trl_datetime *out);
trl_status trl_value_interval(const trl_value *value, trl_interval *out);
trl_status trl_value_decfloat(const trl_value *value, trl_decfloat *out);
size_t trl_value_count(const trl_value *value);
const trl_value *trl_value_at(const trl_value *value, size_t index);
trl_status trl_value_json_text(const trl_value *value, char *out, size_t capacity, size_t *length);
trl_status trl_value_uuid(const trl_value *value, uint8_t out[TRL_UUID_BYTES]);
```

### DESCRIPTION

A value's kind, `trl_kind`, is its tag in the protocol, 0 null to 27
UUID [PROTOCOL Values]. Each function reads the kinds it names:

- `trl_value_text`: a character string, a character large object sent
  whole, an XML value's serialization, or a plug-in value's text, UTF-8
  and not ended by a null;
- `trl_value_bytes`: a binary string, a binary large object sent whole,
  a JSON value's stored form, a UUID's octets, or a plug-in value's
  bytes;
- `trl_value_decimal`, `trl_value_double`, `trl_value_boolean`,
  `trl_value_decfloat`: an exact number, an approximate one, its bits
  kept, a boolean, and a DECFLOAT, its coefficient times ten to its
  exponent;
- `trl_value_datetime`: a date, time or timestamp, its instant in
  microseconds in UTC from 0001-01-01 00:00:00, a date's its midnight's,
  a time's from midnight within a day; of a type WITH TIME ZONE, `zoned`
  and the zone's displacement in minutes;
- `trl_value_interval`: an interval, its units months of a year-month
  one and microseconds of a day-time one, and its qualifier, its fields
  0 YEAR to 5 SECOND;
- `trl_value_count` and `trl_value_at`: an array's or multiset's
  elements, a row's fields, a structured value's attributes, each a
  value, counted from 0;
- `trl_value_json_text`: a JSON value's JSON text, as turborel writes
  it, cut short to fit `out` and ended by a null, its whole length in
  `length`;
- `trl_value_uuid`: a UUID's 16 octets.

A value is valid as long as the result holding it.

### ERRORS

A function of a `trl_status` gives `TRL_INVALID_ARGUMENT` of a null
pointer or a value of another kind; `trl_value_json_text` also
`TRL_INVALID_TEXT` of a stored form no JSON text writes, and
`TRL_NO_MEMORY`. The others give null, 0 or false.

### EXAMPLES

[Running Queries](../user/03-queries.md) reads each kind.

## Values as text

### NAME

`trl_value_to_text`: a value written as Turborel displays it.

### SYNOPSIS

```c
trl_status trl_value_to_text(const trl_value *value, char *out, size_t capacity, size_t *length);
```

### DESCRIPTION

`trl_value_to_text` writes a value as Turborel's own client displays it,
as an export writes each value of CSV [CLIENT Copying and exporting]: a
string as itself; an exact number with its scale's digits, as 12.50; an
approximate one in its shortest digits with an exponent, as 7.5E0; a
DECFLOAT plain, or with an exponent where it is very large or small; a
truth value TRUE or FALSE; a datetime as its literal's string, in its
zone's local time, its fraction to its precision, its zone after it; an
interval as its literal's string; a binary string in upper-case
hexadecimal; a large object sent whole as its text or its octets in
hexadecimal, one by its locator as what it is, as
`<CHARACTER LARGE OBJECT of 70000 characters>`; a JSON value's text; an
XML value's serialization; a UUID's text; a plug-in value's text; an
array, row, multiset, structured value or reference as its literal,
each element as its own literal, as `ARRAY['it''s', FALSE]`; the null
value NULL. The text is ended by a null and cut short to fit
`capacity`; `length`, if not null, gets the whole text's bytes.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null value, or a null `out` with `capacity`
past 0; `TRL_NO_MEMORY`.

### EXAMPLES

[Copying, Importing and Exporting](../user/07-copying.md) exports values
as text.

## Data types

### NAME

`trl_data_type`, `trl_type_kind`, `trl_type_length`,
`trl_type_precision`, `trl_type_scale`, `trl_type_to_text`: a column's
or a parameter's type.

### SYNOPSIS

```c
trl_kind trl_type_kind(const trl_data_type *type);
uint64_t trl_type_length(const trl_data_type *type);
uint32_t trl_type_precision(const trl_data_type *type);
uint32_t trl_type_scale(const trl_data_type *type);
trl_status trl_type_to_text(const trl_data_type *type, char *out, size_t capacity, size_t *length);
```

### DESCRIPTION

A type's kind is that of its values, `TRL_KIND_EXACT` of DECIMAL and
NUMERIC alike [PROTOCOL Data types]. `trl_type_length` gives a
character, binary or large object type's length; `trl_type_precision`
an exact, approximate, datetime or DECFLOAT type's precision; and
`trl_type_scale` an exact type's scale; each gives 0 of a type that has
none.

`trl_type_to_text` writes a type as SQL writes it, as Turborel's own
client writes a type read from the protocol: by its tag and sizes, an
exact type NUMERIC(p,s) however it was declared, an approximate one
FLOAT(p), a string type with its length, OCTETS after that of a
character type counted in octets, an interval with its qualifier, an
array, multiset or row of its elements' or fields' types, a structured
type by its schema and name, a distinct type, which is sent as its
source type, as that type, a type of a plug-in UNREGISTERED
[PROTOCOL Data types]. The text is ended by a null and cut short to fit
`capacity`; `length`, if not null, gets the whole text's bytes. A
column's declared type, where the server tells it, is
`trl_result_column_type_written`'s.

### ERRORS

None of the sizes: each gives 0 of a null type. `trl_type_to_text`
gives `TRL_INVALID_ARGUMENT` of a null type, or of a null `out` with
`capacity` past 0, and `TRL_NO_MEMORY` if memory could not be had.

## Parameters

### NAME

`trl_params`, `trl_params_new`, `trl_params_add_null`,
`trl_params_add_text`, `trl_params_add_bytes`, `trl_params_add_decimal`,
`trl_params_add_double`, `trl_params_add_boolean`,
`trl_params_add_datetime`, `trl_params_add_interval`,
`trl_params_add_decfloat`, `trl_params_add_uuid`, `trl_params_add_json`,
`trl_params_add_xml`, `trl_params_add_value`, `trl_params_count`,
`trl_params_failure`, `trl_params_clear`, `trl_params_free`: the values a statement is
executed with.

### SYNOPSIS

```c
trl_status trl_params_new(trl_params **out);
trl_status trl_params_add_null(trl_params *params);
trl_status trl_params_add_text(trl_params *params, const char *text, size_t length);
trl_status trl_params_add_bytes(trl_params *params, const uint8_t *bytes, size_t length);
trl_status trl_params_add_decimal(trl_params *params, trl_decimal number);
trl_status trl_params_add_double(trl_params *params, double number);
trl_status trl_params_add_boolean(trl_params *params, bool truth);
trl_status trl_params_add_datetime(trl_params *params, trl_kind kind, trl_datetime datetime);
trl_status trl_params_add_interval(trl_params *params, trl_interval interval);
trl_status trl_params_add_decfloat(trl_params *params, trl_decfloat number);
trl_status trl_params_add_uuid(trl_params *params, const uint8_t octets[TRL_UUID_BYTES]);
trl_status trl_params_add_json(trl_params *params, const char *text, size_t length);
trl_status trl_params_add_xml(trl_params *params, const char *text, size_t length);
trl_status trl_params_add_value(trl_params *params, const trl_value *value);
size_t trl_params_count(const trl_params *params);
const trl_failure *trl_params_failure(const trl_params *params);
void trl_params_clear(trl_params *params);
void trl_params_free(trl_params *params);
```

### DESCRIPTION

A parameter list holds one execution's values, in the order of the
statement's `?`s, each copied as it is added, `trl_params_add_value` a
value read copied whole, arrays and rows with their elements
[CLIENT Prepared statements]. `trl_params_add_datetime` takes the kind
of datetime, `TRL_KIND_DATE` to `TRL_KIND_TIMESTAMP_ZONED`, a zoned one sent as a time or timestamp value, zoned.
`trl_params_add_json` makes the stored form of the JSON text given
[PROTOCOL JSON values]; `trl_params_add_xml` sends the serialization
as it is, which the server parses. The server casts each value to its
parameter's type, as SQL's assignment does. A list is executed with a
prepared statement by `trl_execute`, or with a statement run once by
`trl_query_values` ([Statements and Cursors](22-statements.md)).
`trl_params_clear` empties the list for the next execution.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer, of text that is not UTF-8, or
of a datetime of another kind; `TRL_NO_MEMORY`, the list as it was.
`trl_params_add_json` refuses a value as turborel does, with
`TRL_FAILED`, the list as it was and keeping the failure, which
`trl_params_failure` reads until the next such call: text that is not
JSON, SQLCODE -301 and SQLSTATE 22032; a number no type of turborel's
holds exactly, 22003; arrays and objects nested past 128, or a value
whose stored form passes 256 MiB, SQLCODE -900 and SQLSTATE 0A000.

### EXAMPLES

[Prepared Statements](../user/04-prepared-statements.md) adds them.

## UUIDs

### NAME

`trl_uuid_text`, `trl_uuid_parse`: a UUID's text and its octets.

### SYNOPSIS

```c
#define TRL_UUID_BYTES 16
#define TRL_UUID_TEXT_BYTES 37

trl_status trl_uuid_text(const uint8_t octets[TRL_UUID_BYTES], char out[TRL_UUID_TEXT_BYTES]);
trl_status trl_uuid_parse(const char *text, uint8_t out[TRL_UUID_BYTES]);
```

### DESCRIPTION

A UUID comes as its 16 octets, a value of kind `TRL_KIND_UUID`,
whatever types the program registered [CLIENT UUIDs].
`trl_uuid_text` writes them as 8-4-4-4-12 lower-case hexadecimal digits;
`trl_uuid_parse` reads 32 hexadecimal digits of either case, with
hyphens after the 8th, 12th, 16th and 20th, or none.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer; `TRL_INVALID_TEXT` of text of
no UUID.

### EXAMPLES

```c
uint8_t octets[TRL_UUID_BYTES];
char text[TRL_UUID_TEXT_BYTES];
if (trl_uuid_parse("919108F752D143209BACF847DB4148A8", octets) == TRL_OK && trl_uuid_text(octets, text) == TRL_OK) {
    printf("%s\n", text);
}
printf("%d\n", trl_uuid_parse("919108f7-52d1", octets) == TRL_INVALID_TEXT);
```

```
919108f7-52d1-4320-9bac-f847db4148a8
1
```

## XML nodes

### NAME

`trl_xml`, `trl_xml_node`, `trl_xml_kind`, `trl_xml_parse`,
`trl_value_xml`, `trl_xml_failure`, `trl_xml_root`, `trl_xml_node_kind`,
`trl_xml_node_name`, `trl_xml_node_namespace`, `trl_xml_node_prefix`,
`trl_xml_node_value`, `trl_xml_node_child_count`, `trl_xml_node_child`,
`trl_xml_node_attribute_count`, `trl_xml_node_attribute`,
`trl_xml_free`: an XML value's nodes.

### SYNOPSIS

```c
typedef enum trl_xml_kind {
    TRL_XML_NODE_DOCUMENT = 0,
    TRL_XML_NODE_ELEMENT,
    TRL_XML_NODE_ATTRIBUTE,
    TRL_XML_NODE_TEXT,
    TRL_XML_NODE_COMMENT,
    TRL_XML_NODE_PROCESSING_INSTRUCTION,
} trl_xml_kind;

trl_status trl_xml_parse(const char *text, size_t length, trl_xml **out);
trl_status trl_value_xml(const trl_value *value, trl_xml **out);
const trl_failure *trl_xml_failure(const trl_xml *xml);
const trl_xml_node *trl_xml_root(const trl_xml *xml);
trl_xml_kind trl_xml_node_kind(const trl_xml_node *node);
const char *trl_xml_node_name(const trl_xml_node *node);
const char *trl_xml_node_namespace(const trl_xml_node *node);
const char *trl_xml_node_prefix(const trl_xml_node *node);
const char *trl_xml_node_value(const trl_xml_node *node);
size_t trl_xml_node_child_count(const trl_xml_node *node);
const trl_xml_node *trl_xml_node_child(const trl_xml_node *node, size_t index);
size_t trl_xml_node_attribute_count(const trl_xml_node *node);
const trl_xml_node *trl_xml_node_attribute(const trl_xml_node *node, size_t index);
void trl_xml_free(trl_xml *xml);
```

### DESCRIPTION

An XML value comes as its serialization, as CONTENT, in XML 1.0 with no
XML declaration, or in XML 1.1 after one [CLIENT XML values].
`trl_value_xml` parses it, as `trl_xml_parse` parses UTF-8 text, as
well-formed XML content: an XML declaration, if any, then elements,
text, comments and processing instructions in any order. Line ends are
made line feeds, entity and character references replaced, CDATA
sections made text, and each prefix resolved to the namespace its
nearest declaration binds; namespace declarations are not among an
element's attributes. The tree's root is its document node; an
element's children and attributes come in document order. A tree is
the caller's, every string of it valid until `trl_xml_free`.

A tree nests at most 512 elements deep, and holds at most 2^20 nodes
and namespace bindings and 16 MiB of names and text, as Turborel's own
trees do. A document type declaration is not read.

### ERRORS

`TRL_INVALID_ARGUMENT` of a null pointer, or a value not XML;
`TRL_NO_MEMORY`, nothing made. `TRL_FAILED`, the tree made with no root
and holding the failure: SQLCODE -301 and SQLSTATE 2200N of text that
is not well-formed XML content, saying why; SQLCODE -900 and SQLSTATE
0A000 of a tree past the bounds above; SQLCODE -918 of memory not had.

### EXAMPLES

```c
const char text[] = "<order xmlns=\"urn:shop\" no=\"18\"><!-- rush --><item>jam &amp; tea</item></order>";
trl_xml *xml = NULL;
if (trl_xml_parse(text, sizeof text - 1, &xml) == TRL_OK) {
    const trl_xml_node *order = trl_xml_node_child(trl_xml_root(xml), 0);
    printf("%s in %s, no=%s\n", trl_xml_node_name(order), trl_xml_node_namespace(order),
           trl_xml_node_value(trl_xml_node_attribute(order, 0)));
    for (size_t i = 0; i < trl_xml_node_child_count(order); ++i) {
        const trl_xml_node *child = trl_xml_node_child(order, i);
        if (trl_xml_node_kind(child) == TRL_XML_NODE_COMMENT) {
            printf("comment:%s\n", trl_xml_node_value(child));
        } else {
            printf("%s: %s\n", trl_xml_node_name(child), trl_xml_node_value(trl_xml_node_child(child, 0)));
        }
    }
}
trl_xml_free(xml);
if (trl_xml_parse("<a><b></a>", 10, &xml) == TRL_FAILED) {
    char line[256];
    trl_failure_text(trl_xml_failure(xml), line, sizeof line);
    printf("%s\n", line);
}
trl_xml_free(xml);
```

```
order in urn:shop, no=18
comment: rush 
item: jam & tea
SQLCODE -301 (SQLSTATE 2200N): the string is not well-formed XML content: an end tag does not name the element it ends [XML §3 WFC: Element Type Match] [ISO/IEC 9075-14:2006 draft §10.16 GR6a ii]
```

### STANDARD

[PROTOCOL Values], [PROTOCOL Data types], [PROTOCOL JSON values],
[CLIENT Running statements], [CLIENT XML values], [CLIENT UUIDs]; the
XML of XML 1.0 and 1.1 and of Namespaces in XML; ISO/IEC 9075-14:2006
draft §10.16 GR6a ii for the SQLSTATE of content not well formed.

### SEE ALSO

[Statements and Cursors](22-statements.md),
[Appendix A: Types](../appendix/a-types.md)
