/**
 * @file json_nodes.h
 * @brief The nodes of a JSON value's stored form (`json.h`), read in place:
 * their tags, lengths, containers, offsets and keys, every read bounded by
 * the octets given (turborel's RULES.md D89).
 */
#ifndef TRL_CODEC_JSON_NODES_H
#define TRL_CODEC_JSON_NODES_H

#include "codec/decfloat.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The SQL/JSON null. */
#define TRL_JSON_NULL 0U
/** @brief False. */
#define TRL_JSON_FALSE 1U
/** @brief True. */
#define TRL_JSON_TRUE 2U
/** @brief An exact number of scale 0 an i64 holds. */
#define TRL_JSON_INTEGER 3U
/** @brief An exact number whose units an i64 holds, of a scale a u8 holds. */
#define TRL_JSON_DECIMAL 4U
/** @brief Any other exact number. */
#define TRL_JSON_WIDE 5U
/** @brief A DECFLOAT. */
#define TRL_JSON_DECFLOAT 6U
/** @brief An approximate number. */
#define TRL_JSON_APPROXIMATE 7U
/** @brief A string. */
#define TRL_JSON_STRING 8U
/** @brief A datetime. */
#define TRL_JSON_DATETIME 9U
/** @brief An array. */
#define TRL_JSON_ARRAY 10U
/** @brief An object. */
#define TRL_JSON_OBJECT 11U

/** @brief The octets of a count, a size, an offset or a string's length. */
#define TRL_JSON_WORD 4U
/** @brief The octets of an i64, an f64 or a datetime's instant. */
#define TRL_JSON_LONG 8U
/** @brief The octets of an i128. */
#define TRL_JSON_WIDE_WORD 16U
/** @brief A datetime's body: moment, instant, zone, zoned and precision. */
#define TRL_JSON_DATETIME_BODY 13U
/** @brief Where in a datetime node its moment is, after the tag. */
#define TRL_JSON_DATETIME_MOMENT_AT 1U
/** @brief Where in a datetime node its zoned flag is. */
#define TRL_JSON_DATETIME_ZONED_AT 12U
/** @brief The greatest moment: 0 a date, 1 a time, 2 a timestamp. */
#define TRL_JSON_MOMENT_TIMESTAMP 2U

/** @brief Octets of a document, read in place. */
typedef struct trl_json_span {
    /** @brief The first. */
    const uint8_t *bytes;
    /** @brief How many. */
    size_t length;
} trl_json_span;

/** @brief An array's or an object's parts. */
typedef struct trl_json_container {
    /** @brief How many elements or members. */
    uint32_t count;
    /** @brief The offsets, `count` words. */
    const uint8_t *offsets;
    /** @brief The elements or members, the container's size of octets. */
    trl_json_span contents;
} trl_json_container;

/**
 * @brief The span from `at` of `span`; empty past its end.
 * @param span The octets.
 * @param at Where it begins.
 * @return The rest.
 */
trl_json_span trl_json_span_from(trl_json_span span, size_t at);

/**
 * @brief The little-endian u32 at `at` of `span`.
 * @param span The octets.
 * @param at Where it begins.
 * @param out The word.
 * @return Nonzero if four octets are there.
 */
bool trl_json_word_at(trl_json_span span, size_t at, uint32_t *out);

/**
 * @brief The octets a node takes, its tag included.
 * @param node The node, from its tag to the document's end.
 * @param out Its length.
 * @return Nonzero if its tag is a node's and its length's fields are there.
 */
bool trl_json_node_length(trl_json_span node, size_t *out);

/**
 * @brief The string, its length then its octets, at `at` of `span`.
 * @param span The octets.
 * @param at Where its length begins.
 * @param out Its octets.
 * @return Nonzero if they are all there.
 */
bool trl_json_string_at(trl_json_span span, size_t at, trl_json_span *out);

/**
 * @brief An array's or an object's count, offsets and contents.
 * @param node The node, exactly its length.
 * @param out Its parts.
 * @return Nonzero if they fill it.
 */
bool trl_json_container_at(trl_json_span node, trl_json_container *out);

/**
 * @brief The `i`th offset of a container.
 * @param container The container.
 * @param i Which, below its count.
 * @return The offset.
 */
size_t trl_json_offset(trl_json_container container, uint32_t i);

/**
 * @brief The order of two keys, octet by octet, as `memcmp` then length.
 * @param a One.
 * @param b The other.
 * @return Below, at or above zero as `a` sorts before, with or after `b`.
 */
int trl_json_compare_keys(trl_json_span a, trl_json_span b);

/**
 * @brief The DECFLOAT of a DECFLOAT node, as its octets hold it.
 * @param node The node, its length at least 21.
 * @return Its coefficient and exponent.
 */
trl_decfloat trl_json_decfloat_at(trl_json_span node);

/**
 * @brief The little-endian i64 at `at` of a node whose length covers it.
 * @param node The node.
 * @param at Where it begins.
 * @return The integer.
 */
int64_t trl_json_long_at(trl_json_span node, size_t at);

#endif
