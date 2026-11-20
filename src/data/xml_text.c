/**
 * @file xml_text.c
 * @brief An XML serialization before it is parsed [CLIENT XML values]: the
 * version its XML declaration names, its line ends normalized, and its
 * characters checked, by XML 1.0's rules or XML 1.1's.
 */
#include "codec/buffer.h"
#include "data/xml_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The first code point no control character is, and the delete. */
#define TRL_SPACE 0x20U
#define TRL_DELETE 0x7FU
/* UTF-8's lead byte of U+0080 to U+00BF, and the trail bytes of NEL and of
   the C1 controls XML 1.1 restricts, U+0080 to U+0084 and U+0086 to
   U+009F. */
#define TRL_LEAD_C1 0xC2U
#define TRL_NEL_TRAIL 0x85U
#define TRL_C1_FIRST 0x80U
#define TRL_C1_LAST 0x9FU
/* U+FFFE and U+FFFF, and XML 1.1's line ends NEL and U+2028, in UTF-8. */
#define TRL_NONCHAR_FFFE "\xef\xbf\xbe"
#define TRL_NONCHAR_FFFF "\xef\xbf\xbf"
#define TRL_THREE_BYTES 3U
#define TRL_NEL "\xc2\x85"
#define TRL_TWO_BYTES 2U
#define TRL_LINE_SEPARATOR "\xe2\x80\xa8"
/* What a declaration begins with, and the version of XML 1.1. */
#define TRL_DECLARATION "<?xml"
#define TRL_VERSION "version"
#define TRL_VERSION_11 "1.1"

/* Passes over white space in `text` from `at`. */
static size_t past_space(const char *text, size_t length, size_t at) {
    while (at < length && trl_xml_is_space(text[at])) {
        ++at;
    }
    return at;
}

/* Whether `value`, `length` bytes, is a VersionNum, '1.' then digits
   [XML 1.0 §2.8 [26]]. */
static bool is_version_number(const char *value, size_t length) {
    if (length <= strlen("1.") || memcmp(value, "1.", strlen("1.")) != 0) {
        return false;
    }
    for (size_t i = strlen("1."); i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
    }
    return true;
}

trl_xml_version_named trl_xml_version(const char *text, size_t length) {
    size_t at = strlen(TRL_DECLARATION);
    if (length <= at || memcmp(text, TRL_DECLARATION, at) != 0 || !trl_xml_is_space(text[at])) {
        return TRL_XML_VERSION_10;
    }
    at = past_space(text, length, at);
    if (length - at < strlen(TRL_VERSION) || memcmp(text + at, TRL_VERSION, strlen(TRL_VERSION)) != 0) {
        return TRL_XML_VERSION_10;
    }
    at = past_space(text, length, at + strlen(TRL_VERSION));
    if (at == length || text[at] != '=') {
        return TRL_XML_VERSION_NONE;
    }
    at = past_space(text, length, at + 1U);
    if (at == length || (text[at] != '"' && text[at] != '\'')) {
        return TRL_XML_VERSION_NONE;
    }
    char quote = text[at++];
    size_t began = at;
    while (at < length && text[at] != quote) {
        ++at;
    }
    if (at == length || !is_version_number(text + began, at - began)) {
        return TRL_XML_VERSION_NONE;
    }
    bool v11 = at - began == strlen(TRL_VERSION_11) && memcmp(text + began, TRL_VERSION_11, at - began) == 0;
    return v11 ? TRL_XML_VERSION_11 : TRL_XML_VERSION_10;
}

/* How many bytes of an XML 1.1 line end, NEL or U+2028, begin at `at`;
   0 if none does [XML 1.1 §2.11]. */
static size_t line_end_11(const char *text, size_t length, size_t at) {
    if (length - at >= TRL_TWO_BYTES && memcmp(text + at, TRL_NEL, TRL_TWO_BYTES) == 0) {
        return TRL_TWO_BYTES;
    }
    bool separator = length - at >= TRL_THREE_BYTES && memcmp(text + at, TRL_LINE_SEPARATOR, TRL_THREE_BYTES) == 0;
    return separator ? TRL_THREE_BYTES : 0U;
}

/* How many bytes of a line end begin at `at`: CR LF, CR, or of XML 1.1
   CR NEL, NEL or U+2028; 0 if none does [XML 1.0 \xc2\xa72.11]
   [XML 1.1 \xc2\xa72.11]. */
static size_t line_end(const char *text, size_t length, size_t at, bool v11) {
    if (text[at] != '\r') {
        return v11 ? line_end_11(text, length, at) : 0U;
    }
    if (at + 1U < length && text[at + 1U] == '\n') {
        return TRL_TWO_BYTES;
    }
    bool nel = v11 && at + 1U < length && line_end_11(text, length, at + 1U) == TRL_TWO_BYTES;
    return nel ? TRL_THREE_BYTES : 1U;
}

void trl_xml_normalize(const char *text, size_t length, bool v11, trl_buffer *out) {
    for (size_t i = 0U; i < length; ++i) {
        size_t ending = line_end(text, length, i, v11);
        if (ending > 0U) {
            trl_put_u8(out, (uint8_t)'\n');
            i += ending - 1U;
        } else {
            trl_put_u8(out, (uint8_t)text[i]);
        }
    }
}

/* Whether the bytes at `at` are a character XML 1.1 holds only by a
   character reference: the delete, or a C1 control but NEL
   [XML 1.1 §2.2 [2a]]. */
static bool restricted_11(const unsigned char *at, size_t left) {
    if (at[0] == TRL_DELETE) {
        return true;
    }
    return left >= TRL_TWO_BYTES && at[0] == TRL_LEAD_C1 && at[1] >= TRL_C1_FIRST && at[1] <= TRL_C1_LAST &&
           at[1] != TRL_NEL_TRAIL;
}

bool trl_xml_chars_allowed(parser *p) {
    for (size_t i = 0U; i < p->length; ++i) {
        const unsigned char *at = (const unsigned char *)p->text + i;
        size_t left = p->length - i;
        bool control = at[0] < TRL_SPACE && !trl_xml_is_space((char)at[0]);
        bool nonchar = left >= TRL_THREE_BYTES && (memcmp(at, TRL_NONCHAR_FFFE, TRL_THREE_BYTES) == 0 ||
                                                   memcmp(at, TRL_NONCHAR_FFFF, TRL_THREE_BYTES) == 0);
        if (control || nonchar) {
            return trl_xml_invalid(p, "the string holds a character no XML Char is [XML \xc2\xa7"
                                      "2.2 [2]]");
        }
        if (p->v11 && restricted_11(at, left)) {
            return trl_xml_invalid(p, "the string holds a restricted character, which XML 1.1 holds only by a "
                                      "character reference [XML 1.1 \xc2\xa7"
                                      "2.2 [2a]]");
        }
    }
    return true;
}
