/**
 * @file type_text.c
 * @brief A data type as SQL writes it, as turborel's `DataType` displays a
 * type read from the protocol: by its tag and what follows, so an exact
 * numeric type is NUMERIC(10,0) whatever it was declared as, and an
 * approximate one FLOAT(53) [PROTOCOL Data types]. A column's declared
 * type, where the server tells it, is written as it was declared
 * [PROTOCOL Declared types].
 */
#include "codec/buffer.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "data/text.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Room for a number written in decimal. */
#define TRL_NUMBER_BYTES 24U

static void put_text(trl_buffer *out, const char *text) {
    trl_put_raw(out, (const uint8_t *)text, strlen(text));
}

static void put_slice(trl_buffer *out, trl_slice slice) {
    if (slice.length > 0U) {
        trl_put_raw(out, slice.bytes, slice.length);
    }
}

static void put_number(trl_buffer *out, uint64_t number) {
    char digits[TRL_NUMBER_BYTES];
    int written = snprintf(digits, sizeof digits, "%llu", (unsigned long long)number);
    trl_put_raw(out, (const uint8_t *)digits, written > 0 ? (size_t)written : 0U);
}

/* Appends `name`, then its length in parentheses, " OCTETS" after it if
   `octets`. */
static void put_sized(trl_buffer *out, const char *name, uint64_t length, bool octets) {
    put_text(out, name);
    put_text(out, "(");
    put_number(out, length);
    put_text(out, octets ? " OCTETS)" : ")");
}

/* Appends a user-defined type's name: its schema, a period, its name. */
static void put_name(trl_buffer *out, const trl_name *name) {
    put_slice(out, name->schema);
    put_text(out, ".");
    put_slice(out, name->name);
}

/* Appends `text`, each `quote` in it written twice. */
static void put_doubled(trl_buffer *out, trl_slice text, uint8_t quote) {
    for (size_t i = 0U; i < text.length; ++i) {
        trl_put_u8(out, text.bytes[i]);
        if (text.bytes[i] == quote) {
            trl_put_u8(out, quote);
        }
    }
}

/* Appends an XML type: XML(DOCUMENT(ANY)), XML(SEQUENCE), or of XMLSCHEMA
   its schema, namespace and element [ISO/IEC 9075-14:2006 draft §6.1]. */
static void put_xml(trl_buffer *out, const trl_xml_type *xml) {
    static const char *const primaries[] = {"DOCUMENT", "CONTENT", "SEQUENCE"};
    static const char *const secondaries[] = {"UNTYPED", "ANY", "XMLSCHEMA"};
    put_text(out, "XML(");
    put_text(out, primaries[xml->primary <= TRL_XML_SEQUENCE ? xml->primary : TRL_XML_SEQUENCE]);
    if (xml->primary != TRL_XML_SEQUENCE && xml->secondary <= TRL_XML_XMLSCHEMA) {
        put_text(out, "(");
        put_text(out, secondaries[xml->secondary]);
        if (xml->secondary == TRL_XML_XMLSCHEMA) {
            put_text(out, " ID ");
            put_name(out, &xml->schema);
            if (xml->has_namespace && xml->target_namespace.length == 0U) {
                put_text(out, " NO NAMESPACE");
            } else if (xml->has_namespace) {
                put_text(out, " NAMESPACE '");
                put_doubled(out, xml->target_namespace, (uint8_t)'\'');
                put_text(out, "'");
            }
            if (xml->has_element) {
                put_text(out, " ELEMENT \"");
                put_doubled(out, xml->element, (uint8_t)'"');
                put_text(out, "\"");
            }
        }
        put_text(out, ")");
    }
    put_text(out, ")");
}

/* Appends a row type: ROW(name type, ...). */
static void put_row(trl_buffer *out, const trl_data_type *type) {
    put_text(out, "ROW(");
    for (uint32_t i = 0U; i < type->as.row.count; ++i) {
        put_text(out, i == 0U ? "" : ", ");
        put_slice(out, type->as.row.fields[i].name);
        put_text(out, " ");
        trl_type_put_text(out, type->as.row.fields[i].type);
    }
    put_text(out, ")");
}

/* Appends a reference type: REF(name), and its scope if it has one. */
static void put_reference(trl_buffer *out, const trl_data_type *type) {
    put_text(out, "REF(");
    put_name(out, &type->as.reference.referenced.name);
    put_text(out, ")");
    if (type->as.reference.scoped) {
        put_text(out, " SCOPE ");
        put_name(out, &type->as.reference.scope);
    }
}

/* Appends a datetime type: DATE, or TIME or TIMESTAMP, its precision and
   whether WITH TIME ZONE. */
static void put_datetime(trl_buffer *out, const trl_data_type *type) {
    if (type->tag == TRL_TAG_DATE) {
        put_text(out, "DATE");
        return;
    }
    bool time = type->tag == TRL_TAG_TIME || type->tag == TRL_TAG_TIME_ZONED;
    put_text(out, time ? "TIME(" : "TIMESTAMP(");
    put_number(out, type->as.precision);
    put_text(out, ")");
    if (type->tag == TRL_TAG_TIME_ZONED || type->tag == TRL_TAG_TIMESTAMP_ZONED) {
        put_text(out, " WITH TIME ZONE");
    }
}

/* Appends a numeric type: NUMERIC(p,s), FLOAT(p) or DECFLOAT(p). */
static void put_numeric(trl_buffer *out, const trl_data_type *type) {
    if (type->tag == TRL_TAG_EXACT) {
        put_text(out, "NUMERIC(");
        put_number(out, type->as.exact.precision);
        put_text(out, ",");
        put_number(out, type->as.exact.scale);
        put_text(out, ")");
        return;
    }
    put_text(out, type->tag == TRL_TAG_APPROXIMATE ? "FLOAT(" : "DECFLOAT(");
    put_number(out, type->as.precision);
    put_text(out, ")");
}

/* Appends a string type: of characters, of octets, or a large object. */
static void put_string_type(trl_buffer *out, const trl_data_type *type) {
    switch (type->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
        put_sized(out, type->tag == TRL_TAG_CHARACTER ? "CHARACTER" : "CHARACTER VARYING", type->as.character.length,
                  type->as.character.units == TRL_UNITS_OCTETS);
        break;
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
        put_sized(out, type->tag == TRL_TAG_BINARY ? "BINARY" : "BINARY VARYING", type->as.length, false);
        break;
    case TRL_TAG_CLOB:
        put_sized(out, "CHARACTER LARGE OBJECT", type->as.lob.length, type->as.lob.units == TRL_UNITS_OCTETS);
        break;
    default:
        put_sized(out, "BINARY LARGE OBJECT", type->as.lob.length, false);
        break;
    }
}

/* Appends a collection or user-defined type: an array, a multiset, a row,
   a structured or reference type. */
static void put_composite(trl_buffer *out, const trl_data_type *type) {
    switch (type->tag) {
    case TRL_TAG_ARRAY:
        trl_type_put_text(out, type->as.array.element);
        put_text(out, " ARRAY[");
        put_number(out, type->as.array.max);
        put_text(out, "]");
        break;
    case TRL_TAG_MULTISET:
        trl_type_put_text(out, type->as.element);
        put_text(out, " MULTISET");
        break;
    case TRL_TAG_ROW:
        put_row(out, type);
        break;
    case TRL_TAG_STRUCTURED:
        put_name(out, &type->as.structured.name);
        break;
    default:
        put_reference(out, type);
        break;
    }
}

void trl_type_put_text(trl_buffer *out, const trl_data_type *type) {
    switch (type->tag) {
    case TRL_TAG_EXACT:
    case TRL_TAG_APPROXIMATE:
    case TRL_TAG_DECFLOAT:
        put_numeric(out, type);
        break;
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        put_string_type(out, type);
        break;
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIMESTAMP_ZONED:
        put_datetime(out, type);
        break;
    case TRL_TAG_INTERVAL:
        put_text(out, "INTERVAL ");
        trl_value_put_qualifier(out, &type->as.interval);
        break;
    case TRL_TAG_ARRAY:
    case TRL_TAG_MULTISET:
    case TRL_TAG_ROW:
    case TRL_TAG_STRUCTURED:
    case TRL_TAG_REFERENCE:
        put_composite(out, type);
        break;
    case TRL_TAG_BOOLEAN:
        put_text(out, "BOOLEAN");
        break;
    case TRL_TAG_JSON:
        put_text(out, "JSON");
        break;
    case TRL_TAG_UUID:
        put_text(out, "UUID");
        break;
    case TRL_TAG_XML:
        put_xml(out, &type->as.xml);
        break;
    default:
        /* A plug-in's type, which a client has not registered. */
        put_text(out, "UNREGISTERED");
        break;
    }
}

trl_status trl_type_to_text(const trl_data_type *type, char *out, size_t capacity, size_t *length) {
    if (type == NULL || (out == NULL && capacity > 0U)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    trl_type_put_text(&text, type);
    trl_status status = trl_text_give(&text, out, capacity, length);
    trl_buffer_free(&text);
    return status;
}
