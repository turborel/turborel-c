/**
 * @file json_write.c
 * @brief Parsed items written as a stored document, as turborel's
 * `binary/write.rs` writes one: each node after its tag, an exact number
 * in the narrowest of its three forms, a container's offsets reserved and
 * filled once its contents are written, an object's directory sorted by
 * key and, of one key, in the order written.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/json_nodes.h"
#include "codec/json_tree.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The most octets a document holds, turborel's MAX_DOCUMENT_OCTETS. */
#define TRL_JSON_MAX_DOCUMENT 268435456U

/* The runs a merge sort merges at a time: two. */
#define TRL_MERGE_WAYS 2U

/* A member of a directory being sorted: its key and where it is. */
typedef struct entry {
    trl_json_span key;
    uint32_t at;
} entry;

static bool write_node(trl_buffer *buffer, const trl_json_item *item, trl_arena *arena);

/* Writes a string: its octets' count, then them. */
static void write_string(trl_buffer *buffer, trl_json_span text) {
    trl_put_count(buffer, text.length);
    trl_put_raw(buffer, text.bytes, text.length);
}

/* Writes an exact number in the narrowest form that holds it. */
static void write_exact(trl_buffer *buffer, trl_decimal number) {
    bool narrow = (number.units.high == 0 && number.units.low <= (uint64_t)INT64_MAX) ||
                  (number.units.high == -1 && number.units.low > (uint64_t)INT64_MAX);
    if (narrow && number.scale <= UINT8_MAX) {
        trl_put_u8(buffer, number.scale == 0U ? TRL_JSON_INTEGER : TRL_JSON_DECIMAL);
        trl_put_u64(buffer, number.units.low);
        if (number.scale != 0U) {
            trl_put_u8(buffer, (uint8_t)number.scale);
        }
        return;
    }
    trl_put_u8(buffer, TRL_JSON_WIDE);
    trl_put_i128(buffer, number.units);
    trl_put_u32(buffer, number.scale);
}

/* Merges the sorted runs `from[low..middle)` and `from[middle..high)` into
   `to`, the left first of equal keys, so the sort is stable. */
static void merge(const entry *from, entry *to, size_t low, size_t middle, size_t high) {
    size_t left = low;
    size_t right = middle;
    for (size_t i = low; i < high; ++i) {
        bool take_left =
            left < middle && (right >= high || trl_json_compare_keys(from[left].key, from[right].key) <= 0);
        to[i] = take_left ? from[left++] : from[right++];
    }
}

/* Sorts `entries` by key, stably, bottom-up, through `spare`; gives where
   the sorted entries are. */
static entry *sort_entries(entry *entries, entry *spare, size_t count) {
    entry *from = entries;
    entry *to = spare;
    for (size_t width = 1U; width < count; width *= TRL_MERGE_WAYS) {
        for (size_t low = 0U; low < count; low += TRL_MERGE_WAYS * width) {
            size_t middle = low + width < count ? low + width : count;
            size_t high = low + (TRL_MERGE_WAYS * width) < count ? low + (TRL_MERGE_WAYS * width) : count;
            merge(from, to, low, middle, high);
        }
        entry *swap = from;
        from = to;
        to = swap;
    }
    return from;
}

/* Reserves a container's head and offsets: where its size and its first
   offset are. */
static size_t open_container(trl_buffer *buffer, const trl_json_item *item, size_t *offsets) {
    trl_put_u8(buffer, item->tag);
    trl_put_u32(buffer, item->as.container.count);
    size_t size_at = buffer->length;
    trl_put_u32(buffer, 0U);
    *offsets = buffer->length;
    for (uint32_t i = 0U; i < item->as.container.count; ++i) {
        trl_put_u32(buffer, 0U);
    }
    return size_at;
}

/* Writes an array: its elements, each offset filled as it is written. */
static bool write_array(trl_buffer *buffer, const trl_json_item *item, trl_arena *arena) {
    size_t offsets = 0U;
    size_t size_at = open_container(buffer, item, &offsets);
    size_t first = buffer->length;
    uint32_t i = 0U;
    for (const trl_json_item *element = item->as.container.first; element != NULL; element = element->next) {
        trl_patch_u32(buffer, offsets + ((size_t)i * TRL_JSON_WORD), (uint32_t)(buffer->length - first));
        if (!write_node(buffer, element, arena)) {
            return false;
        }
        ++i;
    }
    trl_patch_u32(buffer, size_at, (uint32_t)(buffer->length - first));
    return true;
}

/* Writes an object: its members in the order given, then its directory,
   sorted by key, into the offsets reserved. */
static bool write_object(trl_buffer *buffer, const trl_json_item *item, trl_arena *arena) {
    uint32_t count = item->as.container.count;
    entry *entries = trl_arena_alloc(arena, ((size_t)count * TRL_MERGE_WAYS) + 1U, sizeof(entry));
    if (entries == NULL) {
        buffer->state = TRL_BUFFER_NO_MEMORY;
        return false;
    }
    size_t offsets = 0U;
    size_t size_at = open_container(buffer, item, &offsets);
    size_t first = buffer->length;
    uint32_t i = 0U;
    for (const trl_json_item *member = item->as.container.first; member != NULL && i < count; member = member->next) {
        entries[i].key = member->key;
        entries[i].at = (uint32_t)(buffer->length - first);
        write_string(buffer, member->key);
        if (!write_node(buffer, member, arena)) {
            return false;
        }
        ++i;
    }
    entry *sorted = count == 0U ? entries : sort_entries(entries, entries + count, count);
    for (uint32_t j = 0U; j < count; ++j) {
        trl_patch_u32(buffer, offsets + ((size_t)j * TRL_JSON_WORD), sorted[j].at);
    }
    trl_patch_u32(buffer, size_at, (uint32_t)(buffer->length - first));
    return true;
}

static bool write_node(trl_buffer *buffer, const trl_json_item *item, trl_arena *arena) {
    switch (item->tag) {
    case TRL_JSON_WIDE:
        write_exact(buffer, item->as.exact);
        return true;
    case TRL_JSON_DECFLOAT:
        trl_put_u8(buffer, TRL_JSON_DECFLOAT);
        trl_put_i128(buffer, item->as.decfloat.coefficient);
        trl_put_u32(buffer, (uint32_t)item->as.decfloat.exponent);
        return true;
    case TRL_JSON_APPROXIMATE:
        trl_put_u8(buffer, TRL_JSON_APPROXIMATE);
        trl_put_u64(buffer, item->as.approximate);
        return true;
    case TRL_JSON_STRING:
        trl_put_u8(buffer, TRL_JSON_STRING);
        write_string(buffer, item->as.string);
        return true;
    case TRL_JSON_ARRAY:
        return write_array(buffer, item, arena);
    case TRL_JSON_OBJECT:
        return write_object(buffer, item, arena);
    default:
        trl_put_u8(buffer, item->tag);
        return true;
    }
}

bool trl_json_write_document(trl_buffer *buffer, const trl_json_item *root, trl_arena *arena, trl_codec_error *error) {
    size_t start = buffer->length;
    trl_put_u8(buffer, TRL_JSON_FORMAT_VERSION);
    if (!write_node(buffer, root, arena) || !trl_buffer_ok(buffer, error)) {
        return trl_buffer_ok(buffer, error);
    }
    if (buffer->length - start > TRL_JSON_MAX_DOCUMENT) {
        trl_codec_fail(error, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                       "a JSON value's stored form is longer than 256 MiB");
        return false;
    }
    return true;
}
