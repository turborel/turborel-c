/**
 * @file names.h
 * @brief SQL names as an import writes them: a table's name, a heading's
 * column, each a delimited identifier in the SQL it makes.
 */
#ifndef TRL_BULK_NAMES_H
#define TRL_BULK_NAMES_H

#include "codec/buffer.h"

#include <stdbool.h>

/**
 * @brief Appends `text`, a table name, as SQL text of delimited
 * identifiers, so nothing else is taken for one [ISO/IEC 9075:1992 §5.4];
 * false if it is no table name.
 */
bool trl_table_sql(const char *text, trl_buffer *out);

/**
 * @brief Appends the column `heading` names: the identifier it is, if it
 * is one, a regular one in upper case, a key word as it would be but for
 * being reserved; else its text as written.
 */
void trl_column_of_heading(const char *heading, trl_buffer *out);

/** @brief Appends `name` as a delimited identifier, a quote in it written twice. */
void trl_put_delimited(trl_buffer *out, const char *name, size_t length);

#endif
