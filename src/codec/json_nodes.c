/**
 * @file json_nodes.c
 * @brief The nodes of a JSON value's stored form read in place, each read
 * bounded by the octets given, as turborel's `Node` reads them.
 */
#include "codec/json_nodes.h"

#include "codec/decfloat.h"
#include "codec/int128.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bits of one byte. */
#define TRL_BYTE_BITS 8U
/* Where a container's size is, after its tag and count. */
#define TRL_JSON_SIZE_AT 5U
/* Where its offsets begin, after its tag, count and size. */
#define TRL_JSON_OFFSETS_AT 9U

trl_json_span trl_json_span_from(trl_json_span span, size_t at) {
    trl_json_span rest = {NULL, 0U};
    if (at <= span.length) {
        rest.bytes = span.bytes + at;
        rest.length = span.length - at;
    }
    return rest;
}

bool trl_json_word_at(trl_json_span span, size_t at, uint32_t *out) {
    if (at > span.length || span.length - at < TRL_JSON_WORD) {
        return false;
    }
    uint32_t word = 0U;
    for (unsigned int i = 0U; i < TRL_JSON_WORD; ++i) {
        word |= (uint32_t)span.bytes[at + i] << (i * TRL_BYTE_BITS);
    }
    *out = word;
    return true;
}

int64_t trl_json_long_at(trl_json_span node, size_t at) {
    uint64_t bits = 0U;
    for (unsigned int i = 0U; i < TRL_JSON_LONG; ++i) {
        bits |= (uint64_t)node.bytes[at + i] << (i * TRL_BYTE_BITS);
    }
    return trl_bits_i64(bits);
}

/* The body's length of a container: its head, offsets and size. */
static bool container_length(trl_json_span node, size_t *out) {
    uint32_t count = 0U;
    uint32_t size = 0U;
    if (!trl_json_word_at(node, 1U, &count) || !trl_json_word_at(node, TRL_JSON_SIZE_AT, &size)) {
        return false;
    }
    /* Counted in 64 bits, so no count or size wraps a 32-bit size_t. */
    uint64_t length = (uint64_t)TRL_JSON_OFFSETS_AT + ((uint64_t)count * TRL_JSON_WORD) + size;
    if (length > SIZE_MAX) {
        return false;
    }
    *out = (size_t)length;
    return true;
}

bool trl_json_node_length(trl_json_span node, size_t *out) {
    uint32_t word = 0U;
    switch (node.bytes[0]) {
    case TRL_JSON_NULL:
    case TRL_JSON_FALSE:
    case TRL_JSON_TRUE:
        *out = 1U;
        return true;
    case TRL_JSON_INTEGER:
    case TRL_JSON_APPROXIMATE:
        *out = 1U + TRL_JSON_LONG;
        return true;
    case TRL_JSON_DECIMAL:
        *out = 1U + TRL_JSON_LONG + 1U;
        return true;
    case TRL_JSON_WIDE:
    case TRL_JSON_DECFLOAT:
        *out = 1U + TRL_JSON_WIDE_WORD + TRL_JSON_WORD;
        return true;
    case TRL_JSON_STRING:
        if (!trl_json_word_at(node, 1U, &word) || (uint64_t)word + 1U + TRL_JSON_WORD > SIZE_MAX) {
            return false;
        }
        *out = (size_t)word + 1U + TRL_JSON_WORD;
        return true;
    case TRL_JSON_DATETIME:
        *out = 1U + TRL_JSON_DATETIME_BODY;
        return true;
    case TRL_JSON_ARRAY:
    case TRL_JSON_OBJECT:
        return container_length(node, out);
    default:
        return false;
    }
}

bool trl_json_string_at(trl_json_span span, size_t at, trl_json_span *out) {
    uint32_t length = 0U;
    if (!trl_json_word_at(span, at, &length)) {
        return false;
    }
    size_t from = at + TRL_JSON_WORD;
    if (span.length - from < length) {
        return false;
    }
    out->bytes = span.bytes + from;
    out->length = length;
    return true;
}

bool trl_json_container_at(trl_json_span node, trl_json_container *out) {
    uint32_t size = 0U;
    if (!trl_json_word_at(node, 1U, &out->count) || !trl_json_word_at(node, TRL_JSON_SIZE_AT, &size)) {
        return false;
    }
    uint64_t first = (uint64_t)TRL_JSON_OFFSETS_AT + ((uint64_t)out->count * TRL_JSON_WORD);
    if (first > node.length || node.length - first != size) {
        return false;
    }
    out->offsets = node.bytes + TRL_JSON_OFFSETS_AT;
    out->contents.bytes = node.bytes + first;
    out->contents.length = size;
    return true;
}

size_t trl_json_offset(trl_json_container container, uint32_t i) {
    uint32_t offset = 0U;
    trl_json_span offsets = {container.offsets, (size_t)container.count * TRL_JSON_WORD};
    (void)trl_json_word_at(offsets, (size_t)i * TRL_JSON_WORD, &offset);
    return offset;
}

int trl_json_compare_keys(trl_json_span a, trl_json_span b) {
    size_t shorter = a.length < b.length ? a.length : b.length;
    int order = shorter == 0U ? 0 : memcmp(a.bytes, b.bytes, shorter);
    if (order != 0) {
        return order;
    }
    if (a.length < b.length) {
        return -1;
    }
    return a.length > b.length ? 1 : 0;
}

trl_decfloat trl_json_decfloat_at(trl_json_span node) {
    trl_decfloat value;
    value.coefficient = trl_int128_of_bytes(node.bytes + 1);
    uint32_t exponent = 0U;
    (void)trl_json_word_at(node, 1U + TRL_JSON_WIDE_WORD, &exponent);
    value.exponent = trl_bits_i32(exponent);
    return value;
}
