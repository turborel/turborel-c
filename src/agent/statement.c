/**
 * @file statement.c
 * @brief CONNECT, SET CONNECTION and DISCONNECT read from SQL text, as
 * Turborel's parser reads them (`parser/connections.rs`)
 * [CLIENT The SQL-agent]: a statement is one of them if it begins CONNECT,
 * DISCONNECT or SET CONNECTION, and is then read whole, a semicolon
 * allowed at its end [ISO/IEC 9075:1992 §15.1, §15.2, §15.3]:
 *
 *     CONNECT TO DEFAULT
 *     CONNECT TO <server> [ AS <name> ] [ USER <user> ]
 *     SET CONNECTION { DEFAULT | <name> }
 *     DISCONNECT { DEFAULT | ALL | CURRENT | <name> }
 *
 * each name a <simple value specification>: a literal, or a parameter;
 * an <embedded variable name> refused as embedded SQL. A <character
 * string literal> is its quoted parts, each separator between them
 * holding a newline, a quote in one written twice, and may begin with
 * the introducer of SQL_TEXT, the one character set taken [ISO/IEC
 * 9075:1992 §5.3 SR1-SR4].
 */
#include "agent/statement.h"

#include "bulk/names.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "failure.h"
#include "format.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The SQLCODEs of text that is no statement: of its characters, its
   syntax, or a rule of its syntax; their SQLSTATE, SQL:2023's one class
   of a syntax error or an access rule's violation. */
#define TRL_SQLCODE_LEXICAL (-101)
#define TRL_SQLCODE_SYNTAX (-102)
#define TRL_SQLSTATE_SYNTAX "42000"
#define TRL_SQLSTATE_UNSUPPORTED "0A000"
/* A quote, and the bytes that begin a UTF-8 character but continue none. */
#define TRL_QUOTE '\''
#define TRL_CONTINUATION_MASK 0xC0U
#define TRL_CONTINUATION 0x80U
/* The one character set an introducer may name. */
#define TRL_SQL_TEXT "SQL_TEXT"
/* Room for a character set's name in a message. */
#define TRL_SET_NAME_BYTES 130U

/* Text read: where, the word last read, and where a failure goes. */
typedef struct scanner {
    const char *sql;
    size_t at;
    trl_buffer word;
    trl_failure *failure;
} scanner;

void trl_agent_statement_init(trl_agent_statement *statement) {
    memset(statement, 0, sizeof *statement);
    trl_buffer_init(&statement->server.text);
    trl_buffer_init(&statement->name.text);
    trl_buffer_init(&statement->user.text);
}

void trl_agent_statement_free(trl_agent_statement *statement) {
    trl_buffer_free(&statement->server.text);
    trl_buffer_free(&statement->name.text);
    trl_buffer_free(&statement->user.text);
}

/* The characters before the token at hand, its separators passed if
   `separated`: where a failure is, as Turborel places it. */
static uint64_t here(const scanner *s, bool separated) {
    size_t at = s->at;
    if (separated) {
        (void)trl_separators(s->sql, &at);
    }
    uint64_t characters = 0U;
    for (size_t i = 0U; i < at; ++i) {
        characters += ((uint8_t)s->sql[i] & TRL_CONTINUATION_MASK) != TRL_CONTINUATION ? 1U : 0U;
    }
    return characters;
}

/* Fails the statement at `position`. */
static bool fail_at(scanner *s, uint64_t position, int32_t sqlcode, const char *sqlstate, const char *message) {
    trl_failure_set(s->failure, sqlcode, sqlstate, message);
    s->failure->placed = true;
    s->failure->position = position;
    return false;
}

/* Fails the statement at the token at hand. */
static bool fail(scanner *s, int32_t sqlcode, const char *sqlstate, const char *message) {
    return fail_at(s, here(s, true), sqlcode, sqlstate, message);
}

/* Fails the statement as not of its Format. */
static bool syntax(scanner *s, const char *expected) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "expected %s", expected);
    return fail(s, TRL_SQLCODE_SYNTAX, TRL_SQLSTATE_SYNTAX, message);
}

/* Whether the next token is the key word `word`, passed over if it is. */
static bool key_word(scanner *s, const char *word) {
    size_t at = s->at;
    if (trl_next_token(s->sql, &at, &s->word) == TRL_TOKEN_KEY_WORD && s->word.state == TRL_BUFFER_OK &&
        strcmp((const char *)s->word.data, word) == 0) {
        s->at = at;
        return true;
    }
    return false;
}

/* One quoted part of a literal, from its opening quote, its characters
   added to `out`. */
static bool quoted_part(scanner *s, trl_buffer *out) {
    size_t start = s->at;
    ++s->at;
    for (;;) {
        char c = s->sql[s->at];
        if (c == '\0' || c == '\n' || c == '\r') {
            s->at = c == '\0' ? start : s->at;
            const char *why = c == '\0' ? "unterminated <character string literal>"
                                        : "a <newline> cannot appear inside a <character string literal>";
            return fail_at(s, here(s, false), TRL_SQLCODE_LEXICAL, TRL_SQLSTATE_SYNTAX, why);
        }
        ++s->at;
        if (c == TRL_QUOTE) {
            if (s->sql[s->at] != TRL_QUOTE) {
                return true;
            }
            ++s->at;
        }
        trl_put_u8(out, (uint8_t)c);
    }
}

/* A literal's quoted parts, one after another while each separator
   between them holds a newline; its value null-ended into `out`. */
static bool quoted_parts(scanner *s, trl_buffer *out) {
    for (;;) {
        if (!quoted_part(s, out)) {
            return false;
        }
        size_t after = s->at;
        size_t next = after;
        bool separated = trl_separators(s->sql, &next);
        bool newline = memchr(s->sql + after, '\n', next - after) != NULL;
        if (!separated || !newline || s->sql[next] != TRL_QUOTE) {
            trl_put_u8(out, 0U);
            return true;
        }
        s->at = next;
    }
}

/* Whether `c` may be in a word. */
static bool word_character(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* An introducer, at its underscore, and the literal after it: of SQL_TEXT
   taken, of another set refused. */
static bool introduced(scanner *s, trl_agent_text *value) {
    size_t start = ++s->at;
    while (word_character(s->sql[s->at])) {
        ++s->at;
    }
    char name[TRL_SET_NAME_BYTES];
    size_t length = s->at - start < sizeof name - 1U ? s->at - start : sizeof name - 1U;
    for (size_t i = 0U; i < length; ++i) {
        char c = s->sql[start + i];
        name[i] = c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
    }
    name[length] = '\0';
    (void)trl_separators(s->sql, &s->at);
    if (s->sql[s->at] != TRL_QUOTE) {
        return syntax(s, "a <character string literal> after its <introducer>");
    }
    if (strcmp(name, TRL_SQL_TEXT) != 0) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "the character set %s is valid SQL but is not implemented", name);
        return fail(s, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED, message);
    }
    value->kind = TRL_VALUE_TEXT;
    return quoted_parts(s, &value->text);
}

/* Whether `c` begins a number, perhaps after a sign. */
static bool digit_at(const char *at) {
    return (at[0] >= '0' && at[0] <= '9') || (at[0] == '.' && at[1] >= '0' && at[1] <= '9');
}

/* Passes over a numeric literal: its sign, digits and period, and an
   exponent. */
static void numeric(scanner *s) {
    const char *sql = s->sql;
    s->at += sql[s->at] == '+' || sql[s->at] == '-' ? 1U : 0U;
    while ((sql[s->at] >= '0' && sql[s->at] <= '9') || sql[s->at] == '.') {
        ++s->at;
    }
    if (sql[s->at] == 'E' || sql[s->at] == 'e') {
        ++s->at;
        s->at += sql[s->at] == '+' || sql[s->at] == '-' ? 1U : 0U;
        while (sql[s->at] >= '0' && sql[s->at] <= '9') {
            ++s->at;
        }
    }
}

/* A literal of another type, its word read: a national character, binary
   or bit string literal, its letter against its quote; a datetime literal;
   a truth value. Passed over, whether it is one; `failed` set if it is
   one not ended. */
static bool other_literal(scanner *s, trl_token token, bool *failed) {
    if (s->word.state != TRL_BUFFER_OK || s->word.data == NULL) {
        return false;
    }
    const char *word = (const char *)s->word.data;
    bool lettered = token == TRL_TOKEN_IDENTIFIER && s->sql[s->at] == TRL_QUOTE &&
                    (strcmp(word, "N") == 0 || strcmp(word, "X") == 0 || strcmp(word, "B") == 0);
    bool datetime = token == TRL_TOKEN_KEY_WORD &&
                    (strcmp(word, "DATE") == 0 || strcmp(word, "TIME") == 0 || strcmp(word, "TIMESTAMP") == 0);
    if (datetime) {
        (void)trl_separators(s->sql, &s->at);
        datetime = s->sql[s->at] == TRL_QUOTE;
    }
    if (lettered || datetime) {
        trl_buffer ignored;
        trl_buffer_init(&ignored);
        *failed = !quoted_parts(s, &ignored);
        trl_buffer_free(&ignored);
        return true;
    }
    return token == TRL_TOKEN_KEY_WORD &&
           (strcmp(word, "TRUE") == 0 || strcmp(word, "FALSE") == 0 || strcmp(word, "UNKNOWN") == 0);
}

/* A <simple value specification> into `value`. */
static bool value_of(scanner *s, trl_agent_text *value) {
    (void)trl_separators(s->sql, &s->at);
    const char *at = s->sql + s->at;
    if (*at == TRL_QUOTE) {
        value->kind = TRL_VALUE_TEXT;
        return quoted_parts(s, &value->text);
    }
    if (*at == '_') {
        return introduced(s, value);
    }
    if (*at == ':') {
        return fail(s, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                    "<variable specification> (embedded SQL) is valid SQL but is not implemented");
    }
    value->kind = TRL_VALUE_OTHER;
    if (*at == '?') {
        ++s->at;
        return true;
    }
    if (digit_at(at) || ((*at == '+' || *at == '-') && digit_at(at + 1))) {
        numeric(s);
        return true;
    }
    size_t start = s->at;
    bool failed = false;
    if (other_literal(s, trl_next_token(s->sql, &s->at, &s->word), &failed)) {
        return !failed;
    }
    s->at = start;
    return syntax(s, "a <simple value specification>");
}

/* The statement's end: a semicolon, perhaps, then nothing but separators. */
static bool ended(scanner *s) {
    size_t at = s->at;
    (void)trl_separators(s->sql, &at);
    if (s->sql[at] == ';') {
        s->at = at + 1U;
    }
    at = s->at;
    if (trl_next_token(s->sql, &at, &s->word) != TRL_TOKEN_END) {
        return syntax(s, "end of statement");
    }
    return true;
}

/* CONNECT TO DEFAULT, or CONNECT TO a server [AS a name] [USER a user],
   after CONNECT. */
static bool connect_statement(scanner *s, trl_agent_statement *statement) {
    if (!key_word(s, "TO")) {
        return syntax(s, "TO");
    }
    if (key_word(s, "DEFAULT")) {
        statement->verb = TRL_AGENT_CONNECT_DEFAULT;
        return ended(s);
    }
    statement->verb = TRL_AGENT_CONNECT;
    if (!value_of(s, &statement->server)) {
        return false;
    }
    if (key_word(s, "AS") && !value_of(s, &statement->name)) {
        return false;
    }
    if (key_word(s, "USER") && !value_of(s, &statement->user)) {
        return false;
    }
    return ended(s);
}

/* SET CONNECTION DEFAULT, or a name, after SET CONNECTION. */
static bool set_connection(scanner *s, trl_agent_statement *statement) {
    statement->verb = TRL_AGENT_SET;
    if (key_word(s, "DEFAULT")) {
        statement->object = TRL_AGENT_DEFAULT;
        return ended(s);
    }
    statement->object = TRL_AGENT_NAMED;
    return value_of(s, &statement->name) && ended(s);
}

/* DISCONNECT DEFAULT, ALL, CURRENT or a name, after DISCONNECT. */
static bool disconnect_statement(scanner *s, trl_agent_statement *statement) {
    statement->verb = TRL_AGENT_DISCONNECT;
    if (key_word(s, "DEFAULT")) {
        statement->object = TRL_AGENT_DEFAULT;
    } else if (key_word(s, "ALL")) {
        statement->object = TRL_AGENT_ALL;
    } else if (key_word(s, "CURRENT")) {
        statement->object = TRL_AGENT_CURRENT;
    } else {
        statement->object = TRL_AGENT_NAMED;
        if (!value_of(s, &statement->name)) {
            return false;
        }
    }
    return ended(s);
}

bool trl_agent_statement_read(const char *sql, trl_agent_statement *statement, trl_failure *failure) {
    scanner s = {sql, 0U, {0}, failure};
    trl_buffer_init(&s.word);
    statement->verb = TRL_AGENT_OTHER;
    bool read = true;
    if (key_word(&s, "CONNECT")) {
        read = connect_statement(&s, statement);
    } else if (key_word(&s, "DISCONNECT")) {
        read = disconnect_statement(&s, statement);
    } else if (key_word(&s, "SET") && key_word(&s, "CONNECTION")) {
        read = set_connection(&s, statement);
    }
    if (s.word.state != TRL_BUFFER_OK) {
        /* What failed to be read is memory not had, not the text. */
        trl_failure_no_memory(failure);
        read = false;
    }
    trl_buffer_free(&s.word);
    return read;
}
