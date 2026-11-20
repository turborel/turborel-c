/**
 * @file names.h
 * @brief SQL names as an import writes them: a table's name, a heading's
 * column, each a delimited identifier in the SQL it makes.
 */
#ifndef TRL_BULK_NAMES_H
#define TRL_BULK_NAMES_H

#include "codec/buffer.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief What a token is, as far as names are read. */
typedef enum trl_token {
    /** @brief The end of the text. */
    TRL_TOKEN_END = 0,
    /** @brief An <identifier>: a regular one in upper case, or a delimited one's body. */
    TRL_TOKEN_IDENTIFIER,
    /** @brief A <key word>, in upper case. */
    TRL_TOKEN_KEY_WORD,
    /** @brief A period. */
    TRL_TOKEN_PERIOD,
    /** @brief Anything else, the text at it not passed over. */
    TRL_TOKEN_OTHER,
} trl_token;

/**
 * @brief Passes `at` over spaces, newlines and comments [ISO/IEC 9075:1992
 * §5.2], as Turborel's lexer does: a newline LF or CR LF, a simple
 * comment ended by one, a bracketed comment holding others nested in it;
 * false at text no separator ends, as a comment not closed.
 */
bool trl_separators(const char *text, size_t *at);

/**
 * @brief Reads the token at `at`, after separators, and passes over it: an
 * identifier's or key word's name into `name`, emptied first.
 */
trl_token trl_next_token(const char *text, size_t *at, trl_buffer *name);

/**
 * @brief Whether `text` is one <identifier> and nothing else but
 * separators, as an <authorization identifier> or a <connection name> is
 * [ISO/IEC 9075:1992 §5.4]: its name into `out`, null-ended.
 */
bool trl_identifier_of(const char *text, trl_buffer *out);

/**
 * @brief Appends `text`, a table name, as SQL text of delimited
 * identifiers, so nothing else is taken for one [ISO/IEC 9075:1992 §5.4];
 * false if it is no table name; memory not had marks `out` so.
 */
bool trl_table_sql(const char *text, trl_buffer *out);

/**
 * @brief Appends the column `heading` names: the identifier it is, if it
 * is one, a regular one in upper case, a key word as it would be but for
 * being reserved; else its text as written; memory not had marks `out`
 * so.
 */
void trl_column_of_heading(const char *heading, trl_buffer *out);

/** @brief Appends `name` as a delimited identifier, a quote in it written twice. */
void trl_put_delimited(trl_buffer *out, const char *name, size_t length);

#endif
