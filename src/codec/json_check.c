/**
 * @file json_check.c
 * @brief A JSON value's stored form checked whole, as turborel's
 * `binary::validate` and `Node::check` check it (turborel's RULES.md D89),
 * so a damaged document received is refused, never read past its end.
 */
#include "codec/decfloat.h"
#include "codec/json.h"
#include "codec/json_nodes.h"
#include "codec/utf8.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The fewest octets of a document: its version and a node's tag. */
#define TRL_JSON_LEAST_DOCUMENT 2U

/* A check in progress: the first failure ends it. */
typedef struct checker {
    trl_json_check result;
} checker;

/* Fails the check as damaged, keeping a failure for want of memory. */
static bool damaged(checker *check) {
    if (check->result == TRL_JSON_WHOLE) {
        check->result = TRL_JSON_DAMAGED;
    }
    return false;
}

static bool check_node(checker *check, trl_json_span node, unsigned int depth, size_t *length);

/* Checks a string's octets are UTF-8. */
static bool check_string(checker *check, trl_json_span node) {
    trl_json_span text = {NULL, 0U};
    return (trl_json_string_at(node, 1U, &text) && trl_utf8_valid(text.bytes, text.length)) || damaged(check);
}

/* Checks a datetime's moment and zoned flag. */
static bool check_datetime(checker *check, trl_json_span node) {
    uint8_t moment = node.bytes[TRL_JSON_DATETIME_MOMENT_AT];
    uint8_t zoned = node.bytes[TRL_JSON_DATETIME_ZONED_AT];
    return (moment <= TRL_JSON_MOMENT_TIMESTAMP && zoned <= 1U) || damaged(check);
}

/* Checks a DECFLOAT is one turborel makes of its octets. */
static bool check_decfloat(checker *check, trl_json_span node) {
    trl_decfloat value = trl_json_decfloat_at(node);
    trl_decfloat made = {{0U, 0}, 0};
    return trl_decfloat_made(value, &made) || damaged(check);
}

/* Checks each element is where its offset says, one after another,
   filling the array's size. */
static bool check_elements(checker *check, trl_json_span node, unsigned int depth) {
    trl_json_container container;
    if (!trl_json_container_at(node, &container)) {
        return damaged(check);
    }
    size_t expected = 0U;
    for (uint32_t i = 0U; i < container.count; ++i) {
        if (trl_json_offset(container, i) != expected) {
            return damaged(check);
        }
        size_t taken = 0U;
        trl_json_span element = trl_json_span_from(container.contents, expected);
        if (element.length == 0U || !check_node(check, element, depth + 1U, &taken)) {
            return damaged(check);
        }
        expected += taken;
    }
    return expected == container.contents.length || damaged(check);
}

/* Checks the directory lists each member once, sorted by key, then by
   where it is; `starts` are where the members begin, in order. */
static bool check_directory(checker *check, trl_json_container container, const size_t *starts) {
    trl_json_span previous_key = {NULL, 0U};
    size_t previous_at = 0U;
    for (uint32_t i = 0U; i < container.count; ++i) {
        size_t at = trl_json_offset(container, i);
        size_t low = 0U;
        size_t high = container.count;
        while (low < high) {
            size_t middle = low + ((high - low) >> 1U);
            if (starts[middle] < at) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        trl_json_span key = {NULL, 0U};
        if (low == container.count || starts[low] != at || !trl_json_string_at(container.contents, at, &key)) {
            return damaged(check);
        }
        int order = trl_json_compare_keys(previous_key, key);
        if (i > 0U && (order > 0 || (order == 0 && previous_at >= at))) {
            return damaged(check);
        }
        previous_key = key;
        previous_at = at;
    }
    return true;
}

/* Checks each member, its key and value, filling the object's size. */
static bool check_members(checker *check, trl_json_container container, size_t *starts, unsigned int depth) {
    size_t at = 0U;
    for (uint32_t i = 0U; i < container.count; ++i) {
        trl_json_span key = {NULL, 0U};
        if (!trl_json_string_at(container.contents, at, &key) || !trl_utf8_valid(key.bytes, key.length)) {
            return damaged(check);
        }
        starts[i] = at;
        size_t value_at = at + TRL_JSON_WORD + key.length;
        trl_json_span value = trl_json_span_from(container.contents, value_at);
        size_t taken = 0U;
        if (value.length == 0U || !check_node(check, value, depth + 1U, &taken)) {
            return damaged(check);
        }
        at = value_at + taken;
    }
    return at == container.contents.length || damaged(check);
}

/* Checks an object: its members, then its directory. */
static bool check_object(checker *check, trl_json_span node, unsigned int depth) {
    trl_json_container container;
    if (!trl_json_container_at(node, &container)) {
        return damaged(check);
    }
    size_t *starts = trl_malloc((container.count == 0U ? 1U : container.count) * sizeof(size_t));
    if (starts == NULL) {
        check->result = TRL_JSON_NO_MEMORY;
        return false;
    }
    bool whole = check_members(check, container, starts, depth) && check_directory(check, container, starts);
    free(starts);
    return whole;
}

static bool check_node(checker *check, trl_json_span node, unsigned int depth, size_t *length) {
    if (depth > TRL_JSON_MAX_NESTING || !trl_json_node_length(node, length) || *length > node.length) {
        return damaged(check);
    }
    trl_json_span whole = {node.bytes, *length};
    switch (node.bytes[0]) {
    case TRL_JSON_STRING:
        return check_string(check, whole);
    case TRL_JSON_DATETIME:
        return check_datetime(check, whole);
    case TRL_JSON_DECFLOAT:
        return check_decfloat(check, whole);
    case TRL_JSON_ARRAY:
        return check_elements(check, whole, depth);
    case TRL_JSON_OBJECT:
        return check_object(check, whole, depth);
    default:
        return true;
    }
}

trl_json_check trl_json_check_document(const uint8_t *bytes, size_t length) {
    checker check = {TRL_JSON_WHOLE};
    if (bytes == NULL || length < TRL_JSON_LEAST_DOCUMENT || bytes[0] != TRL_JSON_FORMAT_VERSION) {
        return TRL_JSON_DAMAGED;
    }
    trl_json_span root = {bytes + 1, length - 1U};
    size_t taken = 0U;
    if (!check_node(&check, root, 0U, &taken)) {
        return check.result;
    }
    return taken == root.length ? TRL_JSON_WHOLE : TRL_JSON_DAMAGED;
}

bool trl_json_valid(const uint8_t *bytes, size_t length) {
    return trl_json_check_document(bytes, length) == TRL_JSON_WHOLE;
}
