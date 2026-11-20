/**
 * @file convert.h
 * @brief A value of a result given an application as the C type it asks
 * for, and a value it gives as a parameter's.
 */
#ifndef TRL_ODBC_CONVERT_H
#define TRL_ODBC_CONVERT_H

#include "odbc/odbc.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief Where a value goes: its C type, buffer and capacity, and where its length and null go. */
typedef struct trl_odbc_target {
    /** @brief The C type, never SQL_C_DEFAULT. */
    SQLSMALLINT c_type;
    /** @brief The buffer; may be null to ask only the length. */
    SQLPOINTER buffer;
    /** @brief Its capacity in bytes, of a character or binary type. */
    SQLLEN capacity;
    /** @brief Where the length goes; may be null. */
    SQLLEN *length;
    /** @brief Where SQL_NULL_DATA goes of the null value; may be null. */
    SQLLEN *indicator;
    /** @brief The scale SQL_C_NUMERIC is given at. */
    SQLSMALLINT scale;
    /** @brief The precision SQL_C_NUMERIC is given at. */
    SQLSMALLINT precision;
} trl_odbc_target;

/** @brief The C type SQL_C_DEFAULT is of a column of the concise SQL type `sql_type`. */
SQLSMALLINT trl_odbc_default_c_type(SQLSMALLINT sql_type);

/**
 * @brief Gives `piece`'s value to `target`, from as much of it as was
 * given before, of a character or binary C type; SQL_SUCCESS,
 * SQL_SUCCESS_WITH_INFO of a value cut short (01004) or of fractions lost
 * (01S07), SQL_NO_DATA once all of it was given, or SQL_ERROR with
 * `diag`'s record: 22003 of a number out of range, 22018 of text that is
 * no number or datetime, 07006 of a C type the value cannot be.
 */
SQLRETURN trl_odbc_give(trl_odbc_diag *diag, trl_conn *conn, trl_odbc_piece *piece, const trl_odbc_target *target);

/** @brief Gives a number, a truth value, a datetime or a UUID: as trl_odbc_give() does of the C types that are not
 * strings. */
SQLRETURN trl_odbc_give_scalar(trl_odbc_diag *diag, const trl_odbc_piece *piece, const trl_odbc_target *target);

/** @brief Gives a datetime, or a character string's, as the date, time or timestamp structure the target's C type is.
 */
SQLRETURN trl_odbc_give_datetime(trl_odbc_diag *diag, const trl_odbc_piece *piece, const trl_odbc_target *target);

/** @brief Gives a UUID, or a UUID's text, as SQLGUID. */
SQLRETURN trl_odbc_give_guid(trl_odbc_diag *diag, const trl_odbc_piece *piece, const trl_odbc_target *target);

#endif
