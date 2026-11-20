/**
 * @file values.c
 * @brief Values read by their kind [CLIENT Running statements], types'
 * kinds and sizes, and UUIDs' octets and text [CLIENT UUIDs].
 */
#include "codec/values.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The places of a UUID's text where a hyphen is, and its digits' bits. */
#define TRL_UUID_DIGITS 32U
#define TRL_NIBBLE_BITS 4U
/* A UUID's hexadecimal digits to each of its octets. */
#define TRL_DIGITS_PER_OCTET 2U
#define TRL_NIBBLE_MASK 0x0fU
#define TRL_HEX_LETTER 10U

trl_kind trl_value_kind(const trl_value *value) {
    return value == NULL ? TRL_KIND_NULL : (trl_kind)value->tag;
}

bool trl_value_is_null(const trl_value *value) {
    return value == NULL || value->tag == TRL_TAG_NULL;
}

/* Gives `slice` as a text's bytes, and its length. */
static const uint8_t *give(trl_slice slice, size_t *length) {
    if (length != NULL) {
        *length = slice.length;
    }
    /* A value of no bytes still gives a place, so null means another kind. */
    return slice.bytes == NULL ? (const uint8_t *)"" : slice.bytes;
}

const char *trl_value_text(const trl_value *value, size_t *length) {
    if (value == NULL) {
        return NULL;
    }
    switch (value->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_XML:
        return (const char *)give(value->as.bytes, length);
    case TRL_TAG_PLUGIN:
        return (const char *)give(value->as.plugin.text, length);
    case TRL_TAG_CLOB:
    case TRL_TAG_LOB_WHOLE:
        return value->as.lob.kind == TRL_LOB_CHARACTER ? (const char *)give(value->as.lob.octets, length) : NULL;
    default:
        return NULL;
    }
}

const uint8_t *trl_value_bytes(const trl_value *value, size_t *length) {
    if (value == NULL) {
        return NULL;
    }
    switch (value->tag) {
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
    case TRL_TAG_JSON:
    case TRL_TAG_UUID:
        return give(value->as.bytes, length);
    case TRL_TAG_BLOB:
    case TRL_TAG_LOB_WHOLE:
        return value->as.lob.kind == TRL_LOB_BINARY ? give(value->as.lob.octets, length) : NULL;
    case TRL_TAG_PLUGIN:
        return give(value->as.plugin.bytes, length);
    default:
        return NULL;
    }
}

/* Whether `value` is of `tag`, and `out` given. */
static bool of(const trl_value *value, uint8_t tag, const void *out) {
    return value != NULL && out != NULL && value->tag == tag;
}

trl_status trl_value_decimal(const trl_value *value, trl_decimal *out) {
    if (!of(value, TRL_TAG_EXACT, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = value->as.exact;
    return TRL_OK;
}

trl_status trl_value_double(const trl_value *value, double *out) {
    if (!of(value, TRL_TAG_APPROXIMATE, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    memcpy(out, &value->as.approximate, sizeof *out);
    return TRL_OK;
}

trl_status trl_value_boolean(const trl_value *value, bool *out) {
    if (!of(value, TRL_TAG_BOOLEAN, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = value->as.truth;
    return TRL_OK;
}

/* Whether `tag` is a date's, a time's or a timestamp's. */
static bool is_datetime(uint8_t tag) {
    return tag >= TRL_TAG_DATE && tag <= TRL_TAG_TIMESTAMP_ZONED;
}

trl_status trl_value_datetime(const trl_value *value, trl_datetime *out) {
    if (value == NULL || out == NULL || !is_datetime(value->tag)) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = value->as.datetime;
    return TRL_OK;
}

trl_status trl_value_interval(const trl_value *value, trl_interval *out) {
    if (!of(value, TRL_TAG_INTERVAL, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = value->as.interval;
    return TRL_OK;
}

trl_status trl_value_decfloat(const trl_value *value, trl_decfloat *out) {
    if (!of(value, TRL_TAG_DECFLOAT, out)) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = value->as.decfloat;
    return TRL_OK;
}

/* The items of a value of items, and their count; null of another kind. */
static const trl_value *items_of(const trl_value *value, size_t *count) {
    *count = 0U;
    if (value == NULL) {
        return NULL;
    }
    switch (value->tag) {
    case TRL_TAG_ARRAY:
    case TRL_TAG_ROW:
    case TRL_TAG_MULTISET:
        *count = value->as.list.count;
        return value->as.list.items;
    case TRL_TAG_STRUCTURED:
        *count = value->as.structured.count;
        return value->as.structured.attributes;
    default:
        return NULL;
    }
}

size_t trl_value_count(const trl_value *value) {
    size_t count = 0U;
    (void)items_of(value, &count);
    return count;
}

const trl_value *trl_value_at(const trl_value *value, size_t index) {
    size_t count = 0U;
    const trl_value *items = items_of(value, &count);
    return index < count ? &items[index] : NULL;
}

trl_status trl_value_json_text(const trl_value *value, char *out, size_t capacity, size_t *length) {
    if (value == NULL || value->tag != TRL_TAG_JSON || (out == NULL && capacity > 0U)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    trl_codec_error error = {0, "", ""};
    if (!trl_json_to_text(&text, value->as.bytes.bytes, value->as.bytes.length, &error)) {
        trl_buffer_free(&text);
        return error.sqlcode == TRL_SQLCODE_RESOURCES ? TRL_NO_MEMORY : TRL_INVALID_TEXT;
    }
    if (length != NULL) {
        *length = text.length;
    }
    if (capacity > 0U) {
        size_t kept = text.length < capacity - 1U ? text.length : capacity - 1U;
        if (kept > 0U) {
            memcpy(out, text.data, kept);
        }
        out[kept] = '\0';
    }
    trl_buffer_free(&text);
    return TRL_OK;
}

trl_status trl_value_uuid(const trl_value *value, uint8_t out[TRL_UUID_BYTES]) {
    if (!of(value, TRL_TAG_UUID, out) || value->as.bytes.length != TRL_UUID_BYTES) {
        return TRL_INVALID_ARGUMENT;
    }
    memcpy(out, value->as.bytes.bytes, TRL_UUID_BYTES);
    return TRL_OK;
}

/* Whether a hyphen comes before the digit at `digit` of a UUID's text. */
static bool hyphen_before(size_t digit) {
    static const size_t hyphens[] = {8U, 12U, 16U, 20U};
    for (size_t i = 0U; i < sizeof hyphens / sizeof hyphens[0]; ++i) {
        if (digit == hyphens[i]) {
            return true;
        }
    }
    return false;
}

trl_status trl_uuid_text(const uint8_t octets[TRL_UUID_BYTES], char out[TRL_UUID_TEXT_BYTES]) {
    static const char digits[] = "0123456789abcdef";
    if (octets == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    size_t at = 0U;
    for (size_t digit = 0U; digit < TRL_UUID_DIGITS; ++digit) {
        if (hyphen_before(digit)) {
            out[at++] = '-';
        }
        uint8_t octet = octets[digit / TRL_DIGITS_PER_OCTET];
        unsigned int shifted = digit % TRL_DIGITS_PER_OCTET == 0U ? (unsigned int)octet >> TRL_NIBBLE_BITS : octet;
        out[at++] = digits[shifted & TRL_NIBBLE_MASK];
    }
    out[at] = '\0';
    return TRL_OK;
}

/* The value of a hexadecimal digit, or 16 if `c` is none. */
static unsigned int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return (unsigned int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (unsigned int)(c - 'a') + TRL_HEX_LETTER;
    }
    if (c >= 'A' && c <= 'F') {
        return (unsigned int)(c - 'A') + TRL_HEX_LETTER;
    }
    return TRL_NIBBLE_MASK + 1U;
}

trl_status trl_uuid_parse(const char *text, uint8_t out[TRL_UUID_BYTES]) {
    if (text == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    size_t length = strlen(text);
    bool hyphens = length == TRL_UUID_TEXT_BYTES - 1U;
    if (!hyphens && length != TRL_UUID_DIGITS) {
        return TRL_INVALID_TEXT;
    }
    uint8_t octets[TRL_UUID_BYTES] = {0};
    size_t at = 0U;
    for (size_t digit = 0U; digit < TRL_UUID_DIGITS; ++digit) {
        if (hyphens && hyphen_before(digit) && text[at++] != '-') {
            return TRL_INVALID_TEXT;
        }
        unsigned int nibble = hex_value(text[at++]);
        if (nibble > TRL_NIBBLE_MASK) {
            return TRL_INVALID_TEXT;
        }
        unsigned int placed = digit % TRL_DIGITS_PER_OCTET == 0U ? nibble << TRL_NIBBLE_BITS : nibble;
        octets[digit / TRL_DIGITS_PER_OCTET] = (uint8_t)(octets[digit / TRL_DIGITS_PER_OCTET] | placed);
    }
    memcpy(out, octets, sizeof octets);
    return TRL_OK;
}

trl_kind trl_type_kind(const trl_data_type *type) {
    return type == NULL ? TRL_KIND_NULL : (trl_kind)type->tag;
}

uint64_t trl_type_length(const trl_data_type *type) {
    if (type == NULL) {
        return 0U;
    }
    switch (type->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
        return type->as.character.length;
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
        return type->as.length;
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        return type->as.lob.length;
    default:
        return 0U;
    }
}

uint32_t trl_type_precision(const trl_data_type *type) {
    if (type == NULL) {
        return 0U;
    }
    switch (type->tag) {
    case TRL_TAG_EXACT:
        return type->as.exact.precision;
    case TRL_TAG_APPROXIMATE:
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIMESTAMP_ZONED:
    case TRL_TAG_DECFLOAT:
        return type->as.precision;
    default:
        return 0U;
    }
}

uint32_t trl_type_scale(const trl_data_type *type) {
    return type != NULL && type->tag == TRL_TAG_EXACT ? type->as.exact.scale : 0U;
}
