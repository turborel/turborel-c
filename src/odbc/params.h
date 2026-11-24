/**
 * @file params.h
 * @brief The parameters an application bound, as a statement's values.
 */
#ifndef TRL_ODBC_PARAMS_H
#define TRL_ODBC_PARAMS_H

#include "odbc/odbc.h"

#include <stddef.h>

/**
 * @brief The values `statement` is executed with, as the application bound
 * them, into `out`, the caller's to free: each `?` bound, numbered in the
 * order written, and the value of each IN or INOUT one, an OUT one's
 * left out [PROTOCOL Parameter modes]; SQL_ERROR with the statement's
 * diagnostics of a `?` not bound (07002), of a C type the driver does not
 * take (HY003), of data at execution (HYC00).
 */
SQLRETURN trl_odbc_params(trl_odbc_stmt *stmt, const trl_statement *statement, trl_params **out);

#endif
