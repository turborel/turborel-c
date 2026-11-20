/**
 * @file values.h
 * @brief Values [CLIENT Running statements] [PROTOCOL Values]: those a
 * result holds, read by their kind, and those a program sends as a
 * prepared statement's parameters; the types of columns and parameters;
 * and UUIDs' octets and text [CLIENT UUIDs].
 *
 * A value a result holds is borrowed: valid while its result, or batch,
 * is. A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_VALUES_H
#define TRL_VALUES_H

#include "turborel/base.h"
#include "turborel/numbers.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The kind of a value or of a type: the protocol's tag of it [PROTOCOL Values]. */
typedef enum trl_kind {
    /** @brief The null value. */
    TRL_KIND_NULL = 0,
    /** @brief A character string. */
    TRL_KIND_CHARACTER = 1,
    /** @brief An exact number, a trl_decimal. */
    TRL_KIND_EXACT = 2,
    /** @brief An approximate number, a double. */
    TRL_KIND_APPROXIMATE = 3,
    /** @brief A plug-in type's value: its type's name, its text and its bytes. */
    TRL_KIND_PLUGIN = 4,
    /** @brief CHARACTER VARYING, a type's kind; its values come as TRL_KIND_CHARACTER. */
    TRL_KIND_CHARACTER_VARYING = 5,
    /** @brief A binary string. */
    TRL_KIND_BINARY = 6,
    /** @brief BINARY VARYING, a type's kind; its values come as TRL_KIND_BINARY. */
    TRL_KIND_BINARY_VARYING = 7,
    /** @brief A date. */
    TRL_KIND_DATE = 8,
    /** @brief A time. */
    TRL_KIND_TIME = 9,
    /** @brief TIME WITH TIME ZONE, a type's kind; its values come as TRL_KIND_TIME, zoned. */
    TRL_KIND_TIME_ZONED = 10,
    /** @brief A timestamp. */
    TRL_KIND_TIMESTAMP = 11,
    /** @brief TIMESTAMP WITH TIME ZONE, a type's kind; its values come as TRL_KIND_TIMESTAMP, zoned. */
    TRL_KIND_TIMESTAMP_ZONED = 12,
    /** @brief An interval. */
    TRL_KIND_INTERVAL = 13,
    /** @brief A boolean. */
    TRL_KIND_BOOLEAN = 14,
    /** @brief A decimal floating-point number. */
    TRL_KIND_DECFLOAT = 15,
    /** @brief An array, its elements. */
    TRL_KIND_ARRAY = 16,
    /** @brief A row, its fields. */
    TRL_KIND_ROW = 17,
    /** @brief A character large object, of a type. */
    TRL_KIND_CLOB = 18,
    /** @brief A binary large object, of a type. */
    TRL_KIND_BLOB = 19,
    /** @brief A large object sent whole. */
    TRL_KIND_LOB = 20,
    /** @brief A large object's locator. */
    TRL_KIND_LOB_LOCATOR = 21,
    /** @brief A multiset, its elements. */
    TRL_KIND_MULTISET = 22,
    /** @brief A value of a structured type, its attributes. */
    TRL_KIND_STRUCTURED = 23,
    /** @brief A reference, of a type. */
    TRL_KIND_REFERENCE = 24,
    /** @brief A JSON value, in its stored form. */
    TRL_KIND_JSON = 25,
    /** @brief An XML value, its serialization. */
    TRL_KIND_XML = 26,
    /** @brief A UUID, its 16 octets. */
    TRL_KIND_UUID = 27,
} trl_kind;

/** @brief A value; read only through its functions. */
typedef struct trl_value trl_value;

/** @brief A data type, of a column or a parameter; read only through its functions. */
typedef struct trl_data_type trl_data_type;

/**
 * @brief A date, a time or a timestamp: its instant, microseconds in UTC
 * from 0001-01-01 00:00:00, of a date its midnight's, of a time from
 * midnight within a day [PROTOCOL Values].
 */
typedef struct trl_datetime {
    /** @brief Whether it is of a type WITH TIME ZONE. */
    bool zoned;
    /** @brief Its fractional seconds' precision. */
    uint8_t precision;
    /** @brief The zone's displacement from UTC, in minutes. */
    int16_t zone;
    /** @brief The instant. */
    int64_t utc;
} trl_datetime;

/** @brief An interval's qualifier: its fields, 0 YEAR to 5 SECOND, and precisions. */
typedef struct trl_interval_qualifier {
    /** @brief Its first field. */
    uint32_t start;
    /** @brief Its last field. */
    uint32_t end;
    /** @brief Its leading field's precision. */
    uint32_t leading;
    /** @brief Its fractional seconds' precision. */
    uint32_t fraction;
} trl_interval_qualifier;

/** @brief An interval: months of a year-month one, microseconds of a day-time one. */
typedef struct trl_interval {
    /** @brief Its qualifier. */
    trl_interval_qualifier qualifier;
    /** @brief Its units. */
    trl_int128 units;
} trl_interval;

/** @brief A decimal floating-point number: its coefficient times ten to its exponent. */
typedef struct trl_decfloat {
    /** @brief Its coefficient. */
    trl_int128 coefficient;
    /** @brief Its exponent. */
    int32_t exponent;
} trl_decfloat;

/** @brief The bytes of a UUID's text, its null included. */
#define TRL_UUID_TEXT_BYTES 37
/** @brief The octets of a UUID. */
#define TRL_UUID_BYTES 16

/**
 * @brief The value's kind.
 *
 * @param value The value; null gives TRL_KIND_NULL.
 * @return Its kind.
 */
TRL_API trl_kind trl_value_kind(const trl_value *value);

/**
 * @brief Whether the value is null.
 *
 * @param value The value; null gives true.
 * @return Whether it is.
 */
TRL_API bool trl_value_is_null(const trl_value *value);

/**
 * @brief The text of a character string, a character large object sent
 * whole, an XML value's serialization, or a plug-in value's text: UTF-8,
 * not ended by a null.
 *
 * @param value The value.
 * @param length Where its bytes go; may be null.
 * @return Its bytes, borrowed; null for a value of another kind.
 */
TRL_API const char *trl_value_text(const trl_value *value, size_t *length);

/**
 * @brief The bytes of a binary string, a binary large object sent whole, a
 * JSON value's stored form, a UUID's octets, or a plug-in value's bytes.
 *
 * @param value The value.
 * @param length Where their count goes; may be null.
 * @return The bytes, borrowed; null for a value of another kind.
 */
TRL_API const uint8_t *trl_value_bytes(const trl_value *value, size_t *length);

/**
 * @brief An exact number.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_decimal(const trl_value *value, trl_decimal *out);

/**
 * @brief An approximate number, its bits kept.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_double(const trl_value *value, double *out);

/**
 * @brief A boolean.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_boolean(const trl_value *value, bool *out);

/**
 * @brief A date, a time or a timestamp.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_datetime(const trl_value *value, trl_datetime *out);

/**
 * @brief An interval.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_interval(const trl_value *value, trl_interval *out);

/**
 * @brief A decimal floating-point number.
 *
 * @param value The value.
 * @param out Where it goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_decfloat(const trl_value *value, trl_decfloat *out);

/**
 * @brief How many elements an array or a multiset has, fields a row, or
 * attributes a structured value.
 *
 * @param value The value; another kind gives 0.
 * @return The count.
 */
TRL_API size_t trl_value_count(const trl_value *value);

/**
 * @brief An element, field or attribute, by its place.
 *
 * @param value The value.
 * @param index Which, from 0.
 * @return It, borrowed; null past the last, or of another kind.
 */
TRL_API const trl_value *trl_value_at(const trl_value *value, size_t index);

/**
 * @brief A JSON value as JSON text, as turborel writes it.
 *
 * @param value A JSON value.
 * @param out Where the text goes, ended by a null, cut short to fit; may
 * be null if `capacity` is 0.
 * @param capacity The bytes `out` holds.
 * @param length Where the bytes of the whole text go, its null not
 * counted, so a longer buffer can be had; may be null.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null value or another kind;
 * TRL_INVALID_TEXT of a value no JSON text writes, as a datetime;
 * TRL_NO_MEMORY.
 */
TRL_API trl_status trl_value_json_text(const trl_value *value, char *out, size_t capacity, size_t *length);

/**
 * @brief A value as turborel displays it: a string as itself; an exact
 * number, an approximate one and a DECFLOAT as turborel writes them; a
 * truth value TRUE or FALSE; a datetime and an interval as their
 * literals' strings, in the datetime's zone; a binary string in
 * upper-case hexadecimal; a large object's text, or its octets in
 * hexadecimal, or, by its locator, what it is; a JSON value's text; an
 * XML value's serialization; a UUID's text; a plug-in value's text; an
 * array, row, multiset, structured value or reference as its literal;
 * the null value NULL.
 *
 * @param value The value.
 * @param out Where the text goes, ended by a null, cut short to fit; may
 * be null if `capacity` is 0.
 * @param capacity The bytes `out` holds.
 * @param length Where the bytes of the whole text go, its null not
 * counted; may be null.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null value; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_value_to_text(const trl_value *value, char *out, size_t capacity, size_t *length);

/**
 * @brief A UUID's octets.
 *
 * @param value A UUID.
 * @param out Where its 16 octets go.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind.
 */
TRL_API trl_status trl_value_uuid(const trl_value *value, uint8_t out[TRL_UUID_BYTES]);

/**
 * @brief A UUID's text, as 8-4-4-4-12 lower-case hexadecimal digits.
 *
 * @param octets Its 16 octets.
 * @param out Where the text goes, TRL_UUID_TEXT_BYTES of it, null-ended.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer.
 */
TRL_API trl_status trl_uuid_text(const uint8_t octets[TRL_UUID_BYTES], char out[TRL_UUID_TEXT_BYTES]);

/**
 * @brief A UUID's octets of its text: 32 hexadecimal digits, in either
 * case, hyphens after the 8th, 12th, 16th and 20th or none.
 *
 * @param text The text, null-ended.
 * @param out Where its 16 octets go.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer;
 * TRL_INVALID_TEXT of text of no UUID.
 */
TRL_API trl_status trl_uuid_parse(const char *text, uint8_t out[TRL_UUID_BYTES]);

/**
 * @brief A data type's kind: of the protocol's types, those of the values'
 * kinds, as TRL_KIND_EXACT of DECIMAL and NUMERIC.
 *
 * @param type The type; null gives TRL_KIND_NULL.
 * @return Its kind.
 */
TRL_API trl_kind trl_type_kind(const trl_data_type *type);

/**
 * @brief A type's length: of a character or binary string or a large
 * object, its maximum.
 *
 * @param type The type; another kind gives 0.
 * @return The length.
 */
TRL_API uint64_t trl_type_length(const trl_data_type *type);

/**
 * @brief A type's precision: of an exact number, its digits; of an
 * approximate number or a datetime, its precision.
 *
 * @param type The type; another kind gives 0.
 * @return The precision.
 */
TRL_API uint32_t trl_type_precision(const trl_data_type *type);

/**
 * @brief An exact number's scale.
 *
 * @param type The type; another kind gives 0.
 * @return The scale.
 */
TRL_API uint32_t trl_type_scale(const trl_data_type *type);

/** @brief Values sent as a prepared statement's parameters, in order. */
typedef struct trl_params trl_params;

/**
 * @brief Makes an empty list of parameters.
 *
 * @param out Where the list goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_new(trl_params **out);

/**
 * @brief Adds the null value.
 *
 * @param params The list.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_null(trl_params *params);

/**
 * @brief Adds a character string, UTF-8, copied.
 *
 * @param params The list.
 * @param text The text.
 * @param length Its bytes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or text not
 * UTF-8; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_text(trl_params *params, const char *text, size_t length);

/**
 * @brief Adds a binary string, copied.
 *
 * @param params The list.
 * @param bytes The bytes; may be null if `length` is 0.
 * @param length Their count.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_bytes(trl_params *params, const uint8_t *bytes, size_t length);

/**
 * @brief Adds an exact number.
 *
 * @param params The list.
 * @param number The number.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_decimal(trl_params *params, trl_decimal number);

/**
 * @brief Adds an approximate number.
 *
 * @param params The list.
 * @param number The number.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_double(trl_params *params, double number);

/**
 * @brief Adds a boolean.
 *
 * @param params The list.
 * @param truth The boolean.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_boolean(trl_params *params, bool truth);

/**
 * @brief Adds a date, a time or a timestamp, of `kind`.
 *
 * @param params The list.
 * @param kind TRL_KIND_DATE, TRL_KIND_TIME, TRL_KIND_TIME_ZONED,
 * TRL_KIND_TIMESTAMP or TRL_KIND_TIMESTAMP_ZONED.
 * @param datetime Its instant.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or another kind;
 * TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_datetime(trl_params *params, trl_kind kind, trl_datetime datetime);

/**
 * @brief Adds an interval.
 *
 * @param params The list.
 * @param interval The interval.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_interval(trl_params *params, trl_interval interval);

/**
 * @brief Adds a decimal floating-point number, as turborel makes one of it.
 *
 * @param params The list.
 * @param number The number.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or a number out of
 * DECFLOAT's range; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_decfloat(trl_params *params, trl_decfloat number);

/**
 * @brief Adds a UUID of its octets.
 *
 * @param params The list.
 * @param octets Its 16 octets.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_uuid(trl_params *params, const uint8_t octets[TRL_UUID_BYTES]);

/**
 * @brief Adds a JSON value of its JSON text, read as turborel reads it.
 *
 * @param params The list.
 * @param text The text, UTF-8.
 * @param length Its bytes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer;
 * TRL_INVALID_TEXT of text no JSON value is; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_json(trl_params *params, const char *text, size_t length);

/**
 * @brief Adds an XML value of its serialization, UTF-8.
 *
 * @param params The list.
 * @param text The serialization.
 * @param length Its bytes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer or text not
 * UTF-8; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_xml(trl_params *params, const char *text, size_t length);

/**
 * @brief Adds a copy of a value a result holds, of any kind.
 *
 * @param params The list.
 * @param value The value.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_params_add_value(trl_params *params, const trl_value *value);

/**
 * @brief How many values the list holds.
 *
 * @param params The list; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_params_count(const trl_params *params);

/**
 * @brief Empties the list, for the next execution.
 *
 * @param params The list; null does nothing.
 */
TRL_API void trl_params_clear(trl_params *params);

/**
 * @brief Frees a list.
 *
 * @param params The list; null does nothing.
 */
TRL_API void trl_params_free(trl_params *params);

#ifdef __cplusplus
}
#endif

#endif
