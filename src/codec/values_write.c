/**
 * @file values_write.c
 * @brief Values written into a request [PROTOCOL Values], as
 * turborel-protocol's `fields.rs` writes them, refusing first what a
 * server would refuse, so a request is never sent malformed.
 */
#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/reader.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/utf8.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A value the program gave that cannot be sent: SQLCODE -910, as turborel
   gives a call made wrongly. */
static bool refuse(trl_codec_error *error, const char *why) {
    trl_codec_fail(error, TRL_SQLCODE_API, TRL_SQLSTATE_API, why);
    return false;
}

/* Writes a string, refused unless UTF-8. */
static bool write_string(trl_buffer *buffer, trl_slice text, trl_codec_error *error) {
    if (!trl_utf8_valid(text.bytes, text.length)) {
        return refuse(error, "a string to send is not UTF-8");
    }
    trl_put_bytes(buffer, text);
    return true;
}

/* Writes a datetime's body. */
static void write_datetime(trl_buffer *buffer, trl_datetime datetime) {
    trl_put_flag(buffer, datetime.zoned);
    trl_put_u8(buffer, datetime.precision);
    trl_put_u32(buffer, (uint16_t)datetime.zone);
    trl_put_u64(buffer, (uint64_t)datetime.utc);
}

/* Writes an interval: its qualifier, then its units. */
static bool write_interval(trl_buffer *buffer, const trl_interval *interval, trl_codec_error *error) {
    const trl_interval_qualifier *q = &interval->qualifier;
    if (q->start > TRL_FIELD_SECOND || q->end > TRL_FIELD_SECOND) {
        return refuse(error, "an interval's field is out of range");
    }
    trl_put_u32(buffer, q->start);
    trl_put_u32(buffer, q->end);
    trl_put_u32(buffer, q->leading);
    trl_put_u32(buffer, q->fraction);
    trl_put_i128(buffer, interval->units);
    return true;
}

/* Writes a DECFLOAT, refused unless canonical, as a server reads one. */
static bool write_decfloat(trl_buffer *buffer, trl_decfloat value, trl_codec_error *error) {
    if (!trl_decfloat_canonical(value)) {
        return refuse(error, "a DECFLOAT to send is not canonical or is out of range");
    }
    trl_put_i128(buffer, value.coefficient);
    trl_put_u32(buffer, (uint32_t)value.exponent);
    return true;
}

/* Writes a large object held whole; a character one's octets are UTF-8. */
static bool write_lob(trl_buffer *buffer, const trl_value *value, trl_codec_error *error) {
    if (value->as.lob.kind > TRL_LOB_BINARY) {
        return refuse(error, "not a kind of large object");
    }
    if (value->as.lob.kind == TRL_LOB_CHARACTER &&
        !trl_utf8_valid(value->as.lob.octets.bytes, value->as.lob.octets.length)) {
        return refuse(error, "a character large object to send is not UTF-8");
    }
    trl_put_u8(buffer, value->as.lob.kind);
    trl_put_bytes(buffer, value->as.lob.octets);
    return true;
}

/* Writes a large object under its type's tag: form 0, then its octets,
   a CLOB's UTF-8. */
static bool write_lob_typed(trl_buffer *buffer, const trl_value *value, trl_codec_error *error) {
    if (value->tag == TRL_TAG_CLOB && !trl_utf8_valid(value->as.lob.octets.bytes, value->as.lob.octets.length)) {
        return refuse(error, "a character large object to send is not UTF-8");
    }
    trl_put_u8(buffer, 0U);
    trl_put_bytes(buffer, value->as.lob.octets);
    return true;
}

/* Writes a large object by its locator. */
static bool write_locator(trl_buffer *buffer, const trl_value *value, trl_codec_error *error) {
    if (value->as.locator.kind > TRL_LOB_BINARY) {
        return refuse(error, "not a kind of large object");
    }
    trl_put_u8(buffer, value->as.locator.kind);
    trl_put_u32(buffer, value->as.locator.locator);
    trl_put_u64(buffer, value->as.locator.octets);
    trl_put_u64(buffer, value->as.locator.characters);
    return true;
}

static bool write_value(trl_buffer *buffer, const trl_value *value, trl_codec_error *error, unsigned int depth);

/* Writes a list of values, one level deeper. */
static bool write_list(trl_buffer *buffer, const trl_value *items, uint32_t count, trl_codec_error *error,
                       unsigned int depth) {
    if (depth >= TRL_MAX_NESTING) {
        return refuse(error, "a value's arrays and rows are nested too deep");
    }
    if (count > 0U && items == NULL) {
        return refuse(error, "a list of values has none");
    }
    trl_put_count(buffer, count);
    for (uint32_t i = 0U; i < count; ++i) {
        if (!write_value(buffer, &items[i], error, depth + 1U)) {
            return false;
        }
    }
    return true;
}

/* Writes the constructed values: arrays, rows, multisets and structured values. */
static bool write_constructed(trl_buffer *buffer, const trl_value *value, trl_codec_error *error, unsigned int depth) {
    switch (value->tag) {
    case TRL_TAG_ARRAY:
    case TRL_TAG_ROW:
        return write_list(buffer, value->as.list.items, value->as.list.count, error, depth);
    case TRL_TAG_MULTISET:
        if (value->as.list.count > TRL_MAX_MULTISET_CARDINALITY) {
            return refuse(error, "a multiset has more elements than its maximum cardinality");
        }
        return write_list(buffer, value->as.list.items, value->as.list.count, error, depth);
    case TRL_TAG_STRUCTURED:
        return write_string(buffer, value->as.structured.type.schema, error) &&
               write_string(buffer, value->as.structured.type.name, error) &&
               write_list(buffer, value->as.structured.attributes, value->as.structured.count, error, depth);
    default:
        return refuse(error, "no value has this tag");
    }
}

/* Writes the values whose body is bytes or a string. */
static bool write_octets(trl_buffer *buffer, const trl_value *value, trl_codec_error *error, unsigned int depth) {
    switch (value->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_XML:
        return write_string(buffer, value->as.bytes, error);
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
    case TRL_TAG_REFERENCE:
        trl_put_bytes(buffer, value->as.bytes);
        return true;
    case TRL_TAG_JSON:
        if (!trl_json_valid(value->as.bytes.bytes, value->as.bytes.length)) {
            return refuse(error, "a JSON value to send is no document of its stored form");
        }
        trl_put_bytes(buffer, value->as.bytes);
        return true;
    case TRL_TAG_UUID:
        if (value->as.bytes.length != TRL_UUID_OCTETS) {
            return refuse(error, "a UUID is 16 octets");
        }
        trl_put_bytes(buffer, value->as.bytes);
        return true;
    default:
        return write_constructed(buffer, value, error, depth);
    }
}

/* Writes a plug-in's value: its type's name, its text, its bytes. */
static bool write_plugin(trl_buffer *buffer, const trl_value *value, trl_codec_error *error) {
    if (!write_string(buffer, value->as.plugin.type_name, error) ||
        !write_string(buffer, value->as.plugin.text, error)) {
        return false;
    }
    trl_put_bytes(buffer, value->as.plugin.bytes);
    return true;
}

/* Writes the scalar values of fixed width. */
static bool write_scalar(trl_buffer *buffer, const trl_value *value, trl_codec_error *error, unsigned int depth) {
    switch (value->tag) {
    case TRL_TAG_NULL:
        return true;
    case TRL_TAG_EXACT:
        trl_put_i128(buffer, value->as.exact.units);
        trl_put_u32(buffer, value->as.exact.scale);
        return true;
    case TRL_TAG_APPROXIMATE:
        trl_put_u64(buffer, value->as.approximate);
        return true;
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP_ZONED:
        write_datetime(buffer, value->as.datetime);
        return true;
    case TRL_TAG_BOOLEAN:
        trl_put_flag(buffer, value->as.truth);
        return true;
    default:
        return write_octets(buffer, value, error, depth);
    }
}

static bool write_value(trl_buffer *buffer, const trl_value *value, trl_codec_error *error, unsigned int depth) {
    trl_put_u8(buffer, value->tag);
    switch (value->tag) {
    case TRL_TAG_PLUGIN:
        return write_plugin(buffer, value, error);
    case TRL_TAG_INTERVAL:
        return write_interval(buffer, &value->as.interval, error);
    case TRL_TAG_DECFLOAT:
        return write_decfloat(buffer, value->as.decfloat, error);
    case TRL_TAG_LOB_WHOLE:
        return write_lob(buffer, value, error);
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        return write_lob_typed(buffer, value, error);
    case TRL_TAG_LOB_LOCATOR:
        return write_locator(buffer, value, error);
    default:
        return write_scalar(buffer, value, error, depth);
    }
}

bool trl_write_value(trl_buffer *buffer, const trl_value *value, trl_codec_error *error) {
    if (value == NULL) {
        return refuse(error, "a value to send is null");
    }
    return write_value(buffer, value, error, 0U) && trl_buffer_ok(buffer, error);
}

bool trl_write_row(trl_buffer *buffer, const trl_value *values, size_t count, trl_codec_error *error) {
    if (count > 0U && values == NULL) {
        return refuse(error, "a row to send has no values");
    }
    trl_put_count(buffer, count);
    for (size_t i = 0U; i < count; ++i) {
        if (!write_value(buffer, &values[i], error, 0U)) {
            return false;
        }
    }
    return trl_buffer_ok(buffer, error);
}
