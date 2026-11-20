/**
 * @file copy_test.c
 * @brief Values, types and columns copied deep (RULES.md D10): each of
 * the corpus's, of every tag, copied into an arena of its own and found
 * the same once the reply it was read from is gone.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/reader.h"
#include "codec/replies.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/values.h"
#include "corpus.h"
#include "data/params.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The corpus's samples of a Description and of Rows. */
#define DESCRIPTION 0U
#define FIRST_ROWS 1U
#define LAST_ROWS 3U
/* A byte no sample holds, written over a body once read. */
#define SCRIBBLE 0xA5U

/* Whether two slices hold the same bytes. */
static bool same_slice(trl_slice a, trl_slice b) {
    return a.length == b.length && (a.length == 0U || memcmp(a.bytes, b.bytes, a.length) == 0);
}

/* Whether two names are the same. */
static bool same_name(const trl_name *a, const trl_name *b) {
    return same_slice(a->schema, b->schema) && same_slice(a->name, b->name);
}

/* Whether two structured types' names are the same. */
static bool same_names(const trl_type_names *a, const trl_type_names *b) {
    if (!same_name(&a->name, &b->name) || a->supertype_count != b->supertype_count) {
        return false;
    }
    for (uint32_t i = 0U; i < a->supertype_count; ++i) {
        if (!same_name(&a->supertypes[i], &b->supertypes[i])) {
            return false;
        }
    }
    return true;
}

static bool same_type(const trl_data_type *a, const trl_data_type *b);

/* Whether two element types, either absent, are the same. */
static bool same_nested(const trl_data_type *a, const trl_data_type *b) {
    return (a == NULL && b == NULL) || (a != NULL && b != NULL && same_type(a, b));
}

/* Whether two row types' fields are the same. */
static bool same_fields(const trl_data_type *a, const trl_data_type *b) {
    if (a->as.row.count != b->as.row.count) {
        return false;
    }
    for (uint32_t i = 0U; i < a->as.row.count; ++i) {
        const trl_row_field *x = &a->as.row.fields[i];
        const trl_row_field *y = &b->as.row.fields[i];
        if (!same_slice(x->name, y->name) || !same_nested(x->type, y->type)) {
            return false;
        }
    }
    return true;
}

/* Whether two XML types are the same. */
static bool same_xml(const trl_xml_type *a, const trl_xml_type *b) {
    return a->primary == b->primary && a->secondary == b->secondary && same_name(&a->schema, &b->schema) &&
           a->has_namespace == b->has_namespace && same_slice(a->target_namespace, b->target_namespace) &&
           a->has_element == b->has_element && same_slice(a->element, b->element);
}

/* Whether two types of a scalar tag have the same parameters. */
static bool same_scalar_type(const trl_data_type *a, const trl_data_type *b) {
    const trl_interval_qualifier *x = &a->as.interval;
    const trl_interval_qualifier *y = &b->as.interval;
    switch (a->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
        return a->as.character.length == b->as.character.length && a->as.character.units == b->as.character.units;
    case TRL_TAG_EXACT:
        return a->as.exact.precision == b->as.exact.precision && a->as.exact.scale == b->as.exact.scale;
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
        return a->as.length == b->as.length;
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        return a->as.lob.length == b->as.lob.length && a->as.lob.units == b->as.lob.units;
    case TRL_TAG_INTERVAL:
        return x->start == y->start && x->end == y->end && x->leading == y->leading && x->fraction == y->fraction;
    default:
        return a->as.precision == b->as.precision;
    }
}

/* Whether two types are the same, what they point to compared and not
   where. */
static bool same_type(const trl_data_type *a, const trl_data_type *b) {
    if (a->tag != b->tag) {
        return false;
    }
    switch (a->tag) {
    case TRL_TAG_PLUGIN:
        return same_slice(a->as.plugin.name, b->as.plugin.name) && a->as.plugin.param_count == b->as.plugin.param_count;
    case TRL_TAG_ARRAY:
        return a->as.array.max == b->as.array.max && same_nested(a->as.array.element, b->as.array.element);
    case TRL_TAG_MULTISET:
        return same_nested(a->as.element, b->as.element);
    case TRL_TAG_ROW:
        return same_fields(a, b);
    case TRL_TAG_STRUCTURED:
        return same_names(&a->as.structured, &b->as.structured);
    case TRL_TAG_REFERENCE:
        return same_names(&a->as.reference.referenced, &b->as.reference.referenced) &&
               a->as.reference.scoped == b->as.reference.scoped &&
               same_name(&a->as.reference.scope, &b->as.reference.scope);
    case TRL_TAG_XML:
        return same_xml(&a->as.xml, &b->as.xml);
    default:
        return same_scalar_type(a, b);
    }
}

/* Reads sample `which` of the corpus into `arena`. */
static bool read_sample(sample *samples, size_t which, trl_arena *arena, trl_reply *out) {
    trl_codec_error error = {0, "", ""};
    trl_slice body = {samples[which].body.data, samples[which].body.length};
    return trl_decode_reply(samples[which].kind, body, arena, out, &error);
}

/* Writes over every sample's body, so what still points into one is seen. */
static void scribble(sample *samples, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        if (samples[i].body.length > 0U) {
            memset(samples[i].body.data, SCRIBBLE, samples[i].body.length);
        }
    }
}

/**
 * Verifies: [DESIGN] D10 the columns of a Description, a type of every
 * tag among them, copied deep: the same once the reply is gone.
 */
static void columns_are_copied_deep(trl_test *test) {
    sample samples[SAMPLES];
    size_t count = corpus(samples);
    trl_arena read;
    trl_arena kept;
    trl_arena again;
    trl_arena_init(&read);
    trl_arena_init(&kept);
    trl_arena_init(&again);
    trl_reply first;
    trl_reply second;
    TRL_CHECK(test, read_sample(samples, DESCRIPTION, &read, &first));
    TRL_CHECK(test, read_sample(samples, DESCRIPTION, &again, &second));
    uint32_t columns = first.as.description.count;
    trl_column *copies = trl_columns_copy(&kept, first.as.description.columns, columns);
    TRL_CHECK(test, copies != NULL && columns > 10U);
    trl_arena_free(&read);
    /* The second reading is compared with the copies once its bytes are
       copied away from the body, which is then written over. */
    trl_column *fresh = trl_columns_copy(&again, second.as.description.columns, columns);
    scribble(samples, count);
    for (uint32_t i = 0U; copies != NULL && fresh != NULL && i < columns; ++i) {
        TRL_CHECK(test, copies[i].named == fresh[i].named && same_slice(copies[i].name, fresh[i].name));
        TRL_CHECK(test, same_type(&copies[i].type, &fresh[i].type) && copies[i].not_null == fresh[i].not_null);
    }
    trl_arena_free(&kept);
    trl_arena_free(&again);
    free_corpus(samples);
}

/**
 * Verifies: [DESIGN] D10 the values of each Rows of the corpus, a value of
 * every tag among them, copied deep: written as the values read are, once
 * the reply is gone.
 */
static void values_are_copied_deep(trl_test *test) {
    sample samples[SAMPLES];
    size_t count = corpus(samples);
    for (size_t which = FIRST_ROWS; which <= LAST_ROWS; ++which) {
        trl_arena read;
        trl_arena kept;
        trl_arena_init(&read);
        trl_arena_init(&kept);
        trl_reply reply;
        TRL_CHECK(test, read_sample(samples, which, &read, &reply));
        trl_buffer expected;
        trl_buffer_init(&expected);
        trl_codec_error error = {0, "", ""};
        const trl_row *row = &reply.as.rows.rows[0];
        trl_value copies[16];
        uint32_t values = row->count < 16U ? row->count : 16U;
        TRL_CHECK(test, row->count <= 16U);
        for (uint32_t i = 0U; i < values; ++i) {
            TRL_CHECK(test, trl_write_value(&expected, &row->values[i], &error));
            TRL_CHECK(test, trl_value_copy(&kept, &row->values[i], &copies[i], 0U));
        }
        trl_arena_free(&read);
        scribble(samples, count);
        trl_buffer written;
        trl_buffer_init(&written);
        for (uint32_t i = 0U; i < values; ++i) {
            TRL_CHECK(test, trl_write_value(&written, &copies[i], &error));
        }
        TRL_CHECK(test, written.length == expected.length && memcmp(written.data, expected.data, expected.length) == 0);
        trl_buffer_free(&written);
        trl_buffer_free(&expected);
        trl_arena_free(&kept);
        free_corpus(samples);
        count = corpus(samples);
    }
    free_corpus(samples);
}

/**
 * Verifies: [DESIGN] D10 a value nested past the deepest a value may be is
 * not copied.
 */
static void nesting_past_the_deepest_is_refused(trl_test *test) {
    trl_value inner;
    memset(&inner, 0, sizeof inner);
    inner.tag = TRL_TAG_ARRAY;
    trl_value outer = inner;
    outer.as.list.items = &inner;
    outer.as.list.count = 1U;
    trl_arena arena;
    trl_arena_init(&arena);
    trl_value copy;
    TRL_CHECK(test, !trl_value_copy(&arena, &outer, &copy, TRL_MAX_NESTING));
    TRL_CHECK(test, trl_value_copy(&arena, &outer, &copy, 0U) && copy.as.list.items != &inner);
    trl_data_type element;
    memset(&element, 0, sizeof element);
    element.tag = TRL_TAG_BOOLEAN;
    trl_data_type multiset;
    memset(&multiset, 0, sizeof multiset);
    multiset.tag = TRL_TAG_MULTISET;
    multiset.as.element = &element;
    trl_data_type type_copy;
    TRL_CHECK(test, !trl_type_copy(&arena, &multiset, &type_copy, TRL_MAX_NESTING));
    TRL_CHECK(test, trl_type_copy(&arena, &multiset, &type_copy, 0U) && type_copy.as.element != &element);
    trl_arena_free(&arena);
}

/**
 * Verifies: [DESIGN] D10 a structured type's names and its supertypes', a
 * row type of no fields and an array of no element type, copied.
 */
static void types_of_names_are_copied(trl_test *test) {
    trl_name supertypes[2] = {
        {{(const uint8_t *)"S", 1U}, {(const uint8_t *)"BASE", 4U}},
        {{(const uint8_t *)"S", 1U}, {(const uint8_t *)"ROOT", 4U}},
    };
    trl_data_type structured;
    memset(&structured, 0, sizeof structured);
    structured.tag = TRL_TAG_STRUCTURED;
    structured.as.structured.name.schema = (trl_slice){(const uint8_t *)"S", 1U};
    structured.as.structured.name.name = (trl_slice){(const uint8_t *)"T", 1U};
    structured.as.structured.supertypes = supertypes;
    structured.as.structured.supertype_count = 2U;
    trl_arena arena;
    trl_arena_init(&arena);
    trl_data_type copy;
    TRL_CHECK(test, trl_type_copy(&arena, &structured, &copy, 0U) && same_type(&copy, &structured));
    TRL_CHECK(test, copy.as.structured.supertypes != supertypes);
    structured.as.structured.supertype_count = 0U;
    TRL_CHECK(test, trl_type_copy(&arena, &structured, &copy, 0U) && same_type(&copy, &structured));
    trl_data_type row;
    memset(&row, 0, sizeof row);
    row.tag = TRL_TAG_ROW;
    TRL_CHECK(test, trl_type_copy(&arena, &row, &copy, 0U) && copy.as.row.count == 0U);
    trl_data_type array;
    memset(&array, 0, sizeof array);
    array.tag = TRL_TAG_ARRAY;
    TRL_CHECK(test, trl_type_copy(&arena, &array, &copy, 0U) && copy.as.array.element == NULL);
    TRL_CHECK(test, trl_columns_copy(&arena, NULL, 0U) != NULL);
    trl_arena_free(&arena);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(columns_are_copied_deep),
        TRL_TEST_CASE(values_are_copied_deep),
        TRL_TEST_CASE(nesting_past_the_deepest_is_refused),
        TRL_TEST_CASE(types_of_names_are_copied),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
