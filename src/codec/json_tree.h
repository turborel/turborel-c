/**
 * @file json_tree.h
 * @brief A JSON text parsed, before it is written as a stored document
 * (`json_parse.c`, `json_write.c`): an item for each value, in an arena.
 */
#ifndef TRL_CODEC_JSON_TREE_H
#define TRL_CODEC_JSON_TREE_H

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/error.h"
#include "codec/json_nodes.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief An item of a parsed text. */
typedef struct trl_json_item trl_json_item;

/** @brief An item, by the node tag it is written with (`json_nodes.h`). */
struct trl_json_item {
    /** @brief Its node's tag; of an exact number, TRL_JSON_WIDE until written. */
    uint8_t tag;
    /** @brief The next element or member of its container, in order. */
    trl_json_item *next;
    /** @brief Of a member, its key, unescaped. */
    trl_json_span key;
    /** @brief Its value. */
    union {
        /** @brief An exact number. */
        trl_decimal exact;
        /** @brief A DECFLOAT, canonical. */
        trl_decfloat decfloat;
        /** @brief An approximate number's bits. */
        uint64_t approximate;
        /** @brief A string, unescaped. */
        trl_json_span string;
        /** @brief An array's elements or an object's members. */
        struct {
            trl_json_item *first;
            uint32_t count;
        } container;
    } as;
};

/**
 * @brief Writes a parsed item as a stored document, after its version.
 * @param buffer Where the document goes.
 * @param root The item.
 * @param arena Where the key directories are sorted.
 * @param error Where a failure is recorded.
 * @return Nonzero if written.
 */
bool trl_json_write_document(trl_buffer *buffer, const trl_json_item *root, trl_arena *arena, trl_codec_error *error);

#endif
