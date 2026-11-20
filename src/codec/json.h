/**
 * @file json.h
 * @brief A JSON value's stored form, which a value of tag 25 carries
 * [PROTOCOL Values]: the binary tree of turborel's RULES.md D89, checked
 * whole, written as JSON text, and made from JSON text.
 *
 * A document is a version octet, 1, then its root node. A node is a tag
 * octet and its body, integers little-endian: 0 null, 1 false, 2 true; 3
 * an integer, i64; 4 a decimal, units i64 and scale u8; 5 a wide decimal,
 * units i128 and scale u32; 6 a DECFLOAT, coefficient i128 and exponent
 * i32; 7 an approximate number, an f64's bits; 8 a string, its octets u32
 * then UTF-8; 9 a datetime, moment u8, instant i64, zone i16, zoned u8,
 * precision u8; 10 an array and 11 an object, each a count u32, a size
 * u32, an offset u32 of each element or member from the first, then the
 * elements, or the members, each a key's octets u32, the key and a value
 * node. An object's offsets are its key directory, sorted by key, octet
 * by octet, and of one key in the order written; its members stay in the
 * order written. Arrays and objects nest at most 128 deep.
 */
#ifndef TRL_CODEC_JSON_H
#define TRL_CODEC_JSON_H

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The version of the stored form, a document's first octet. */
#define TRL_JSON_FORMAT_VERSION 1U
/** @brief How deep arrays and objects nest, turborel's MAX_NESTING. */
#define TRL_JSON_MAX_NESTING 128U

/** @brief What checking a document found. */
typedef enum trl_json_check {
    /** @brief It is whole and of the format. */
    TRL_JSON_WHOLE = 0,
    /** @brief Its octets are damaged. */
    TRL_JSON_DAMAGED,
    /** @brief Memory to check it could not be had. */
    TRL_JSON_NO_MEMORY,
} trl_json_check;

/**
 * @brief Checks a document whole, as turborel's `binary::validate` does:
 * each node within it, every string UTF-8, every DECFLOAT one turborel
 * makes, every offset an element's or a member's, each container's size
 * its contents', each key directory sorted, nothing after the root.
 * @param bytes The document; may be null if `length` is 0.
 * @param length Its octets.
 * @return What it found.
 */
trl_json_check trl_json_check_document(const uint8_t *bytes, size_t length);

/**
 * @brief Whether a document is whole and of the format.
 * @param bytes The document.
 * @param length Its octets.
 * @return Nonzero if trl_json_check_document() finds it whole.
 */
bool trl_json_valid(const uint8_t *bytes, size_t length);

/**
 * @brief Appends the JSON text of a whole document, as turborel serializes
 * one: no white space; members in the order written; a string's `"` and
 * `\` escaped, and its control characters, by their two-character escape
 * where RFC 8259 has one, else `\u` and four lower-case hexadecimal
 * digits; an exact number's digits with its scale's decimal point; a
 * DECFLOAT plain, without an exponent; an approximate number as its
 * shortest numeral with an exponent.
 * @param buffer Where the text goes, with no null after it.
 * @param bytes The document, which trl_json_valid() finds whole.
 * @param length Its octets.
 * @param error Where a failure is recorded.
 * @return Nonzero if written. A datetime, or an approximate number that is
 * not finite, has no JSON text: SQLSTATE 22032.
 */
bool trl_json_to_text(trl_buffer *buffer, const uint8_t *bytes, size_t length, trl_codec_error *error);

/**
 * @brief Appends the document of a JSON text, as turborel parses one
 * (RFC 8259): a number without an exponent exact, of more digits than an
 * exact number holds a DECFLOAT, one with an exponent approximate; members
 * of one key all kept, in the order written.
 * @param buffer Where the document goes.
 * @param text The text, UTF-8; may be null if `length` is 0.
 * @param length Its octets.
 * @param arena Where the parse is held while the document is written.
 * @param error Where a failure is recorded.
 * @return Nonzero if written. A text that is not JSON, or a lone surrogate,
 * is SQLSTATE 22032; a number no type of turborel's holds exactly 22003;
 * arrays and objects nested past 128, SQLCODE -900.
 */
bool trl_json_from_text(trl_buffer *buffer, const uint8_t *text, size_t length, trl_arena *arena,
                        trl_codec_error *error);

#endif
