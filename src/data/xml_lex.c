/**
 * @file xml_lex.c
 * @brief The XML parser's lexical pieces [CLIENT XML values]: places, white
 * space, names, character and entity references, and XML's characters.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "data/xml_parser.h"
#include "data/xml_tree.h"
#include "failure.h"
#include "format.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The SQLSTATE of a string that is not well-formed XML content, and the
   rule turborel cites of it. */
#define TRL_SQLSTATE_INVALID_XML_CONTENT "2200N"
#define TRL_XML_RULE                                                                                                   \
    "ISO/IEC 9075-14:2006 draft \xc2\xa7"                                                                              \
    "10.16 GR6a ii"
/* The most code point Unicode has, and its surrogates. */
#define TRL_MAX_CODE_POINT 0x10FFFFU
#define TRL_SURROGATES_FIRST 0xD800U
#define TRL_SURROGATES_LAST 0xDFFFU
#define TRL_FFFE 0xFFFEU
#define TRL_SPACE 0x20U
#define TRL_DECIMAL 10U
#define TRL_HEX 16U
#define TRL_HEX_LETTER 10U
/* UTF-8's lead bytes and their bits. */
#define TRL_ONE_BYTE 0x80U
#define TRL_TWO_BYTES 0x800U
#define TRL_THREE_BYTES 0x10000U
#define TRL_LEAD_TWO 0xC0U
#define TRL_LEAD_THREE 0xE0U
#define TRL_LEAD_FOUR 0xF0U
#define TRL_TRAIL 0x80U
#define TRL_SIX_BITS 0x3FU
#define TRL_BITS 6U
#define TRL_TWO_SHIFTS 12U
#define TRL_THREE_SHIFTS 18U

/* Fails: the string is not well-formed XML content, and why. */
bool trl_xml_invalid(parser *p, const char *why) {
    if (!p->failed) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "the string is not well-formed XML content: %s", why);
        trl_failure_cite(&p->xml->failure, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_XML_CONTENT, message, TRL_XML_RULE);
        p->failed = true;
    }
    return false;
}

/* Fails: the value is past turborel's limits, and why. */
bool trl_xml_beyond(parser *p, const char *why) {
    if (!p->failed) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "the XML value is beyond turborel's limits: %s", why);
        trl_failure_set(&p->xml->failure, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED, message);
        p->failed = true;
    }
    return false;
}

/* Counts a node or a binding made. */
bool trl_xml_counted(parser *p) {
    if (p->nodes == TRL_XML_MAX_NODES) {
        char why[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(why, sizeof why, "it has more than %lu nodes and bindings", (unsigned long)TRL_XML_MAX_NODES);
        return trl_xml_beyond(p, why);
    }
    ++p->nodes;
    return true;
}

/* Fails as memory not had. */
bool trl_xml_no_memory(parser *p) {
    if (!p->failed) {
        trl_failure_no_memory(&p->xml->failure);
        p->failed = true;
    }
    return false;
}

/* Whether the text at the parser's place begins with `prefix`. */
bool trl_xml_at(const parser *p, const char *prefix) {
    size_t length = strlen(prefix);
    return p->length - p->at >= length && memcmp(p->text + p->at, prefix, length) == 0;
}

/* Whether `c` is XML's white space. */
bool trl_xml_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Passes over white space; whether there was any. */
bool trl_xml_skip_space(parser *p) {
    size_t began = p->at;
    while (p->at < p->length && trl_xml_is_space(p->text[p->at])) {
        ++p->at;
    }
    return p->at > began;
}

/* A copy of `length` bytes in the tree's arena, null-ended; null if memory
   cannot be had. */
char *trl_xml_keep(parser *p, const char *bytes, size_t length) {
    if (length > TRL_XML_MAX_TEXT - p->held) {
        char why[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(why, sizeof why, "it holds more than %lu octets of text", (unsigned long)TRL_XML_MAX_TEXT);
        (void)trl_xml_beyond(p, why);
        return NULL;
    }
    p->held += length;
    char *copy = trl_arena_alloc(&p->xml->arena, length + 1U, 1U);
    if (copy == NULL) {
        (void)trl_xml_no_memory(p);
        return NULL;
    }
    if (length > 0U) {
        memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    return copy;
}

/* Appends code point `c` to `out` as UTF-8. */
static void put_code_point(trl_buffer *out, uint32_t c) {
    if (c < TRL_ONE_BYTE) {
        trl_put_u8(out, (uint8_t)c);
    } else if (c < TRL_TWO_BYTES) {
        trl_put_u8(out, (uint8_t)(TRL_LEAD_TWO | (c >> TRL_BITS)));
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | (c & TRL_SIX_BITS)));
    } else if (c < TRL_THREE_BYTES) {
        trl_put_u8(out, (uint8_t)(TRL_LEAD_THREE | (c >> TRL_TWO_SHIFTS)));
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | ((c >> TRL_BITS) & TRL_SIX_BITS)));
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | (c & TRL_SIX_BITS)));
    } else {
        trl_put_u8(out, (uint8_t)(TRL_LEAD_FOUR | (c >> TRL_THREE_SHIFTS)));
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | ((c >> TRL_TWO_SHIFTS) & TRL_SIX_BITS)));
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | ((c >> TRL_BITS) & TRL_SIX_BITS)));
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | (c & TRL_SIX_BITS)));
    }
}

/* Whether code point `c` is a Char of XML 1.0, or of 1.1 by a character
   reference [XML 1.0 \xc2\xa72.2 [2]] [XML 1.1 \xc2\xa72.2 [2]]. */
static bool is_char(uint32_t c, bool v11) {
    if (c < TRL_SPACE) {
        return v11 ? c > 0U : c == '\t' || c == '\n' || c == '\r';
    }
    return c <= TRL_MAX_CODE_POINT && !(c >= TRL_SURROGATES_FIRST && c <= TRL_SURROGATES_LAST) && c != TRL_FFFE &&
           c != TRL_FFFE + 1U;
}

/* The value of digit `c` in `base`, or `base` if it is none. */
static uint32_t digit_of(char c, uint32_t base) {
    uint32_t value = base;
    if (c >= '0' && c <= '9') {
        value = (uint32_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
        value = (uint32_t)(c - 'a') + TRL_HEX_LETTER;
    } else if (c >= 'A' && c <= 'F') {
        value = (uint32_t)(c - 'A') + TRL_HEX_LETTER;
    }
    return value < base ? value : base;
}

/* Reads a character reference after "&#", to its ";" [XML §4.1 [66]]. */
static bool char_reference(parser *p, trl_buffer *out) {
    uint32_t base = TRL_DECIMAL;
    if (p->at < p->length && p->text[p->at] == 'x') {
        base = TRL_HEX;
        ++p->at;
    }
    uint32_t c = 0U;
    size_t began = p->at;
    while (p->at < p->length && digit_of(p->text[p->at], base) < base) {
        c = (c * base) + digit_of(p->text[p->at], base);
        if (c > TRL_MAX_CODE_POINT) {
            return trl_xml_invalid(p, "a character reference names no character [XML \xc2\xa7"
                                      "4.1]");
        }
        ++p->at;
    }
    if (p->at == began || p->at == p->length || p->text[p->at] != ';') {
        return trl_xml_invalid(p, "a character reference is malformed [XML \xc2\xa7"
                                  "4.1 [66]]");
    }
    ++p->at;
    if (!is_char(c, p->v11)) {
        return trl_xml_invalid(p, "a character reference names no Char [XML \xc2\xa7"
                                  "2.2]");
    }
    put_code_point(out, c);
    return true;
}

/* Reads a reference after "&": a character's, or a predefined entity's. */
bool trl_xml_reference(parser *p, trl_buffer *out) {
    static const char *const names[] = {"lt;", "gt;", "amp;", "apos;", "quot;"};
    static const char replacements[] = {'<', '>', '&', '\'', '"'};
    ++p->at;
    if (trl_xml_at(p, "#")) {
        ++p->at;
        return char_reference(p, out);
    }
    for (size_t i = 0U; i < sizeof replacements; ++i) {
        if (trl_xml_at(p, names[i])) {
            p->at += strlen(names[i]);
            trl_put_u8(out, (uint8_t)replacements[i]);
            return true;
        }
    }
    return trl_xml_invalid(p, "an entity is referred to that is not declared [XML \xc2\xa7"
                              "4.1 WFC: Entity Declared]");
}

/* Whether `c` may begin a name [XML §2.3 [4]]: a letter, "_", ":" or a
   character past ASCII. */
static bool name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' || (unsigned char)c >= TRL_ONE_BYTE;
}

/* Whether `c` may be in a name [XML §2.3 [4a]]. */
static bool name_char(char c) {
    return name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

/* Reads a name; null if there is none. */
const char *trl_xml_read_name(parser *p) {
    if (p->at == p->length || !name_start(p->text[p->at])) {
        (void)trl_xml_invalid(p, "a name was expected [XML \xc2\xa7"
                                 "2.3 [5]]");
        return NULL;
    }
    size_t began = p->at;
    while (p->at < p->length && name_char(p->text[p->at])) {
        ++p->at;
    }
    return trl_xml_keep(p, p->text + began, p->at - began);
}
