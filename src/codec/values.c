/**
 * @file values.c
 * @brief Values read from a reply [PROTOCOL Values], as
 * turborel-protocol's `fields.rs` and the types' own readers read them:
 * each tag's body checked, the nested ones bounded in depth, and a
 * malformed one a connection failure.
 */
#include "codec/values.h"

#include "codec/decfloat.h"
#include "codec/int128.h"
#include "codec/json.h"
#include "codec/reader.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/utf8.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The fewest bytes a value takes: its tag. */
#define TRL_VALUE_LEAST_BYTES 1U

/* Reads a datetime's body: zoned, precision, displacement, instant. */
static bool read_datetime(trl_reader *reader, trl_datetime *out) {
    uint8_t zoned = 0U;
    uint32_t zone = 0U;
    uint64_t utc = 0U;
    if (!trl_read_u8(reader, &zoned) || !trl_read_u8(reader, &out->precision) || !trl_read_u32(reader, &zone)) {
        return false;
    }
    if (zone > UINT16_MAX) {
        return trl_reader_fail(reader, "a time zone displacement is out of range");
    }
    if (!trl_read_u64(reader, &utc)) {
        return false;
    }
    /* turborel reads a zoned byte of 1 as zoned, any other as not. */
    out->zoned = zoned == 1U;
    out->zone = trl_bits_i16((uint16_t)zone);
    out->utc = trl_bits_i64(utc);
    return true;
}

/* Reads a truth value, 0 or 1. */
static bool read_truth(trl_reader *reader, bool *out) {
    uint8_t byte = 0U;
    if (!trl_read_u8(reader, &byte)) {
        return false;
    }
    if (byte > 1U) {
        return trl_reader_fail(reader, "a truth value is neither 0 nor 1");
    }
    *out = byte == 1U;
    return true;
}

/* Reads a DECFLOAT: coefficient, then exponent, which must be canonical. */
static bool read_decfloat(trl_reader *reader, trl_decfloat *out) {
    uint32_t exponent = 0U;
    if (!trl_read_i128(reader, &out->coefficient) || !trl_read_u32(reader, &exponent)) {
        return false;
    }
    out->exponent = trl_bits_i32(exponent);
    return trl_decfloat_canonical(*out) || trl_reader_fail(reader, "a DECFLOAT value is malformed");
}

/* Reads a large object's kind, 0 or 1. */
static bool read_lob_kind(trl_reader *reader, uint8_t *out) {
    if (!trl_read_u8(reader, out)) {
        return false;
    }
    return *out <= TRL_LOB_BINARY || trl_reader_fail(reader, "not a kind of large object");
}

/* Reads a large object held whole; a character one's octets are UTF-8. */
static bool read_lob_whole(trl_reader *reader, trl_value *out) {
    if (!read_lob_kind(reader, &out->as.lob.kind) || !trl_read_bytes(reader, &out->as.lob.octets)) {
        return false;
    }
    return out->as.lob.kind == TRL_LOB_BINARY || trl_utf8_valid(out->as.lob.octets.bytes, out->as.lob.octets.length) ||
           trl_reader_fail(reader, "a character large object is not UTF-8");
}

/* Reads a large object under its type's tag, as turborel's reader takes
   one though no server sends it: a form byte, 0 for its octets held
   whole; 1, a large object kept in a database, has no meaning to a client
   and is refused. */
static bool read_lob_typed(trl_reader *reader, trl_value *out) {
    uint8_t form = 0U;
    if (!trl_read_u8(reader, &form)) {
        return false;
    }
    if (form != 0U) {
        return trl_reader_fail(reader, "a large object kept in a database cannot be read by a client");
    }
    out->as.lob.kind = out->tag == TRL_TAG_CLOB ? TRL_LOB_CHARACTER : TRL_LOB_BINARY;
    if (!trl_read_bytes(reader, &out->as.lob.octets)) {
        return false;
    }
    return out->as.lob.kind == TRL_LOB_BINARY || trl_utf8_valid(out->as.lob.octets.bytes, out->as.lob.octets.length) ||
           trl_reader_fail(reader, "a character large object is not UTF-8");
}

/* Reads a large object by its locator. */
static bool read_lob_locator(trl_reader *reader, trl_value *out) {
    return read_lob_kind(reader, &out->as.locator.kind) && trl_read_u32(reader, &out->as.locator.locator) &&
           trl_read_u64(reader, &out->as.locator.octets) && trl_read_u64(reader, &out->as.locator.characters);
}

/* Reads a list of values, within the depth. */
static bool read_list(trl_reader *reader, trl_value **items, uint32_t *count) {
    if (!trl_reader_enter(reader)) {
        return false;
    }
    bool read = trl_read_row(reader, items, count);
    trl_reader_leave(reader);
    return read;
}

/* Reads a structured value: its type's schema and name, then attributes. */
static bool read_structured(trl_reader *reader, trl_value *out) {
    if (!trl_read_string(reader, &out->as.structured.type.schema) ||
        !trl_read_string(reader, &out->as.structured.type.name)) {
        return false;
    }
    return read_list(reader, &out->as.structured.attributes, &out->as.structured.count);
}

/* Reads a plug-in's value: its type's name, its text, its bytes. */
static bool read_plugin(trl_reader *reader, trl_value *out) {
    return trl_read_string(reader, &out->as.plugin.type_name) && trl_read_string(reader, &out->as.plugin.text) &&
           trl_read_bytes(reader, &out->as.plugin.bytes);
}

/* Reads a UUID: bytes, exactly 16 of them. */
static bool read_uuid(trl_reader *reader, trl_slice *out) {
    if (!trl_read_bytes(reader, out)) {
        return false;
    }
    return out->length == TRL_UUID_OCTETS || trl_reader_fail(reader, "a UUID is 16 octets");
}

/* Reads a JSON value: bytes, a document of turborel's binary tree. */
static bool read_json(trl_reader *reader, trl_slice *out) {
    if (!trl_read_bytes(reader, out)) {
        return false;
    }
    return trl_json_valid(out->bytes, out->length) || trl_reader_fail(reader, "a JSON value's octets are damaged");
}

/* Reads the bodies of the constructed values. */
static bool read_constructed(trl_reader *reader, trl_value *out) {
    switch (out->tag) {
    case TRL_TAG_ARRAY:
    case TRL_TAG_ROW:
        return read_list(reader, &out->as.list.items, &out->as.list.count);
    case TRL_TAG_MULTISET:
        if (!read_list(reader, &out->as.list.items, &out->as.list.count)) {
            return false;
        }
        return out->as.list.count <= TRL_MAX_MULTISET_CARDINALITY ||
               trl_reader_fail(reader, "a multiset has more elements than its maximum cardinality");
    case TRL_TAG_STRUCTURED:
        if (!trl_reader_enter(reader)) {
            return false;
        }
        bool read = read_structured(reader, out);
        trl_reader_leave(reader);
        return read;
    default:
        return trl_reader_fail(reader, "no value has this tag");
    }
}

/* Reads the bodies of the values whose body is bytes or a string. */
static bool read_octets(trl_reader *reader, trl_value *out) {
    switch (out->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_XML:
        return trl_read_string(reader, &out->as.bytes);
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
    case TRL_TAG_REFERENCE:
        return trl_read_bytes(reader, &out->as.bytes);
    case TRL_TAG_JSON:
        return read_json(reader, &out->as.bytes);
    case TRL_TAG_UUID:
        return read_uuid(reader, &out->as.bytes);
    default:
        return read_constructed(reader, out);
    }
}

bool trl_read_value(trl_reader *reader, trl_value *out) {
    if (!trl_read_u8(reader, &out->tag)) {
        return false;
    }
    switch (out->tag) {
    case TRL_TAG_NULL:
        return true;
    case TRL_TAG_EXACT:
        return trl_read_i128(reader, &out->as.exact.units) && trl_read_u32(reader, &out->as.exact.scale);
    case TRL_TAG_APPROXIMATE:
        return trl_read_u64(reader, &out->as.approximate);
    case TRL_TAG_PLUGIN:
        return read_plugin(reader, out);
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP_ZONED:
        return read_datetime(reader, &out->as.datetime);
    case TRL_TAG_INTERVAL:
        return trl_read_interval_qualifier(reader, &out->as.interval.qualifier) &&
               trl_read_i128(reader, &out->as.interval.units);
    case TRL_TAG_BOOLEAN:
        return read_truth(reader, &out->as.truth);
    case TRL_TAG_DECFLOAT:
        return read_decfloat(reader, &out->as.decfloat);
    case TRL_TAG_LOB_WHOLE:
        return read_lob_whole(reader, out);
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        return read_lob_typed(reader, out);
    case TRL_TAG_LOB_LOCATOR:
        return read_lob_locator(reader, out);
    default:
        return read_octets(reader, out);
    }
}

bool trl_read_row(trl_reader *reader, trl_value **values, uint32_t *count) {
    if (!trl_read_count(reader, TRL_VALUE_LEAST_BYTES, count)) {
        return false;
    }
    *values = trl_reader_alloc(reader, *count, sizeof(trl_value));
    if (*values == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < *count; ++i) {
        if (!trl_read_value(reader, &(*values)[i])) {
            return false;
        }
    }
    return true;
}
