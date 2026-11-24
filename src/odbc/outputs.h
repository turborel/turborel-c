/**
 * @file outputs.h
 * @brief A statement's parameters in and out: the IPD filled, and a CALL's outputs given back.
 */
#ifndef TRL_ODBC_OUTPUTS_H
#define TRL_ODBC_OUTPUTS_H

#include "odbc/odbc.h"

/**
 * @brief Fills the IPD of the statement just prepared, unless
 * SQL_ATTR_ENABLE_AUTO_IPD is off: each `?`'s SQL type, size and digits,
 * and its SQL_DESC_PARAMETER_TYPE, SQL_PARAM_INPUT, SQL_PARAM_INPUT_OUTPUT
 * or SQL_PARAM_OUTPUT by its mode. SQL_ERROR (HY001) if memory cannot be
 * had.
 */
SQLRETURN trl_odbc_ipd_filled(trl_odbc_stmt *stmt);

/**
 * @brief Gives each `?` of an OUT or INOUT parameter bound as
 * SQL_PARAM_OUTPUT or SQL_PARAM_INPUT_OUTPUT the value of its column of
 * the CALL's row, `statement` the CALL executed; the row so given, closes
 * the cursor. SQL_SUCCESS_WITH_INFO of a value cut short, SQL_ERROR of one
 * that cannot be given, with the statement's diagnostics.
 */
SQLRETURN trl_odbc_outputs_given(trl_odbc_stmt *stmt, const trl_statement *statement);

#endif
