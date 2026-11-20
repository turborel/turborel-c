/**
 * @file escapes.c
 * @brief ODBC's escape sequences turned into Turborel's SQL (RULES.md
 * D2), as ODBC's reference's Appendix C writes them and GUI tools send
 * them: `{d '...'}`, `{t '...'}` and `{ts '...'}` datetime literals,
 * `{oj ...}` outer joins, `{call ...}` procedure calls, `{escape '...'}`
 * a LIKE's escape character, `{interval ...}` interval literals, and
 * `{fn ...}` scalar functions, those named differently written as
 * Turborel's, others as they are. Escapes may be nested; text in quotes
 * and comments is copied as it is.
 */
#include "odbc/escapes.h"

#include "memory.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The most escapes nested, and the most arguments a function takes here. */
#define TRL_ESCAPES_DEEPEST 16U
#define TRL_ARGUMENTS_MOST 2U
/* The bytes of a comment's mark. */
#define TRL_MARK_BYTES 2U
/* What text being made first holds room for, and how its room grows. */
#define TRL_TEXT_FIRST 64U
#define TRL_TEXT_GROWTH 2U

/* Text being made. */
typedef struct text {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} text;

static void put(text *t, const char *bytes, size_t length) {
    if (t->failed) {
        return;
    }
    if (t->length + length + 1U > t->capacity) {
        size_t capacity = t->capacity == 0U ? TRL_TEXT_FIRST : t->capacity;
        while (capacity < t->length + length + 1U) {
            capacity *= TRL_TEXT_GROWTH;
        }
        char *grown = trl_realloc(t->data, capacity);
        if (grown == NULL) {
            t->failed = true;
            return;
        }
        t->data = grown;
        t->capacity = capacity;
    }
    memcpy(t->data + t->length, bytes, length);
    t->length += length;
    t->data[t->length] = '\0';
}

static void put_text(text *t, const char *s) {
    put(t, s, strlen(s));
}

/* Where a quoted literal or identifier at `at` ends: past its closing
   quote, a quote in it written twice. */
static size_t quote_end(const char *text_in, size_t at, size_t length) {
    char quote = text_in[at++];
    while (at < length && !(text_in[at] == quote && (at + 1U >= length || text_in[at + 1U] != quote))) {
        at += text_in[at] == quote ? TRL_MARK_BYTES : 1U;
    }
    return at < length ? at + 1U : length;
}

/* Where a comment at `at` ends; `at` if there is none. */
static size_t comment_end(const char *sql, size_t at) {
    if (sql[at] == '-' && sql[at + 1U] == '-') {
        const char *line = strchr(sql + at, '\n');
        return line != NULL ? (size_t)(line - sql) : strlen(sql);
    }
    if (sql[at] == '/' && sql[at + 1U] == '*') {
        const char *closed = strstr(sql + at + TRL_MARK_BYTES, "*/");
        return closed != NULL ? (size_t)(closed - sql) + TRL_MARK_BYTES : strlen(sql);
    }
    return at;
}

/* Passes over a quoted literal or identifier, or a comment, at `at`,
   copying it; whether there was one. */
static bool quoted(const char *sql, size_t *at, text *out) {
    size_t start = *at;
    if (sql[start] == '\'' || sql[start] == '"') {
        *at = quote_end(sql, start, strlen(sql));
    } else {
        *at = comment_end(sql, start);
    }
    put(out, sql + start, *at - start);
    return *at != start;
}

/* Whether `word`, any case, begins `s`, a word ending it. */
static bool begins(const char *s, const char *word) {
    size_t length = strlen(word);
    for (size_t i = 0U; i < length; ++i) {
        if (toupper((unsigned char)s[i]) != word[i]) {
            return false;
        }
    }
    return !isalnum((unsigned char)s[length]) && s[length] != '_';
}

static bool translate(const char *sql, size_t *at, text *out, unsigned int depth);

/* A function's arguments at the top level of `args`, its parentheses' insides: at most two. */
static size_t split(const char *args, size_t length, size_t *starts, size_t *ends) {
    size_t count = 0U;
    size_t depth = 0U;
    starts[0] = 0U;
    for (size_t i = 0U; i < length; ++i) {
        char c = args[i];
        if (c == '\'' || c == '"') {
            i = quote_end(args, i, length) - 1U;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            depth -= depth > 0U ? 1U : 0U;
        } else if (c == ',' && depth == 0U) {
            if (count + 1U >= TRL_ARGUMENTS_MOST) {
                return TRL_ARGUMENTS_MOST + 1U;
            }
            ends[count++] = i;
            starts[count] = i + 1U;
        }
    }
    ends[count++] = length;
    return count;
}

/* A function renamed: ODBC's name and Turborel's. */
typedef struct renamed {
    const char *odbc;
    const char *turborel;
} renamed;

static const renamed renames[] = {
    {"UCASE", "UPPER"},
    {"LCASE", "LOWER"},
    {"LENGTH", "CHAR_LENGTH"},
    {"IFNULL", "COALESCE"},
};

/* A function written without its parentheses: ODBC's, and Turborel's word for it. */
static const renamed bare[] = {
    {"NOW", "LOCALTIMESTAMP"},
    {"CURDATE", "CURRENT_DATE"},
    {"CURTIME", "LOCALTIME"},
    {"USER", "CURRENT_USER"},
};

/* ODBC's SQL type names CONVERT takes, and Turborel's types they are. */
static const renamed converted[] = {
    {"SQL_BIGINT", "BIGINT"},
    {"SQL_BINARY", "BINARY"},
    {"SQL_BIT", "BOOLEAN"},
    {"SQL_CHAR", "CHARACTER"},
    {"SQL_DECIMAL", "DECIMAL"},
    {"SQL_DOUBLE", "DOUBLE PRECISION"},
    {"SQL_FLOAT", "FLOAT"},
    {"SQL_GUID", "UUID"},
    {"SQL_INTEGER", "INTEGER"},
    {"SQL_LONGVARBINARY", "BINARY LARGE OBJECT"},
    {"SQL_LONGVARCHAR", "CHARACTER LARGE OBJECT"},
    {"SQL_NUMERIC", "NUMERIC"},
    {"SQL_REAL", "REAL"},
    {"SQL_SMALLINT", "SMALLINT"},
    {"SQL_TINYINT", "SMALLINT"},
    {"SQL_TYPE_DATE", "DATE"},
    {"SQL_DATE", "DATE"},
    {"SQL_TYPE_TIME", "TIME"},
    {"SQL_TIME", "TIME"},
    {"SQL_TYPE_TIMESTAMP", "TIMESTAMP"},
    {"SQL_TIMESTAMP", "TIMESTAMP"},
    {"SQL_VARBINARY", "BINARY VARYING"},
    {"SQL_VARCHAR", "CHARACTER VARYING"},
    {"SQL_WCHAR", "CHARACTER"},
    {"SQL_WLONGVARCHAR", "CHARACTER LARGE OBJECT"},
    {"SQL_WVARCHAR", "CHARACTER VARYING"},
};

/* Writes CONVERT(value, type) as CAST(value AS type); the type as written if ODBC names none of it. */
static void convert(const char *value, size_t value_length, const char *type, size_t type_length, text *out) {
    while (type_length > 0U && isspace((unsigned char)*type)) {
        ++type;
        --type_length;
    }
    while (type_length > 0U && isspace((unsigned char)type[type_length - 1U])) {
        --type_length;
    }
    put_text(out, "CAST(");
    put(out, value, value_length);
    put_text(out, " AS ");
    for (size_t i = 0U; i < sizeof converted / sizeof converted[0]; ++i) {
        if (strlen(converted[i].odbc) == type_length && begins(type, converted[i].odbc)) {
            put_text(out, converted[i].turborel);
            put_text(out, ")");
            return;
        }
    }
    put(out, type, type_length);
    put_text(out, ")");
}

/* A function's call: its name, and its arguments within its parentheses. */
typedef struct call {
    const char *name;
    const char *open;
    const char *args;
    size_t args_length;
    size_t starts[TRL_ARGUMENTS_MOST + 1U];
    size_t ends[TRL_ARGUMENTS_MOST + 1U];
    size_t count;
} call;

/* Writes the functions written otherwise than by a new name: CONCAT, LTRIM,
   RTRIM and CONVERT; whether it was one of them. */
static bool rewritten(const call *c, text *out) {
    if (begins(c->name, "CONCAT") && c->count == TRL_ARGUMENTS_MOST) {
        put_text(out, "(");
        put(out, c->args + c->starts[0], c->ends[0] - c->starts[0]);
        put_text(out, " || ");
        put(out, c->args + c->starts[1], c->ends[1] - c->starts[1]);
        put_text(out, ")");
        return true;
    }
    bool left = begins(c->name, "LTRIM");
    if ((left || begins(c->name, "RTRIM")) && c->count == 1U) {
        put_text(out, left ? "TRIM(LEADING FROM " : "TRIM(TRAILING FROM ");
        put(out, c->args, c->args_length);
        put_text(out, ")");
        return true;
    }
    if (begins(c->name, "CONVERT") && c->count == TRL_ARGUMENTS_MOST) {
        convert(c->args + c->starts[0], c->ends[0] - c->starts[0], c->args + c->starts[1], c->ends[1] - c->starts[1],
                out);
        return true;
    }
    return false;
}

/* Writes a function renamed, or written bare; whether it was one. */
static bool renamed_put(const call *c, text *out) {
    bool empty = strspn(c->args, " ") == c->args_length;
    for (size_t i = 0U; empty && i < sizeof bare / sizeof bare[0]; ++i) {
        if (begins(c->name, bare[i].odbc)) {
            put_text(out, bare[i].turborel);
            return true;
        }
    }
    for (size_t i = 0U; i < sizeof renames / sizeof renames[0]; ++i) {
        if (begins(c->name, renames[i].odbc)) {
            put_text(out, renames[i].turborel);
            put(out, c->open, strlen(c->open));
            return true;
        }
    }
    return false;
}

/* Writes a scalar function, `name(args)` of `text_in`, as Turborel's SQL. */
static void function(const char *text_in, text *out) {
    call c;
    c.name = text_in;
    c.open = strchr(text_in, '(');
    const char *close = c.open != NULL ? strrchr(c.open, ')') : NULL;
    if (close == NULL) {
        put_text(out, text_in);
        return;
    }
    c.args = c.open + 1;
    c.args_length = (size_t)(close - c.args);
    c.count = split(c.args, c.args_length, c.starts, c.ends);
    if (!renamed_put(&c, out) && !rewritten(&c, out)) {
        put_text(out, text_in);
    }
}

/* What an escape is, by its keyword. */
typedef enum kind {
    KIND_LITERAL = 0,
    KIND_CALL,
    KIND_FUNCTION,
} kind;

/* The keyword of an escape, what it is, and what it is written as. */
typedef struct keyword {
    const char *word;
    kind kind;
    const char *written;
} keyword;

static const keyword keywords[] = {
    {"TS", KIND_LITERAL, "TIMESTAMP "}, {"D", KIND_LITERAL, "DATE "},        {"T", KIND_LITERAL, "TIME "},
    {"OJ", KIND_LITERAL, ""},           {"ESCAPE", KIND_LITERAL, "ESCAPE "}, {"INTERVAL", KIND_LITERAL, "INTERVAL "},
    {"CALL", KIND_CALL, "CALL "},       {"FN", KIND_FUNCTION, ""},
};

/* Passes over spaces at `at`. */
static void spaces_skip(const char *sql, size_t *at) {
    while (isspace((unsigned char)sql[*at])) {
        ++*at;
    }
}

/* An escape, from after its brace to its closing brace. */
static bool escape(const char *sql, size_t *at, text *out, unsigned int depth) {
    spaces_skip(sql, at);
    const keyword *kw = NULL;
    for (size_t i = 0U; i < sizeof keywords / sizeof keywords[0] && kw == NULL; ++i) {
        kw = begins(sql + *at, keywords[i].word) ? &keywords[i] : NULL;
    }
    if (kw == NULL) {
        return false;
    }
    *at += strlen(kw->word);
    spaces_skip(sql, at);
    text inside = {NULL, 0U, 0U, false};
    bool closed = translate(sql, at, &inside, depth + 1U) && !inside.failed;
    const char *body = inside.data != NULL ? inside.data : "";
    if (closed && kw->kind == KIND_FUNCTION) {
        function(body, out);
    } else if (closed) {
        put_text(out, kw->written);
        put_text(out, body);
    }
    free(inside.data);
    return closed;
}

/* Copies `sql` from `at`, each escape written as SQL, to its end or, within
   an escape, to the brace closing it; whether it ended as it should. */
static bool translate(const char *sql, size_t *at, text *out, unsigned int depth) {
    if (depth > TRL_ESCAPES_DEEPEST) {
        return false;
    }
    while (sql[*at] != '\0') {
        if (quoted(sql, at, out)) {
            continue;
        }
        char c = sql[*at];
        if (c == '}') {
            ++*at;
            return depth > 0U;
        }
        ++*at;
        if (c == '{') {
            if (!escape(sql, at, out, depth)) {
                return false;
            }
            continue;
        }
        put(out, &c, 1U);
    }
    return depth == 0U;
}

char *trl_odbc_escaped(const char *sql, bool *malformed) {
    text out = {NULL, 0U, 0U, false};
    put(&out, "", 0U);
    size_t at = 0U;
    *malformed = !translate(sql, &at, &out, 0U);
    if (out.failed || *malformed) {
        free(out.data);
        return NULL;
    }
    return out.data;
}
