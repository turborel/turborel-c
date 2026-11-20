/**
 * @file params.h
 * @brief The parameters an application bound, as a statement's values.
 */
#ifndef TRL_ODBC_PARAMS_H
#define TRL_ODBC_PARAMS_H

#include "odbc/odbc.h"

#include <stddef.h>

/**
 * @brief The values of the statement's first `count` parameters, as the
 * application bound them, into `out`, the caller's to free; SQL_ERROR
 * with the statement's diagnostics of one not bound (07002), of a C type
 * the driver does not take (HY003), of data at execution (HYC00).
 */
SQLRETURN trl_odbc_params(trl_odbc_stmt *stmt, size_t count, trl_params **out);

#endif
