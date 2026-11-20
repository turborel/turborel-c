/**
 * @file json_text.c
 * @brief A whole JSON document written as JSON text, as turborel's
 * `serialize` writes one (turborel's RULES.md D89, `json/serialize.rs`).
 */
#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/error.h"
#include "codec/int128.h"
#include "codec/json.h"
#include "codec/json_nodes.h"
#include "codec/numerals.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The last control character, U+001F, which a string escapes. */
#define TRL_LAST_CONTROL 0x1FU
/* Room for `\u` and four hexadecimal digits, and a null. */
#define TRL_ESCAPE_BYTES 8U
/* The characters of a two-character escape. */
#define TRL_SHORT_ESCAPE_BYTES 2U
/* Where a decimal node's scale is, after its tag and units. */
#define TRL_JSON_DECIMAL_SCALE_AT 9U

/* Appends a C string. */
static void put_text(trl_buffer *buffer, const char *text, size_t length) {
    trl_put_raw(buffer, (const uint8_t *)text, length);
}

/* An item that has no JSON text: SQLSTATE 22032. */
static bool no_text(trl_codec_error *error, const char *message) {
    trl_codec_fail(error, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_JSON_TEXT, message);
    return false;
}

/* The two-character escape of `c`, if RFC 8259 has one; else none. */
static const char *short_escape(uint8_t c) {
    switch (c) {
    case '"':
        return "\\\"";
    case '\\':
        return "\\\\";
    case '\b':
        return "\\b";
    case '\f':
        return "\\f";
    case '\n':
        return "\\n";
    case '\r':
        return "\\r";
    case '\t':
        return "\\t";
    default:
        return NULL;
    }
}

/* Appends a string, quoted and escaped as turborel escapes one. */
static void put_string(trl_buffer *buffer, trl_json_span text) {
    trl_put_u8(buffer, (uint8_t)'"');
    for (size_t i = 0U; i < text.length; ++i) {
        uint8_t c = text.bytes[i];
        const char *escape = short_escape(c);
        if (escape != NULL) {
            put_text(buffer, escape, TRL_SHORT_ESCAPE_BYTES);
        } else if (c <= TRL_LAST_CONTROL) {
            char unicode[TRL_ESCAPE_BYTES];
            int written = snprintf(unicode, sizeof unicode, "\\u%04x", (unsigned int)c);
            put_text(buffer, unicode, written > 0 ? (size_t)written : 0U);
        } else {
            trl_put_u8(buffer, c);
        }
    }
    trl_put_u8(buffer, (uint8_t)'"');
}

/* Appends an exact number as turborel's `Decimal` writes it. */
static void put_exact(trl_buffer *buffer, trl_decimal number) {
    char text[TRL_DECIMAL_TEXT_BYTES];
    if (trl_decimal_text(&number, text, sizeof text) == TRL_OK) {
        size_t length = 0U;
        while (text[length] != '\0') {
            ++length;
        }
        put_text(buffer, text, length);
    }
}

/* The exact number of an integer, decimal or wide decimal node. */
static trl_decimal exact_of(trl_json_span node) {
    trl_decimal number = {{0U, 0}, 0U};
    if (node.bytes[0] == TRL_JSON_WIDE) {
        uint32_t scale = 0U;
        number.units = trl_int128_of_bytes(node.bytes + 1);
        (void)trl_json_word_at(node, 1U + TRL_JSON_WIDE_WORD, &scale);
        number.scale = scale;
        return number;
    }
    int64_t units = trl_json_long_at(node, 1U);
    number.units.low = (uint64_t)units;
    number.units.high = units < 0 ? -1 : 0;
    number.scale = node.bytes[0] == TRL_JSON_DECIMAL ? node.bytes[TRL_JSON_DECIMAL_SCALE_AT] : 0U;
    return number;
}

static bool put_node(trl_buffer *buffer, trl_json_span node, trl_codec_error *error);

/* Appends an array's elements, in order. */
static bool put_array(trl_buffer *buffer, trl_json_span node, trl_codec_error *error) {
    trl_json_container container;
    (void)trl_json_container_at(node, &container);
    trl_put_u8(buffer, (uint8_t)'[');
    for (uint32_t i = 0U; i < container.count; ++i) {
        if (i > 0U) {
            trl_put_u8(buffer, (uint8_t)',');
        }
        trl_json_span element = trl_json_span_from(container.contents, trl_json_offset(container, i));
        if (!put_node(buffer, element, error)) {
            return false;
        }
    }
    trl_put_u8(buffer, (uint8_t)']');
    return true;
}

/* Appends an object's members, in the order written. */
static bool put_object(trl_buffer *buffer, trl_json_span node, trl_codec_error *error) {
    trl_json_container container;
    (void)trl_json_container_at(node, &container);
    trl_put_u8(buffer, (uint8_t)'{');
    size_t at = 0U;
    for (uint32_t i = 0U; i < container.count; ++i) {
        trl_json_span key = {NULL, 0U};
        (void)trl_json_string_at(container.contents, at, &key);
        if (i > 0U) {
            trl_put_u8(buffer, (uint8_t)',');
        }
        put_string(buffer, key);
        trl_put_u8(buffer, (uint8_t)':');
        trl_json_span value = trl_json_span_from(container.contents, at + TRL_JSON_WORD + key.length);
        size_t taken = 0U;
        if (!trl_json_node_length(value, &taken) || !put_node(buffer, value, error)) {
            return false;
        }
        at += TRL_JSON_WORD + key.length + taken;
    }
    trl_put_u8(buffer, (uint8_t)'}');
    return true;
}

/* Appends a number node. */
static bool put_number(trl_buffer *buffer, trl_json_span node, trl_codec_error *error) {
    if (node.bytes[0] == TRL_JSON_APPROXIMATE) {
        uint64_t bits = (uint64_t)trl_json_long_at(node, 1U);
        return trl_put_approximate_numeral(buffer, bits) ||
               no_text(error, "no JSON text parses to an approximate number that is not finite");
    }
    if (node.bytes[0] == TRL_JSON_DECFLOAT) {
        trl_decfloat made = {{0U, 0}, 0};
        (void)trl_decfloat_made(trl_json_decfloat_at(node), &made);
        trl_put_decfloat_numeral(buffer, made);
        return true;
    }
    put_exact(buffer, exact_of(node));
    return true;
}

static bool put_node(trl_buffer *buffer, trl_json_span node, trl_codec_error *error) {
    /* The node alone, not what follows it in its container. */
    size_t length = 0U;
    if (trl_json_node_length(node, &length) && length <= node.length) {
        node.length = length;
    }
    switch (node.bytes[0]) {
    case TRL_JSON_NULL:
        put_text(buffer, "null", sizeof "null" - 1U);
        return true;
    case TRL_JSON_FALSE:
        put_text(buffer, "false", sizeof "false" - 1U);
        return true;
    case TRL_JSON_TRUE:
        put_text(buffer, "true", sizeof "true" - 1U);
        return true;
    case TRL_JSON_STRING: {
        trl_json_span text = {NULL, 0U};
        (void)trl_json_string_at(node, 1U, &text);
        put_string(buffer, text);
        return true;
    }
    case TRL_JSON_DATETIME:
        return no_text(error, "no JSON text parses to a datetime");
    case TRL_JSON_ARRAY:
        return put_array(buffer, node, error);
    case TRL_JSON_OBJECT:
        return put_object(buffer, node, error);
    default:
        return put_number(buffer, node, error);
    }
}

bool trl_json_to_text(trl_buffer *buffer, const uint8_t *bytes, size_t length, trl_codec_error *error) {
    trl_json_check check = trl_json_check_document(bytes, length);
    if (check == TRL_JSON_NO_MEMORY) {
        trl_codec_no_memory(error);
        return false;
    }
    if (check != TRL_JSON_WHOLE) {
        trl_codec_fail(error, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_JSON_TEXT, "a JSON value's octets are damaged");
        return false;
    }
    trl_json_span root = {bytes + 1, length - 1U};
    return put_node(buffer, root, error) && trl_buffer_ok(buffer, error);
}
