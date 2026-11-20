/**
 * @file names.c
 * @brief SQL names read as Turborel's lexer reads them (`lexer.rs`): a
 * <regular identifier>, a letter, then letters, digits and underscores, at
 * most 128 letters and digits, in upper case, unless it is a <reserved
 * word>, a <key word>; a <delimited identifier> in double quotes, its case
 * kept, a quote in it written twice; spaces, newlines and comments between
 * [ISO/IEC 9075:1992 §5.2]. Letters past ASCII are taken as Turborel takes
 * them as far as RULES.md D8 says.
 */
#include "bulk/names.h"

#include "codec/buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The most letters and digits of a <regular identifier> [§5.2 SR8], and
   the most parts of a <table name>: catalog, schema and table [§5.4]. */
#define TRL_MAX_IDENTIFIER 128U
#define TRL_MAX_NAME_PARTS 3U
/* The code points past ASCII of the letters this lexer upper-cases. */
#define TRL_LATIN1_SMALL_FIRST 0xE0U
#define TRL_LATIN1_SMALL_LAST 0xFEU
#define TRL_DIVISION_SIGN 0xF7U
#define TRL_MULTIPLICATION_SIGN 0xD7U
#define TRL_Y_DIAERESIS 0xFFU
#define TRL_Y_DIAERESIS_CAPITAL 0x178U
#define TRL_EXTENDED_A_FIRST 0x100U
#define TRL_EXTENDED_A_LAST 0x17FU
#define TRL_GREEK_SMALL_FIRST 0x3B1U
#define TRL_GREEK_SMALL_LAST 0x3C9U
#define TRL_GREEK_FINAL_SIGMA 0x3C2U
#define TRL_GREEK_SIGMA 0x3A3U
#define TRL_CYRILLIC_SMALL_FIRST 0x430U
#define TRL_CYRILLIC_SMALL_LAST 0x44FU
#define TRL_CYRILLIC_EXTRA_FIRST 0x450U
#define TRL_CYRILLIC_EXTRA_LAST 0x45FU
#define TRL_CASE_DISTANCE 0x20U
#define TRL_CYRILLIC_EXTRA_DISTANCE 0x50U
/* Code points past ASCII taken as no letter: C1 controls and Latin-1's
   signs, general punctuation to the arrows and symbols, CJK punctuation. */
#define TRL_LATIN1_SIGNS_LAST 0xBFU
#define TRL_PUNCTUATION_FIRST 0x2000U
#define TRL_SYMBOLS_LAST 0x2BFFU
#define TRL_CJK_PUNCTUATION_FIRST 0x3000U
#define TRL_CJK_PUNCTUATION_LAST 0x303FU
/* The octets of a comment's marks, as -- or the brackets of one. */
#define TRL_COMMENT_MARK 2U

/* UTF-8's lead bytes and bits. */
#define TRL_ONE_BYTE 0x80U
#define TRL_TWO_BYTES 0x800U
#define TRL_THREE_BYTES 0x10000U
#define TRL_LEAD_TWO 0xC0U
#define TRL_LEAD_THREE 0xE0U
#define TRL_LEAD_FOUR 0xF0U
#define TRL_TRAIL 0x80U
#define TRL_SIX_BITS 0x3FU
#define TRL_BITS 6U

/* [ISO/IEC 9075:1992 §5.2] The <reserved word>s Turborel's lexer takes as
   <key word>s, in order. */
static const char *const key_words[] = {
    "ABSOLUTE",
    "ACTION",
    "ADD",
    "ALL",
    "ALLOCATE",
    "ALTER",
    "AND",
    "ANY",
    "ARE",
    "AS",
    "ASC",
    "ASSERTION",
    "AT",
    "AUTHORIZATION",
    "AVG",
    "BEGIN",
    "BETWEEN",
    "BIT",
    "BIT_LENGTH",
    "BOTH",
    "BY",
    "CASCADE",
    "CASCADED",
    "CASE",
    "CAST",
    "CATALOG",
    "CHAR",
    "CHARACTER",
    "CHARACTER_LENGTH",
    "CHAR_LENGTH",
    "CHECK",
    "CLOSE",
    "COALESCE",
    "COLLATE",
    "COLLATION",
    "COLUMN",
    "COMMIT",
    "CONNECT",
    "CONNECTION",
    "CONSTRAINT",
    "CONSTRAINTS",
    "CONTINUE",
    "CONVERT",
    "CORRESPONDING",
    "COUNT",
    "CREATE",
    "CROSS",
    "CURRENT",
    "CURRENT_DATE",
    "CURRENT_TIME",
    "CURRENT_TIMESTAMP",
    "CURRENT_USER",
    "CURSOR",
    "DATE",
    "DAY",
    "DEALLOCATE",
    "DEC",
    "DECIMAL",
    "DECLARE",
    "DEFAULT",
    "DEFERRABLE",
    "DEFERRED",
    "DELETE",
    "DESC",
    "DESCRIBE",
    "DESCRIPTOR",
    "DIAGNOSTICS",
    "DISCONNECT",
    "DISTINCT",
    "DOMAIN",
    "DOUBLE",
    "DROP",
    "ELSE",
    "END",
    "ESCAPE",
    "EXCEPT",
    "EXCEPTION",
    "EXEC",
    "EXECUTE",
    "EXISTS",
    "EXTERNAL",
    "EXTRACT",
    "FALSE",
    "FETCH",
    "FIRST",
    "FLOAT",
    "FOR",
    "FOREIGN",
    "FOUND",
    "FROM",
    "FULL",
    "GET",
    "GLOBAL",
    "GO",
    "GOTO",
    "GRANT",
    "GROUP",
    "HAVING",
    "HOUR",
    "IDENTITY",
    "IMMEDIATE",
    "IN",
    "INDICATOR",
    "INITIALLY",
    "INNER",
    "INPUT",
    "INSENSITIVE",
    "INSERT",
    "INT",
    "INTEGER",
    "INTERSECT",
    "INTERVAL",
    "INTO",
    "IS",
    "ISOLATION",
    "JOIN",
    "KEY",
    "LANGUAGE",
    "LAST",
    "LEADING",
    "LEFT",
    "LEVEL",
    "LIKE",
    "LOCAL",
    "LOWER",
    "MATCH",
    "MAX",
    "MIN",
    "MINUTE",
    "MODULE",
    "MONTH",
    "NAMES",
    "NATIONAL",
    "NATURAL",
    "NCHAR",
    "NEXT",
    "NO",
    "NOT",
    "NULL",
    "NULLIF",
    "NUMERIC",
    "OCTET_LENGTH",
    "OF",
    "ON",
    "ONLY",
    "OPEN",
    "OPTION",
    "OR",
    "ORDER",
    "OUTER",
    "OUTPUT",
    "OVERLAPS",
    "PAD",
    "PARTIAL",
    "POSITION",
    "PRECISION",
    "PREPARE",
    "PRESERVE",
    "PRIMARY",
    "PRIOR",
    "PRIVILEGES",
    "PROCEDURE",
    "PUBLIC",
    "READ",
    "REAL",
    "REFERENCES",
    "RELATIVE",
    "RESTRICT",
    "REVOKE",
    "RIGHT",
    "ROLLBACK",
    "ROWS",
    "SCHEMA",
    "SCROLL",
    "SECOND",
    "SECTION",
    "SELECT",
    "SESSION",
    "SESSION_USER",
    "SET",
    "SIZE",
    "SMALLINT",
    "SOME",
    "SPACE",
    "SQL",
    "SQLCODE",
    "SQLERROR",
    "SQLSTATE",
    "SUBSTRING",
    "SUM",
    "SYSTEM_USER",
    "TABLE",
    "TEMPORARY",
    "THEN",
    "TIME",
    "TIMESTAMP",
    "TIMEZONE_HOUR",
    "TIMEZONE_MINUTE",
    "TO",
    "TRAILING",
    "TRANSACTION",
    "TRANSLATE",
    "TRANSLATION",
    "TRIM",
    "TRUE",
    "UNION",
    "UNIQUE",
    "UNKNOWN",
    "UPDATE",
    "UPPER",
    "USAGE",
    "USER",
    "USING",
    "VALUE",
    "VALUES",
    "VARCHAR",
    "VARYING",
    "VIEW",
    "WHEN",
    "WHENEVER",
    "WHERE",
    "WITH",
    "WORK",
    "WRITE",
    "YEAR",
    "ZONE",
};

/* What a token is. */
typedef enum kind {
    TOKEN_END = 0,
    TOKEN_IDENTIFIER,
    TOKEN_KEY_WORD,
    TOKEN_PERIOD,
    TOKEN_OTHER,
} kind;

/* Decodes the code point at `at`, UTF-8 already checked; the bytes in `width`. */
static uint32_t decode(const uint8_t *at, size_t *width) {
    uint32_t c = at[0];
    size_t count = 1U;
    if (c >= TRL_ONE_BYTE) {
        for (uint32_t bit = TRL_ONE_BYTE >> 1U; (c & bit) != 0U; bit >>= 1U) {
            ++count;
        }
        c &= (TRL_ONE_BYTE >> count) - 1U;
        for (size_t i = 1U; i < count; ++i) {
            c = (c << TRL_BITS) | (at[i] & TRL_SIX_BITS);
        }
    }
    *width = count;
    return c;
}

/* Appends code point `c` as UTF-8. */
static void encode(trl_buffer *out, uint32_t c) {
    static const uint8_t leads[] = {0U, TRL_LEAD_TWO, TRL_LEAD_THREE, TRL_LEAD_FOUR};
    if (c < TRL_ONE_BYTE) {
        trl_put_u8(out, (uint8_t)c);
        return;
    }
    size_t trailing = 1U;
    trailing += c >= TRL_TWO_BYTES ? 1U : 0U;
    trailing += c >= TRL_THREE_BYTES ? 1U : 0U;
    trl_put_u8(out, (uint8_t)(leads[trailing] | (c >> (trailing * TRL_BITS))));
    for (size_t i = trailing; i-- > 0U;) {
        trl_put_u8(out, (uint8_t)(TRL_TRAIL | ((c >> (i * TRL_BITS)) & TRL_SIX_BITS)));
    }
}

/* Whether code point `c` is taken as a letter. */
static bool letter(uint32_t c) {
    if (c < TRL_ONE_BYTE) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    bool sign = c <= TRL_LATIN1_SIGNS_LAST || c == TRL_MULTIPLICATION_SIGN || c == TRL_DIVISION_SIGN;
    bool punctuation = (c >= TRL_PUNCTUATION_FIRST && c <= TRL_SYMBOLS_LAST) ||
                       (c >= TRL_CJK_PUNCTUATION_FIRST && c <= TRL_CJK_PUNCTUATION_LAST);
    return !sign && !punctuation;
}

/* Code point `c` in upper case, as far as this lexer knows it. */
static uint32_t upper(uint32_t c) {
    if (c >= 'a' && c <= 'z') {
        return c - TRL_CASE_DISTANCE;
    }
    if (c >= TRL_LATIN1_SMALL_FIRST && c <= TRL_LATIN1_SMALL_LAST && c != TRL_DIVISION_SIGN) {
        return c - TRL_CASE_DISTANCE;
    }
    if (c == TRL_Y_DIAERESIS) {
        return TRL_Y_DIAERESIS_CAPITAL;
    }
    if (c >= TRL_EXTENDED_A_FIRST && c <= TRL_EXTENDED_A_LAST && (c & 1U) == 1U) {
        return c - 1U;
    }
    if (c == TRL_GREEK_FINAL_SIGMA) {
        return TRL_GREEK_SIGMA;
    }
    if ((c >= TRL_GREEK_SMALL_FIRST && c <= TRL_GREEK_SMALL_LAST) ||
        (c >= TRL_CYRILLIC_SMALL_FIRST && c <= TRL_CYRILLIC_SMALL_LAST)) {
        return c - TRL_CASE_DISTANCE;
    }
    if (c >= TRL_CYRILLIC_EXTRA_FIRST && c <= TRL_CYRILLIC_EXTRA_LAST) {
        return c - TRL_CYRILLIC_EXTRA_DISTANCE;
    }
    return c;
}

/* Compares two key words, for bsearch. */
static int compare_words(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Whether `word`, in upper case, is a <key word>. */
static bool key_word(const char *word) {
    return bsearch((const void *)&word, (const void *)key_words, sizeof key_words / sizeof key_words[0],
                   sizeof key_words[0], compare_words) != NULL;
}

/* Passes over spaces, newlines and comments; false at text no separator
   ends, as a comment not closed. */
static bool separators(const char *text, size_t *at) {
    for (;;) {
        char c = text[*at];
        if (c == ' ' || c == '\n' || c == '\r') {
            ++*at;
        } else if (c == '-' && text[*at + 1U] == '-') {
            const char *line = strpbrk(text + *at, "\r\n");
            if (line == NULL) {
                return false;
            }
            *at = (size_t)(line - text);
        } else if (c == '/' && text[*at + 1U] == '*') {
            const char *closed = strstr(text + *at + TRL_COMMENT_MARK, "*/");
            if (closed == NULL) {
                return false;
            }
            *at = (size_t)(closed - text) + TRL_COMMENT_MARK;
        } else {
            return true;
        }
    }
}

/* Reads a <delimited identifier>'s body after its quote into `name`. */
static kind delimited_token(const char *text, size_t *at, trl_buffer *name) {
    for (;;) {
        char c = text[*at];
        if (c == '\0' || c == '\n' || c == '\r') {
            return TOKEN_OTHER;
        }
        ++*at;
        if (c == '"') {
            if (text[*at] != '"') {
                return name->length == 0U ? TOKEN_OTHER : TOKEN_IDENTIFIER;
            }
            ++*at;
        }
        trl_put_u8(name, (uint8_t)c);
    }
}

/* Reads a <regular identifier> or a <key word> into `name`, in upper case. */
static kind regular_token(const char *text, size_t *at, trl_buffer *name) {
    size_t letters = 0U;
    for (;;) {
        size_t width = 1U;
        uint32_t c = decode((const uint8_t *)text + *at, &width);
        bool digit = c >= '0' && c <= '9';
        if (!letter(c) && !digit && c != '_') {
            break;
        }
        letters += c == '_' ? 0U : 1U;
        encode(name, upper(c));
        *at += width;
    }
    trl_put_u8(name, 0U);
    --name->length;
    if (key_word((const char *)name->data)) {
        return TOKEN_KEY_WORD;
    }
    return letters <= TRL_MAX_IDENTIFIER ? TOKEN_IDENTIFIER : TOKEN_OTHER;
}

/* Reads the next token from `at`, an identifier's or key word's name into
   `name`, emptied first. */
static kind next_token(const char *text, size_t *at, trl_buffer *name) {
    name->length = 0U;
    if (!separators(text, at)) {
        return TOKEN_OTHER;
    }
    size_t width = 1U;
    uint32_t c = text[*at] == '\0' ? 0U : decode((const uint8_t *)text + *at, &width);
    if (c == 0U) {
        return TOKEN_END;
    }
    if (c == '"') {
        ++*at;
        return delimited_token(text, at, name);
    }
    if (c == '.') {
        ++*at;
        return TOKEN_PERIOD;
    }
    return letter(c) ? regular_token(text, at, name) : TOKEN_OTHER;
}

void trl_put_delimited(trl_buffer *out, const char *name, size_t length) {
    trl_put_u8(out, (uint8_t)'"');
    for (size_t i = 0U; i < length; ++i) {
        trl_put_u8(out, (uint8_t)name[i]);
        if (name[i] == '"') {
            trl_put_u8(out, (uint8_t)'"');
        }
    }
    trl_put_u8(out, (uint8_t)'"');
}

bool trl_table_sql(const char *text, trl_buffer *out) {
    trl_buffer name;
    trl_buffer_init(&name);
    size_t at = 0U;
    bool named = true;
    for (unsigned int parts = 1U; named; ++parts) {
        named = next_token(text, &at, &name) == TOKEN_IDENTIFIER;
        if (named) {
            trl_put_delimited(out, (const char *)name.data, name.length);
            kind after = next_token(text, &at, &name);
            if (after == TOKEN_END) {
                break;
            }
            named = after == TOKEN_PERIOD && parts < TRL_MAX_NAME_PARTS;
            trl_put_u8(out, (uint8_t)'.');
        }
    }
    trl_buffer_free(&name);
    return named;
}

void trl_column_of_heading(const char *heading, trl_buffer *out) {
    trl_buffer name;
    trl_buffer_init(&name);
    size_t at = 0U;
    kind first = next_token(heading, &at, &name);
    size_t length = name.length;
    trl_buffer rest;
    trl_buffer_init(&rest);
    bool alone = (first == TOKEN_IDENTIFIER || first == TOKEN_KEY_WORD) && next_token(heading, &at, &rest) == TOKEN_END;
    if (alone) {
        trl_put_raw(out, name.data, length);
    } else {
        trl_put_raw(out, (const uint8_t *)heading, strlen(heading));
    }
    trl_buffer_free(&rest);
    trl_buffer_free(&name);
}
