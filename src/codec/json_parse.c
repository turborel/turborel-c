/**
 * @file json_parse.c
 * @brief JSON text parsed by RFC 8259's grammar into items, as turborel's
 * `json/text.rs` parses it: a number without an exponent exact, of more
 * digits than an exact number holds a DECFLOAT, one with an exponent
 * approximate; members of one key all kept, in the order written.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/error.h"
#include "codec/int128.h"
#include "codec/json.h"
#include "codec/json_nodes.h"
#include "codec/json_tree.h"
#include "codec/strtod.h"
#include "codec/utf8.h"
#include "turborel/turborel.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The most digits, and the greatest scale, of an exact number, turborel's
   MAX_EXACT_PRECISION. */
#define TRL_MAX_EXACT_DIGITS 18U
/* The most significant digits of a DECFLOAT, turborel's MAX_DECFLOAT_PRECISION. */
#define TRL_MAX_DECFLOAT_DIGITS 34U
/* The last control character, which a string holds only escaped. */
#define TRL_LAST_CONTROL 0x1FU
/* A \u escape's four hexadecimal digits [RFC 8259 §7]. */
#define TRL_HEX_DIGITS 4U
#define TRL_HEX_BASE 16U
#define TRL_HEX_TEN 10U
/* The code units of a surrogate pair, and the bits each holds. */
#define TRL_HIGH_SURROGATE_FIRST 0xD800U
#define TRL_HIGH_SURROGATE_LAST 0xDBFFU
#define TRL_LOW_SURROGATE_FIRST 0xDC00U
#define TRL_LOW_SURROGATE_LAST 0xDFFFU
#define TRL_SURROGATE_BITS 10U
/* The first code point a surrogate pair counts from. */
#define TRL_SUPPLEMENTARY 0x10000U
/* UTF-8's bounds of one and two bytes, its lead bytes, and its six bits a
   continuation byte holds (RFC 3629). */
#define TRL_UTF8_ONE_LIMIT 0x80U
#define TRL_UTF8_TWO_LIMIT 0x800U
#define TRL_UTF8_LEAD2 0xC0U
#define TRL_UTF8_LEAD3 0xE0U
#define TRL_UTF8_LEAD4 0xF0U
#define TRL_UTF8_CONTINUATION 0x80U
#define TRL_UTF8_MASK 0x3FU
#define TRL_UTF8_SHIFT 6U
#define TRL_UTF8_SHIFT2 12U
#define TRL_UTF8_SHIFT3 18U
/* The characters of an escape's opening, a reverse solidus and a letter. */
#define TRL_ESCAPE_BYTES 2U

/* A text being parsed. */
typedef struct parser {
    const char *text;
    size_t length;
    size_t at;
    trl_arena *arena;
    trl_codec_error *error;
} parser;

/* The number's characters, as read. */
typedef struct numeral {
    bool negative;
    const char *whole;
    size_t whole_length;
    const char *fraction;
    size_t fraction_length;
    bool exponent;
    size_t start;
} numeral;

/* A text that is not JSON: SQLSTATE 22032. */
static bool invalid(parser *p, const char *why) {
    trl_codec_fail(p->error, TRL_SQLCODE_DATA, TRL_SQLSTATE_INVALID_JSON_TEXT, why);
    return false;
}

/* A number no type of turborel's holds exactly: SQLSTATE 22003. */
static bool out_of_range(parser *p, const char *why) {
    trl_codec_fail(p->error, TRL_SQLCODE_DATA, TRL_SQLSTATE_OUT_OF_RANGE, why);
    return false;
}

/* The character at the parser, or a null at the end. */
static char peek(const parser *p) {
    return p->at < p->length ? p->text[p->at] : '\0';
}

/* Skips white space [RFC 8259 §2]: space, tab, line feed, return. */
static void skip_space(parser *p) {
    while (p->at < p->length) {
        char c = p->text[p->at];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return;
        }
        ++p->at;
    }
}

/* Whether `c` is a decimal digit. */
static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Reads digits, at least one. */
static bool read_digits(parser *p, const char **start, size_t *count) {
    *start = p->text + p->at;
    size_t from = p->at;
    while (is_digit(peek(p))) {
        ++p->at;
    }
    *count = p->at - from;
    return *count > 0U || invalid(p, "a digit is missing from a number");
}

/* Reads a number's characters [RFC 8259 §6]. */
static bool read_numeral(parser *p, numeral *out) {
    out->start = p->at;
    out->negative = peek(p) == '-';
    if (out->negative) {
        ++p->at;
    }
    if (!read_digits(p, &out->whole, &out->whole_length)) {
        return false;
    }
    if (out->whole_length > 1U && out->whole[0] == '0') {
        return invalid(p, "a number has a leading zero");
    }
    if (peek(p) == '.') {
        ++p->at;
        if (!read_digits(p, &out->fraction, &out->fraction_length)) {
            return false;
        }
    }
    out->exponent = peek(p) == 'e' || peek(p) == 'E';
    if (!out->exponent) {
        return true;
    }
    ++p->at;
    if (peek(p) == '+' || peek(p) == '-') {
        ++p->at;
    }
    const char *ignored = NULL;
    size_t count = 0U;
    return read_digits(p, &ignored, &count);
}

/* The value of `count` digits from `digits`, which fit 128 bits. */
static trl_uint128 digits_value(const char *digits, size_t count) {
    trl_uint128 value = {0U, 0U};
    for (size_t i = 0U; i < count; ++i) {
        (void)trl_uint128_times_ten_plus(&value, (unsigned int)(digits[i] - '0'));
    }
    return value;
}

/* Sets `item` to the exact number of `digits` (a whole's and a fraction's,
   at most 18) and `scale`. */
static void set_exact(trl_json_item *item, const char *digits, size_t count, size_t scale, bool negative) {
    item->tag = TRL_JSON_WIDE;
    (void)trl_int128_of(digits_value(digits, count), negative, &item->as.exact.units);
    item->as.exact.scale = (uint32_t)scale;
}

/* Joins a numeral's whole and fraction digits into the arena. */
static char *joined_digits(parser *p, const numeral *n, size_t *count) {
    *count = n->whole_length + n->fraction_length;
    char *digits = trl_arena_alloc(p->arena, *count + 1U, 1U);
    if (digits == NULL) {
        trl_codec_no_memory(p->error);
        return NULL;
    }
    memcpy(digits, n->whole, n->whole_length);
    if (n->fraction_length > 0U) {
        memcpy(digits + n->whole_length, n->fraction, n->fraction_length);
    }
    return digits;
}

/* The significant digits of a number, of which `fraction` follow its
   point. */
typedef struct digit_run {
    const char *digits;
    size_t count;
    size_t fraction;
    bool negative;
} digit_run;

/* Sets `item` to the DECFLOAT of `run`. */
static bool set_decfloat(parser *p, trl_json_item *item, digit_run run) {
    size_t kept = run.count;
    while (kept > 0U && run.digits[kept - 1U] == '0') {
        --kept;
    }
    if (kept > TRL_MAX_DECFLOAT_DIGITS) {
        return out_of_range(p, "the number has more than 34 significant digits");
    }
    int64_t exponent = (int64_t)(run.count - kept) - (int64_t)run.fraction;
    if (exponent < INT32_MIN || exponent > INT32_MAX) {
        return out_of_range(p, "the number is out of the range of a decimal floating-point number");
    }
    trl_decfloat value = {{0U, 0}, (int32_t)exponent};
    (void)trl_int128_of(digits_value(run.digits, kept), run.negative, &value.coefficient);
    item->tag = TRL_JSON_DECFLOAT;
    return trl_decfloat_made(value, &item->as.decfloat) ||
           out_of_range(p, "the number is out of the range of a decimal floating-point number");
}

/* The number without an exponent, exact or a DECFLOAT, as turborel's
   `exact` makes it: its leading zeros, and its fraction's trailing ones,
   not counted where they made it too long. */
static bool exact_number(parser *p, const numeral *n, trl_json_item *item) {
    size_t count = 0U;
    char *digits = joined_digits(p, n, &count);
    if (digits == NULL) {
        return false;
    }
    if (count <= TRL_MAX_EXACT_DIGITS && n->fraction_length <= TRL_MAX_EXACT_DIGITS) {
        set_exact(item, digits, count, n->fraction_length, n->negative);
        return true;
    }
    digit_run run = {digits, n->whole_length, n->fraction_length, n->negative};
    while (run.fraction > 0U && n->fraction[run.fraction - 1U] == '0') {
        --run.fraction;
    }
    run.count += run.fraction;
    while (run.count > 0U && *run.digits == '0') {
        ++run.digits;
        --run.count;
    }
    if (run.count <= TRL_MAX_EXACT_DIGITS && run.fraction <= TRL_MAX_EXACT_DIGITS) {
        set_exact(item, run.digits, run.count, run.fraction, n->negative);
        return true;
    }
    return set_decfloat(p, item, run);
}

/* Whether a numeral's digits before its exponent are not all zeros. */
static bool any_digit_nonzero(const parser *p, const numeral *n) {
    for (size_t i = n->start; i < p->at && p->text[i] != 'e' && p->text[i] != 'E'; ++i) {
        if (p->text[i] >= '1' && p->text[i] <= '9') {
            return true;
        }
    }
    return false;
}

/* The number with an exponent, approximate, read in the "C" locale. */
static bool approximate_number(parser *p, const numeral *n, trl_json_item *item) {
    size_t length = p->at - n->start;
    char *copy = trl_arena_alloc(p->arena, length + 1U, 1U);
    double x = 0.0;
    if (copy == NULL) {
        trl_codec_no_memory(p->error);
        return false;
    }
    memcpy(copy, p->text + n->start, length);
    if (!trl_strtod_c(copy, &x)) {
        trl_codec_no_memory(p->error);
        return false;
    }
    if (!isfinite(x)) {
        return out_of_range(p, "the number is too great for an approximate number");
    }
    item->tag = TRL_JSON_APPROXIMATE;
    memcpy(&item->as.approximate, &x, sizeof x);
    if ((item->as.approximate << 1U) == 0U && any_digit_nonzero(p, n)) {
        return out_of_range(p, "the number is too small for an approximate number, but not zero");
    }
    return true;
}

/* Parses a number. */
static bool parse_number(parser *p, trl_json_item *item) {
    numeral n;
    memset(&n, 0, sizeof n);
    if (!read_numeral(p, &n)) {
        return false;
    }
    return n.exponent ? approximate_number(p, &n, item) : exact_number(p, &n, item);
}

static bool parse_string(parser *p, trl_json_span *out);
static bool parse_value(parser *p, trl_json_item *item, unsigned int depth);

/* The value of four hexadecimal digits at the parser; false if they are not. */
static bool read_hex(parser *p, uint32_t *out) {
    if (p->length - p->at < TRL_HEX_DIGITS) {
        return invalid(p, "a \\u escape has fewer than four hexadecimal digits");
    }
    uint32_t value = 0U;
    for (unsigned int i = 0U; i < TRL_HEX_DIGITS; ++i) {
        char c = p->text[p->at + i];
        uint32_t digit = 0U;
        if (is_digit(c)) {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (uint32_t)(c - 'a') + TRL_HEX_TEN;
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A') + TRL_HEX_TEN;
        } else {
            return invalid(p, "a \\u escape has a character that is not a hexadecimal digit");
        }
        value = (value * TRL_HEX_BASE) + digit;
    }
    p->at += TRL_HEX_DIGITS;
    *out = value;
    return true;
}

/* Reads a \u escape, after its \u, as a character: a surrogate pair is one
   character, and a surrogate not of a pair none [RFC 8259 §7]. */
static bool read_unicode(parser *p, uint32_t *out) {
    uint32_t unit = 0U;
    if (!read_hex(p, &unit)) {
        return false;
    }
    if (unit >= TRL_LOW_SURROGATE_FIRST && unit <= TRL_LOW_SURROGATE_LAST) {
        return invalid(p, "a string escapes a surrogate that is not of a pair");
    }
    if (unit < TRL_HIGH_SURROGATE_FIRST || unit > TRL_HIGH_SURROGATE_LAST) {
        *out = unit;
        return true;
    }
    uint32_t low = 0U;
    if (p->length - p->at < TRL_ESCAPE_BYTES || p->text[p->at] != '\\' || p->text[p->at + 1U] != 'u') {
        return invalid(p, "a string escapes a surrogate that is not of a pair");
    }
    p->at += TRL_ESCAPE_BYTES;
    if (!read_hex(p, &low)) {
        return false;
    }
    if (low < TRL_LOW_SURROGATE_FIRST || low > TRL_LOW_SURROGATE_LAST) {
        return invalid(p, "a string escapes a surrogate that is not of a pair");
    }
    *out =
        TRL_SUPPLEMENTARY + ((unit - TRL_HIGH_SURROGATE_FIRST) << TRL_SURROGATE_BITS) + (low - TRL_LOW_SURROGATE_FIRST);
    return true;
}

/* Appends `c` as UTF-8 at `*at` of `out`. */
static void put_utf8(char *out, size_t *at, uint32_t c) {
    if (c < TRL_UTF8_ONE_LIMIT) {
        out[(*at)++] = (char)c;
    } else if (c < TRL_UTF8_TWO_LIMIT) {
        out[(*at)++] = (char)(TRL_UTF8_LEAD2 | (c >> TRL_UTF8_SHIFT));
        out[(*at)++] = (char)(TRL_UTF8_CONTINUATION | (c & TRL_UTF8_MASK));
    } else if (c < TRL_SUPPLEMENTARY) {
        out[(*at)++] = (char)(TRL_UTF8_LEAD3 | (c >> TRL_UTF8_SHIFT2));
        out[(*at)++] = (char)(TRL_UTF8_CONTINUATION | ((c >> TRL_UTF8_SHIFT) & TRL_UTF8_MASK));
        out[(*at)++] = (char)(TRL_UTF8_CONTINUATION | (c & TRL_UTF8_MASK));
    } else {
        out[(*at)++] = (char)(TRL_UTF8_LEAD4 | (c >> TRL_UTF8_SHIFT3));
        out[(*at)++] = (char)(TRL_UTF8_CONTINUATION | ((c >> TRL_UTF8_SHIFT2) & TRL_UTF8_MASK));
        out[(*at)++] = (char)(TRL_UTF8_CONTINUATION | ((c >> TRL_UTF8_SHIFT) & TRL_UTF8_MASK));
        out[(*at)++] = (char)(TRL_UTF8_CONTINUATION | (c & TRL_UTF8_MASK));
    }
}

/* The character a one-letter escape stands for; 0 if `c` is none's. */
static char short_unescape(char c) {
    switch (c) {
    case '"':
    case '\\':
    case '/':
        return c;
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    default:
        return '\0';
    }
}

/* Where a string's closing quotation mark is, from the parser, its escaped
   characters skipped; the text's length if it has none. */
static size_t string_end(const parser *p) {
    size_t at = p->at;
    while (at < p->length && p->text[at] != '"') {
        at += p->text[at] == '\\' ? TRL_ESCAPE_BYTES : 1U;
    }
    return at;
}

/* Reads one escape, after its reverse solidus, into `out`. */
static bool read_escape(parser *p, char *out, size_t *written) {
    char c = peek(p);
    ++p->at;
    if (c == 'u') {
        uint32_t character = 0U;
        if (!read_unicode(p, &character)) {
            return false;
        }
        put_utf8(out, written, character);
        return true;
    }
    char plain = short_unescape(c);
    if (plain == '\0') {
        return invalid(p, "a string has an escape that is not one of JSON's");
    }
    out[(*written)++] = plain;
    return true;
}

static bool parse_string(parser *p, trl_json_span *out) {
    ++p->at;
    size_t end = string_end(p);
    if (end >= p->length) {
        return invalid(p, "a string is not closed");
    }
    char *text = trl_arena_alloc(p->arena, end - p->at + 1U, 1U);
    if (text == NULL) {
        trl_codec_no_memory(p->error);
        return false;
    }
    size_t written = 0U;
    while (p->at < end) {
        char c = p->text[p->at];
        if ((unsigned char)c <= TRL_LAST_CONTROL) {
            return invalid(p, "a string holds a control character unescaped");
        }
        if (c != '\\') {
            text[written++] = c;
            ++p->at;
        } else {
            ++p->at;
            if (!read_escape(p, text, &written)) {
                return false;
            }
        }
    }
    ++p->at;
    out->bytes = (const uint8_t *)text;
    out->length = written;
    return true;
}

/* Parses a literal name. */
static bool parse_name(parser *p, trl_json_item *item, const char *name, uint8_t tag) {
    size_t length = strlen(name);
    if (p->length - p->at < length || memcmp(p->text + p->at, name, length) != 0) {
        return invalid(p, "a value is not one of JSON's");
    }
    p->at += length;
    item->tag = tag;
    return true;
}

/* A new item, from the arena, linked after `last`. */
static trl_json_item *new_item(parser *p, trl_json_item **first, trl_json_item **last) {
    trl_json_item *item = trl_arena_alloc(p->arena, 1U, sizeof(trl_json_item));
    if (item == NULL) {
        trl_codec_no_memory(p->error);
        return NULL;
    }
    if (*last == NULL) {
        *first = item;
    } else {
        (*last)->next = item;
    }
    *last = item;
    return item;
}

/* Parses a member's key and its colon. */
static bool parse_key(parser *p, trl_json_item *member) {
    if (peek(p) != '"') {
        return invalid(p, "an object's member has no key");
    }
    if (!parse_string(p, &member->key)) {
        return false;
    }
    skip_space(p);
    if (peek(p) != ':') {
        return invalid(p, "an object's key is not followed by a colon");
    }
    ++p->at;
    return true;
}

/* Parses an array's elements or an object's members, after the opening
   bracket, up to `close`. */
static bool parse_container(parser *p, trl_json_item *item, char close, unsigned int depth) {
    trl_json_item *last = NULL;
    skip_space(p);
    if (peek(p) == close) {
        ++p->at;
        return true;
    }
    for (;;) {
        trl_json_item *child = new_item(p, &item->as.container.first, &last);
        if (child == NULL) {
            return false;
        }
        ++item->as.container.count;
        skip_space(p);
        if (close == '}' && !parse_key(p, child)) {
            return false;
        }
        if (!parse_value(p, child, depth)) {
            return false;
        }
        skip_space(p);
        char c = peek(p);
        ++p->at;
        if (c == close) {
            return true;
        }
        if (c != ',') {
            return invalid(p, "an array's or an object's items are not separated by commas");
        }
    }
}

static bool parse_value(parser *p, trl_json_item *item, unsigned int depth) {
    skip_space(p);
    char c = peek(p);
    if (c == '[' || c == '{') {
        if (depth >= TRL_JSON_MAX_NESTING) {
            trl_codec_fail(p->error, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                           "arrays and objects nest too deep");
            return false;
        }
        ++p->at;
        item->tag = c == '[' ? TRL_JSON_ARRAY : TRL_JSON_OBJECT;
        return parse_container(p, item, c == '[' ? ']' : '}', depth + 1U);
    }
    if (c == '"') {
        item->tag = TRL_JSON_STRING;
        return parse_string(p, &item->as.string);
    }
    if (c == '-' || is_digit(c)) {
        return parse_number(p, item);
    }
    if (c == 't') {
        return parse_name(p, item, "true", TRL_JSON_TRUE);
    }
    if (c == 'f') {
        return parse_name(p, item, "false", TRL_JSON_FALSE);
    }
    return parse_name(p, item, "null", TRL_JSON_NULL);
}

bool trl_json_from_text(trl_buffer *buffer, const uint8_t *text, size_t length, trl_arena *arena,
                        trl_codec_error *error) {
    parser p = {(const char *)text, length, 0U, arena, error};
    if (text == NULL && length > 0U) {
        return invalid(&p, "a JSON text is missing");
    }
    if (!trl_utf8_valid(text, length)) {
        return invalid(&p, "a JSON text is not UTF-8");
    }
    trl_json_item *root = trl_arena_alloc(arena, 1U, sizeof(trl_json_item));
    if (root == NULL) {
        trl_codec_no_memory(error);
        return false;
    }
    if (!parse_value(&p, root, 0U)) {
        return false;
    }
    skip_space(&p);
    if (p.at != p.length) {
        return invalid(&p, "characters follow a JSON text's value");
    }
    return trl_json_write_document(buffer, root, arena, error);
}
