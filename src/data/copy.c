/**
 * @file copy.c
 * @brief A value copied whole into an arena: its strings, its bytes and
 * the values nested in it, as deep as the protocol nests them.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/fields.h"
#include "codec/reader.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/values.h"
#include "data/params.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Copies `slice`'s bytes into the arena. */
static bool copy_slice(trl_arena *arena, trl_slice *slice) {
    if (slice->length == 0U) {
        return true;
    }
    uint8_t *copy = trl_arena_alloc(arena, slice->length, 1U);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, slice->bytes, slice->length);
    slice->bytes = copy;
    return true;
}

/* Copies `count` values at `*items` into the arena, and points there. */
static bool copy_items(trl_arena *arena, trl_value **items, uint32_t count, unsigned int depth) {
    if (count == 0U) {
        return true;
    }
    if (depth >= TRL_MAX_NESTING) {
        return false;
    }
    trl_value *copies = trl_arena_alloc(arena, count, sizeof *copies);
    if (copies == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < count; ++i) {
        if (!trl_value_copy(arena, &(*items)[i], &copies[i], depth + 1U)) {
            return false;
        }
    }
    *items = copies;
    return true;
}

bool trl_value_copy(trl_arena *arena, const trl_value *from, trl_value *out, unsigned int depth) {
    *out = *from;
    switch (out->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
    case TRL_TAG_JSON:
    case TRL_TAG_XML:
    case TRL_TAG_UUID:
    case TRL_TAG_REFERENCE:
        return copy_slice(arena, &out->as.bytes);
    case TRL_TAG_PLUGIN:
        return copy_slice(arena, &out->as.plugin.type_name) && copy_slice(arena, &out->as.plugin.text) &&
               copy_slice(arena, &out->as.plugin.bytes);
    case TRL_TAG_LOB_WHOLE:
        return copy_slice(arena, &out->as.lob.octets);
    case TRL_TAG_ARRAY:
    case TRL_TAG_ROW:
    case TRL_TAG_MULTISET:
        return copy_items(arena, &out->as.list.items, out->as.list.count, depth);
    case TRL_TAG_STRUCTURED:
        return copy_slice(arena, &out->as.structured.type.schema) && copy_slice(arena, &out->as.structured.type.name) &&
               copy_items(arena, &out->as.structured.attributes, out->as.structured.count, depth);
    default:
        return true;
    }
}

/* Copies a schema-qualified name's parts into the arena. */
static bool copy_name(trl_arena *arena, trl_name *name) {
    return copy_slice(arena, &name->schema) && copy_slice(arena, &name->name);
}

/* Copies a structured type's name and its supertypes' into the arena. */
static bool copy_type_names(trl_arena *arena, trl_type_names *names) {
    if (!copy_name(arena, &names->name)) {
        return false;
    }
    if (names->supertype_count == 0U) {
        return true;
    }
    trl_name *copies = trl_arena_alloc(arena, names->supertype_count, sizeof *copies);
    if (copies == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < names->supertype_count; ++i) {
        copies[i] = names->supertypes[i];
        if (!copy_name(arena, &copies[i])) {
            return false;
        }
    }
    names->supertypes = copies;
    return true;
}

/* Copies a type nested in another into the arena, and points there. */
static bool copy_nested(trl_arena *arena, trl_data_type **type, unsigned int depth) {
    if (*type == NULL) {
        return true;
    }
    if (depth >= TRL_MAX_NESTING) {
        return false;
    }
    trl_data_type *copy = trl_arena_alloc(arena, 1U, sizeof *copy);
    if (copy == NULL || !trl_type_copy(arena, *type, copy, depth + 1U)) {
        return false;
    }
    *type = copy;
    return true;
}

/* Copies a row type's fields into the arena. */
static bool copy_fields(trl_arena *arena, trl_data_type *type, unsigned int depth) {
    if (type->as.row.count == 0U) {
        return true;
    }
    trl_row_field *copies = trl_arena_alloc(arena, type->as.row.count, sizeof *copies);
    if (copies == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < type->as.row.count; ++i) {
        copies[i] = type->as.row.fields[i];
        if (!copy_slice(arena, &copies[i].name) || !copy_nested(arena, &copies[i].type, depth)) {
            return false;
        }
    }
    type->as.row.fields = copies;
    return true;
}

/* Copies an XML type's names into the arena. */
static bool copy_xml(trl_arena *arena, trl_xml_type *xml) {
    return copy_name(arena, &xml->schema) && copy_slice(arena, &xml->target_namespace) &&
           copy_slice(arena, &xml->element);
}

bool trl_type_copy(trl_arena *arena, const trl_data_type *from, trl_data_type *out, unsigned int depth) {
    *out = *from;
    switch (out->tag) {
    case TRL_TAG_PLUGIN:
        return copy_slice(arena, &out->as.plugin.name);
    case TRL_TAG_ARRAY:
        return copy_nested(arena, &out->as.array.element, depth);
    case TRL_TAG_MULTISET:
        return copy_nested(arena, &out->as.element, depth);
    case TRL_TAG_ROW:
        return copy_fields(arena, out, depth);
    case TRL_TAG_STRUCTURED:
        return copy_type_names(arena, &out->as.structured);
    case TRL_TAG_REFERENCE:
        return copy_type_names(arena, &out->as.reference.referenced) && copy_name(arena, &out->as.reference.scope);
    case TRL_TAG_XML:
        return copy_xml(arena, &out->as.xml);
    default:
        return true;
    }
}

trl_column *trl_columns_copy(trl_arena *arena, const trl_column *from, uint32_t count) {
    trl_column *copies = trl_arena_alloc(arena, count == 0U ? 1U : count, sizeof *copies);
    for (uint32_t i = 0U; copies != NULL && i < count; ++i) {
        copies[i] = from[i];
        if (!copy_slice(arena, &copies[i].name) || !copy_slice(arena, &copies[i].declared_name) ||
            !copy_slice(arena, &copies[i].declared_written) ||
            !trl_type_copy(arena, &from[i].type, &copies[i].type, 0U)) {
            return NULL;
        }
    }
    return copies;
}
