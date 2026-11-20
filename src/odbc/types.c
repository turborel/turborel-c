/**
 * @file types.c
 * @brief Turborel's types as ODBC's SQL types (RULES.md D2), by the name
 * a column's declared type has where the server tells it
 * [PROTOCOL Declared types], so INTEGER is SQL_INTEGER and not
 * NUMERIC(10,0), else by the type's tag: each type's SQL type, column
 * size, decimal digits, octet length and display size, as Appendix D of
 * ODBC's reference gives them. A type ODBC has no type of, as a datetime
 * with a time zone, an interval, DECFLOAT, JSON, XML and the composite
 * types, is a character string: its text.
 */
#include "odbc/types.h"

#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The digits of a SMALLINT, an INTEGER and a BIGINT; of REAL's and DOUBLE's
   mantissas as ODBC counts them. */
#define TRL_SMALLINT_DIGITS 5U
#define TRL_INTEGER_DIGITS 10U
#define TRL_BIGINT_DIGITS 19U
#define TRL_REAL_DIGITS 7U
#define TRL_DOUBLE_DIGITS 15U
/* The characters of a date, a time and a timestamp without fractions, of a
   UUID's text, and the display of each number of a fixed size, its sign
   and point among it. */
#define TRL_DATE_CHARACTERS 10U
#define TRL_TIME_CHARACTERS 8U
#define TRL_TIMESTAMP_CHARACTERS 19U
#define TRL_UUID_CHARACTERS 36U
#define TRL_SMALLINT_DISPLAY 6
#define TRL_INTEGER_DISPLAY 11
#define TRL_BIGINT_DISPLAY 20
#define TRL_REAL_DISPLAY 14
#define TRL_DOUBLE_DISPLAY 24
/* The bytes of a SMALLINT, an INTEGER, a BIGINT, a REAL and a DOUBLE; of
   ODBC's date, time and timestamp structures, and of a GUID. */
#define TRL_SMALLINT_BYTES 2
#define TRL_INTEGER_BYTES 4
#define TRL_BIGINT_BYTES 8
#define TRL_REAL_BYTES 4
#define TRL_DOUBLE_BYTES 8
#define TRL_DATE_BYTES 6
#define TRL_TIME_BYTES 6
#define TRL_TIMESTAMP_BYTES 16
#define TRL_GUID_BYTES 16
/* The characters a scalar shown as text, as an interval, is given room for. */
#define TRL_TEXT_CHARACTERS 255U
/* The hexadecimal digits each byte displays as. */
#define TRL_HEX_DIGITS 2U
/* The most bytes a character takes in UTF-8. */
#define TRL_UTF8_MOST 4U
/* What a length past what ODBC can say is told as. */
#define TRL_MOST_LENGTH 2147483647U
/* The precision an exact number has at most, and the radix of numbers. */
#define TRL_NUMERIC_MOST 38U
#define TRL_DECIMAL_RADIX 10
/* The characters of a sign and a point a number's display adds. */
#define TRL_SIGN_AND_POINT 2

/* A type known by its declared name. */
typedef struct by_name {
    const char *name;
    SQLSMALLINT sql_type;
} by_name;

/* The declared names ODBC has a type of. */
static const by_name names[] = {
    {"SMALLINT", SQL_SMALLINT},
    {"INTEGER", SQL_INTEGER},
    {"BIGINT", SQL_BIGINT},
    {"NUMERIC", SQL_NUMERIC},
    {"DECIMAL", SQL_DECIMAL},
    {"REAL", SQL_REAL},
    {"DOUBLE PRECISION", SQL_DOUBLE},
    {"FLOAT", SQL_FLOAT},
    {"CHARACTER", SQL_CHAR},
    {"CHARACTER VARYING", SQL_VARCHAR},
    {"CHARACTER LARGE OBJECT", SQL_LONGVARCHAR},
    {"BINARY", SQL_BINARY},
    {"BINARY VARYING", SQL_VARBINARY},
    {"BINARY LARGE OBJECT", SQL_LONGVARBINARY},
    {"BOOLEAN", SQL_BIT},
    {"DATE", SQL_TYPE_DATE},
    {"TIME", SQL_TYPE_TIME},
    {"TIMESTAMP", SQL_TYPE_TIMESTAMP},
    {"UUID", SQL_GUID},
};

/* Whether a type of `kind` is shown as long text: JSON, XML and the
   composite types, of no bound. */
static bool long_text(trl_kind kind) {
    return kind == TRL_KIND_JSON || kind == TRL_KIND_XML || kind == TRL_KIND_ARRAY || kind == TRL_KIND_ROW ||
           kind == TRL_KIND_MULTISET || kind == TRL_KIND_STRUCTURED;
}

/* The SQL type of each tag ODBC has a type of. */
static const struct {
    trl_kind kind;
    SQLSMALLINT sql_type;
} kinds[] = {
    {TRL_KIND_EXACT, SQL_NUMERIC},    {TRL_KIND_APPROXIMATE, SQL_DOUBLE},       {TRL_KIND_CHARACTER, SQL_CHAR},
    {TRL_KIND_BINARY, SQL_BINARY},    {TRL_KIND_BINARY_VARYING, SQL_VARBINARY}, {TRL_KIND_DATE, SQL_TYPE_DATE},
    {TRL_KIND_TIME, SQL_TYPE_TIME},   {TRL_KIND_TIMESTAMP, SQL_TYPE_TIMESTAMP}, {TRL_KIND_BOOLEAN, SQL_BIT},
    {TRL_KIND_CLOB, SQL_LONGVARCHAR}, {TRL_KIND_BLOB, SQL_LONGVARBINARY},       {TRL_KIND_UUID, SQL_GUID},
};

/* The SQL type of a type's tag, where its declared name is not told. */
static SQLSMALLINT of_kind(trl_kind kind) {
    if (long_text(kind)) {
        return SQL_LONGVARCHAR;
    }
    for (size_t i = 0U; i < sizeof kinds / sizeof kinds[0]; ++i) {
        if (kinds[i].kind == kind) {
            return kinds[i].sql_type;
        }
    }
    return SQL_VARCHAR;
}

SQLSMALLINT trl_odbc_sql_type(const trl_data_type *type, const char *declared) {
    for (size_t i = 0U; declared != NULL && i < sizeof names / sizeof names[0]; ++i) {
        if (strcmp(declared, names[i].name) == 0) {
            return names[i].sql_type;
        }
    }
    SQLSMALLINT kind_type = of_kind(trl_type_kind(type));
    /* A name ODBC has no type of is shown as text, of what length the tag says. */
    return declared == NULL || kind_type == SQL_LONGVARCHAR ? kind_type : SQL_VARCHAR;
}

/* A length as ODBC can say it. */
static SQLULEN bounded(uint64_t length) {
    return length == 0U || length > TRL_MOST_LENGTH ? TRL_MOST_LENGTH : (SQLULEN)length;
}

/* A number of a fixed size: its SQL type, digits, bytes and display. */
typedef struct fixed_number {
    SQLULEN size;
    SQLLEN bytes;
    SQLLEN display;
    SQLSMALLINT sql_type;
} fixed_number;

static const fixed_number fixed[] = {
    {TRL_SMALLINT_DIGITS, TRL_SMALLINT_BYTES, TRL_SMALLINT_DISPLAY, SQL_SMALLINT},
    {TRL_INTEGER_DIGITS, TRL_INTEGER_BYTES, TRL_INTEGER_DISPLAY, SQL_INTEGER},
    {TRL_BIGINT_DIGITS, TRL_BIGINT_BYTES, TRL_BIGINT_DISPLAY, SQL_BIGINT},
    {TRL_REAL_DIGITS, TRL_REAL_BYTES, TRL_REAL_DISPLAY, SQL_REAL},
    {TRL_DOUBLE_DIGITS, TRL_DOUBLE_BYTES, TRL_DOUBLE_DISPLAY, SQL_DOUBLE},
    {TRL_DOUBLE_DIGITS, TRL_DOUBLE_BYTES, TRL_DOUBLE_DISPLAY, SQL_FLOAT},
};

/* The size, digits and bytes of a number of `sql_type`. */
static bool numeric_sizes(SQLSMALLINT sql_type, const trl_data_type *type, trl_odbc_type *out) {
    for (size_t i = 0U; i < sizeof fixed / sizeof fixed[0]; ++i) {
        if (fixed[i].sql_type == sql_type) {
            out->column_size = fixed[i].size;
            out->octet_length = fixed[i].bytes;
            out->display_size = fixed[i].display;
            out->radix = TRL_DECIMAL_RADIX;
            return true;
        }
    }
    if (sql_type != SQL_NUMERIC && sql_type != SQL_DECIMAL) {
        return false;
    }
    uint32_t precision = trl_type_precision(type);
    out->column_size = precision == 0U ? TRL_NUMERIC_MOST : precision;
    out->digits = (SQLSMALLINT)trl_type_scale(type);
    out->octet_length = (SQLLEN)out->column_size + TRL_SIGN_AND_POINT;
    out->display_size = (SQLLEN)out->column_size + TRL_SIGN_AND_POINT;
    out->radix = TRL_DECIMAL_RADIX;
    return true;
}

/* The size, digits and bytes of a datetime of `sql_type`. */
static bool datetime_sizes(SQLSMALLINT sql_type, const trl_data_type *type, trl_odbc_type *out) {
    uint32_t fraction = trl_type_precision(type);
    if (sql_type == SQL_TYPE_DATE) {
        out->column_size = TRL_DATE_CHARACTERS;
        out->octet_length = TRL_DATE_BYTES;
        out->interval_code = SQL_CODE_DATE;
    } else if (sql_type == SQL_TYPE_TIME) {
        out->column_size = TRL_TIME_CHARACTERS + (fraction > 0U ? fraction + 1U : 0U);
        out->octet_length = TRL_TIME_BYTES;
        out->interval_code = SQL_CODE_TIME;
        out->digits = (SQLSMALLINT)fraction;
    } else if (sql_type == SQL_TYPE_TIMESTAMP) {
        out->column_size = TRL_TIMESTAMP_CHARACTERS + (fraction > 0U ? fraction + 1U : 0U);
        out->octet_length = TRL_TIMESTAMP_BYTES;
        out->interval_code = SQL_CODE_TIMESTAMP;
        out->digits = (SQLSMALLINT)fraction;
    } else {
        return false;
    }
    out->verbose_type = SQL_DATETIME;
    out->display_size = (SQLLEN)out->column_size;
    return true;
}

/* The size and bytes of a string, a truth value or a UUID of `sql_type`. */
static void string_sizes(SQLSMALLINT sql_type, const trl_data_type *type, trl_odbc_type *out) {
    if (sql_type == SQL_BIT) {
        out->column_size = 1U;
        out->octet_length = 1;
        out->display_size = 1;
        return;
    }
    if (sql_type == SQL_GUID) {
        out->column_size = TRL_UUID_CHARACTERS;
        out->octet_length = TRL_GUID_BYTES;
        out->display_size = TRL_UUID_CHARACTERS;
        return;
    }
    bool binary = sql_type == SQL_BINARY || sql_type == SQL_VARBINARY || sql_type == SQL_LONGVARBINARY;
    trl_kind kind = trl_type_kind(type);
    bool string = binary || kind == TRL_KIND_CHARACTER || kind == TRL_KIND_CHARACTER_VARYING || kind == TRL_KIND_CLOB;
    out->column_size = string || long_text(kind) ? bounded(trl_type_length(type)) : TRL_TEXT_CHARACTERS;
    out->text = !binary;
    out->octet_length = binary || out->column_size > TRL_MOST_LENGTH / TRL_UTF8_MOST
                            ? (SQLLEN)out->column_size
                            : (SQLLEN)(out->column_size * TRL_UTF8_MOST);
    /* Each byte of a binary string displays as two hexadecimal digits. */
    out->display_size = binary && out->column_size <= TRL_MOST_LENGTH / TRL_HEX_DIGITS
                            ? (SQLLEN)(out->column_size * TRL_HEX_DIGITS)
                            : (SQLLEN)out->column_size;
}

void trl_odbc_type_of(const trl_data_type *type, const char *declared, trl_odbc_type *out) {
    memset(out, 0, sizeof *out);
    SQLSMALLINT sql_type = trl_odbc_sql_type(type, declared);
    out->sql_type = sql_type;
    out->verbose_type = sql_type;
    if (!numeric_sizes(sql_type, type, out) && !datetime_sizes(sql_type, type, out)) {
        string_sizes(sql_type, type, out);
    }
}

const char *trl_odbc_type_name(SQLSMALLINT sql_type) {
    for (size_t i = 0U; i < sizeof names / sizeof names[0]; ++i) {
        if (names[i].sql_type == sql_type) {
            return names[i].name;
        }
    }
    return "CHARACTER VARYING";
}
