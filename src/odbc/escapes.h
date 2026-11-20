/**
 * @file escapes.h
 * @brief ODBC's escape sequences turned into Turborel's SQL.
 */
#ifndef TRL_ODBC_ESCAPES_H
#define TRL_ODBC_ESCAPES_H

#include <stdbool.h>

/**
 * @brief `sql` with each escape sequence written as Turborel's SQL, a new
 * string the caller's to free; null if memory cannot be had, or, with
 * `*malformed` set, of an escape not closed or of no kind ODBC has.
 */
char *trl_odbc_escaped(const char *sql, bool *malformed);

#endif
