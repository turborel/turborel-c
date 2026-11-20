# Values

[Contents](README.md)

This page will describe the values the low-level client gives and takes
for each of the protocol's types, among them exact numbers, datetimes
and intervals, arrays, rows, multisets and structured values, JSON, XML
and UUIDs; Appendix A maps each to its C type. It is filled
at milestone M1, and completed at M3.

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
