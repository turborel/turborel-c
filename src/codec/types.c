/**
 * @file types.c
 * @brief Data types read from a reply [PROTOCOL Data types], as
 * turborel-protocol's `fields.rs` reads them: each kind's parameters, the
 * nested ones bounded in depth, and a malformed one a connection failure.
 */
#include "codec/types.h"

#include "codec/buffer.h"
#include "codec/reader.h"
#include "codec/tags.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The fewest bytes a list's item takes: a name, its schema's and its own
   strings' lengths; a row field, its name's length and a type's tag. */
#define TRL_NAME_LEAST_BYTES 8U
#define TRL_FIELD_LEAST_BYTES 5U

/* Reads a character length's units, 0 or 1. */
static bool read_units(trl_reader *reader, uint8_t *out) {
    if (!trl_read_u8(reader, out)) {
        return false;
    }
    return *out <= TRL_UNITS_OCTETS || trl_reader_fail(reader, "a length's units are neither characters nor octets");
}

/* Reads a schema and a name. */
static bool read_name(trl_reader *reader, trl_name *out) {
    return trl_read_string(reader, &out->schema) && trl_read_string(reader, &out->name);
}

/* Reads a structured type's names: its own, then its supertypes'. */
static bool read_type_names(trl_reader *reader, trl_type_names *out) {
    uint32_t count = 0U;
    if (!read_name(reader, &out->name) || !trl_read_count(reader, TRL_NAME_LEAST_BYTES, &count)) {
        return false;
    }
    out->supertypes = trl_reader_alloc(reader, count, sizeof(trl_name));
    if (out->supertypes == NULL) {
        return false;
    }
    out->supertype_count = count;
    for (uint32_t i = 0U; i < count; ++i) {
        if (!read_name(reader, &out->supertypes[i])) {
            return false;
        }
    }
    return true;
}

/* Reads an optional part of an XML schema's name: 0 none, 1 a string. */
static bool read_optional(trl_reader *reader, bool *present, trl_slice *out) {
    uint8_t marker = 0U;
    if (!trl_read_u8(reader, &marker)) {
        return false;
    }
    if (marker > 1U) {
        return trl_reader_fail(reader, "an XML type's namespace or element is marked neither 0 nor 1");
    }
    *present = marker == 1U;
    return !*present || trl_read_string(reader, out);
}

/* Reads an XML type's modifiers, and an XMLSCHEMA's names. */
static bool read_xml(trl_reader *reader, trl_xml_type *out) {
    if (!trl_read_u8(reader, &out->primary) || !trl_read_u8(reader, &out->secondary)) {
        return false;
    }
    if (out->secondary > TRL_XML_XMLSCHEMA) {
        return trl_reader_fail(reader, "an XML type's secondary modifier is no modifier");
    }
    if (out->primary > TRL_XML_SEQUENCE || (out->primary == TRL_XML_SEQUENCE && out->secondary != TRL_XML_UNTYPED)) {
        return trl_reader_fail(reader, "an XML type's primary modifier is no modifier, or SEQUENCE typed");
    }
    if (out->secondary != TRL_XML_XMLSCHEMA) {
        return true;
    }
    return read_name(reader, &out->schema) && read_optional(reader, &out->has_namespace, &out->target_namespace) &&
           read_optional(reader, &out->has_element, &out->element);
}

/* Reads an interval's qualifier; its fields are 0 to 5. */
static bool read_qualifier(trl_reader *reader, trl_interval_qualifier *out) {
    if (!trl_read_u32(reader, &out->start) || !trl_read_u32(reader, &out->end) ||
        !trl_read_u32(reader, &out->leading) || !trl_read_u32(reader, &out->fraction)) {
        return false;
    }
    return (out->start <= TRL_FIELD_SECOND && out->end <= TRL_FIELD_SECOND) ||
           trl_reader_fail(reader, "an interval's field is out of range");
}

/* Reads a plug-in's type: its name and at most two parameters. */
static bool read_plugin(trl_reader *reader, trl_data_type *out) {
    uint32_t count = 0U;
    if (!trl_read_string(reader, &out->as.plugin.name) || !trl_read_count(reader, TRL_U32_BYTES, &count)) {
        return false;
    }
    if (count > TRL_MAX_PLUGIN_PARAMS) {
        return trl_reader_fail(reader, "a type has too many parameters");
    }
    out->as.plugin.param_count = count;
    for (uint32_t i = 0U; i < count; ++i) {
        if (!trl_read_u32(reader, &out->as.plugin.params[i])) {
            return false;
        }
    }
    return true;
}

/* A new type from the arena, read. */
static trl_data_type *read_nested(trl_reader *reader) {
    trl_data_type *type = trl_reader_alloc(reader, 1U, sizeof(trl_data_type));
    if (type == NULL || !trl_read_data_type(reader, type)) {
        return NULL;
    }
    return type;
}

/* Reads a row type's fields, one or more. */
static bool read_row(trl_reader *reader, trl_data_type *out) {
    uint32_t count = 0U;
    if (!trl_read_count(reader, TRL_FIELD_LEAST_BYTES, &count)) {
        return false;
    }
    if (count == 0U) {
        return trl_reader_fail(reader, "a row type has no fields");
    }
    out->as.row.fields = trl_reader_alloc(reader, count, sizeof(trl_row_field));
    if (out->as.row.fields == NULL) {
        return false;
    }
    out->as.row.count = count;
    for (uint32_t i = 0U; i < count; ++i) {
        trl_row_field *field = &out->as.row.fields[i];
        if (!trl_read_string(reader, &field->name)) {
            return false;
        }
        field->type = read_nested(reader);
        if (field->type == NULL) {
            return false;
        }
    }
    return true;
}

/* Reads an array type's maximum cardinality, 1 to 65535, and element type. */
static bool read_array(trl_reader *reader, trl_data_type *out) {
    if (!trl_read_u32(reader, &out->as.array.max)) {
        return false;
    }
    if (out->as.array.max == 0U || out->as.array.max > TRL_MAX_ARRAY_CARDINALITY) {
        return trl_reader_fail(reader, "an array type's maximum cardinality is out of range");
    }
    out->as.array.element = read_nested(reader);
    return out->as.array.element != NULL;
}

/* Reads the kinds that nest: array, multiset and row; within the depth. */
static bool read_constructed(trl_reader *reader, trl_data_type *out) {
    if (!trl_reader_enter(reader)) {
        return false;
    }
    bool read = false;
    if (out->tag == TRL_TAG_ARRAY) {
        read = read_array(reader, out);
    } else if (out->tag == TRL_TAG_MULTISET) {
        out->as.element = read_nested(reader);
        read = out->as.element != NULL;
    } else {
        read = read_row(reader, out);
    }
    trl_reader_leave(reader);
    return read;
}

/* Reads a reference type: its referenced type's names, then its scope. */
static bool read_reference(trl_reader *reader, trl_data_type *out) {
    uint8_t scoped = 0U;
    if (!read_type_names(reader, &out->as.reference.referenced) || !trl_read_u8(reader, &scoped)) {
        return false;
    }
    if (scoped > 1U) {
        return trl_reader_fail(reader, "a reference type's scope flag is neither 0 nor 1");
    }
    out->as.reference.scoped = scoped == 1U;
    return scoped == 0U || read_name(reader, &out->as.reference.scope);
}

/* Reads the parameters of the kinds that take a length, or none. */
static bool read_sized(trl_reader *reader, trl_data_type *out) {
    switch (out->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
        return trl_read_u32(reader, &out->as.character.length) && read_units(reader, &out->as.character.units);
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
        return trl_read_u32(reader, &out->as.length);
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        return trl_read_u64(reader, &out->as.lob.length) && read_units(reader, &out->as.lob.units);
    case TRL_TAG_BOOLEAN:
    case TRL_TAG_JSON:
    case TRL_TAG_UUID:
        return true;
    default:
        return trl_reader_fail(reader, "no data type has this tag");
    }
}

bool trl_read_data_type(trl_reader *reader, trl_data_type *out) {
    if (!trl_read_u8(reader, &out->tag)) {
        return false;
    }
    switch (out->tag) {
    case TRL_TAG_EXACT:
        return trl_read_u32(reader, &out->as.exact.precision) && trl_read_u32(reader, &out->as.exact.scale);
    case TRL_TAG_APPROXIMATE:
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIME_ZONED:
    case TRL_TAG_TIMESTAMP:
    case TRL_TAG_TIMESTAMP_ZONED:
    case TRL_TAG_DECFLOAT:
        return trl_read_u32(reader, &out->as.precision);
    case TRL_TAG_PLUGIN:
        return read_plugin(reader, out);
    case TRL_TAG_INTERVAL:
        return read_qualifier(reader, &out->as.interval);
    case TRL_TAG_ARRAY:
    case TRL_TAG_MULTISET:
    case TRL_TAG_ROW:
        return read_constructed(reader, out);
    case TRL_TAG_STRUCTURED:
        return read_type_names(reader, &out->as.structured);
    case TRL_TAG_REFERENCE:
        return read_reference(reader, out);
    case TRL_TAG_XML:
        return read_xml(reader, &out->as.xml);
    default:
        return read_sized(reader, out);
    }
}

bool trl_read_interval_qualifier(trl_reader *reader, trl_interval_qualifier *out) {
    return read_qualifier(reader, out);
}
