/**
 * @file fetch.c
 * @brief A result's rows fetched (RULES.md D2): SQLBindCol binding a
 * column, column-wise or row-wise, SQLFetch and SQLFetchScroll giving a
 * rowset of SQL_ATTR_ROW_ARRAY_SIZE rows into what is bound, each row's
 * status and the rows fetched told, and SQLGetData giving a column of the
 * rowset's first row, a piece at a time. The rows are all on the client,
 * so a static cursor scrolls; a forward-only one fetches the next rowset
 * alone.
 */
#include "odbc/convert.h"
#include "odbc/describe.h"
#include "odbc/odbc.h"
#include "odbc/types.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The bytes of a C type of fixed size, as bound column-wise; 0 of a string. */
static size_t c_size(SQLSMALLINT c_type) {
    switch (c_type) {
    case SQL_C_STINYINT:
    case SQL_C_TINYINT:
    case SQL_C_UTINYINT:
    case SQL_C_BIT:
        return sizeof(SQLCHAR);
    case SQL_C_SSHORT:
    case SQL_C_SHORT:
    case SQL_C_USHORT:
        return sizeof(SQLSMALLINT);
    case SQL_C_SLONG:
    case SQL_C_LONG:
    case SQL_C_ULONG:
    case SQL_C_FLOAT:
        return sizeof(SQLINTEGER);
    case SQL_C_SBIGINT:
    case SQL_C_UBIGINT:
    case SQL_C_DOUBLE:
        return sizeof(SQLBIGINT);
    case SQL_C_TYPE_DATE:
    case SQL_C_DATE:
        return sizeof(DATE_STRUCT);
    case SQL_C_TYPE_TIME:
    case SQL_C_TIME:
        return sizeof(TIME_STRUCT);
    case SQL_C_TYPE_TIMESTAMP:
    case SQL_C_TIMESTAMP:
        return sizeof(TIMESTAMP_STRUCT);
    case SQL_C_NUMERIC:
        return sizeof(SQL_NUMERIC_STRUCT);
    case SQL_C_GUID:
        return sizeof(SQLGUID);
    default:
        return 0U;
    }
}

/* The C type of column `column` as asked: SQL_C_DEFAULT its SQL type's. */
static SQLSMALLINT c_type_of(const trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLSMALLINT asked) {
    if (asked != SQL_C_DEFAULT) {
        return asked;
    }
    const trl_data_type *type = NULL;
    const char *declared = NULL;
    (void)trl_odbc_column_type(stmt, column, &type, &declared);
    return trl_odbc_default_c_type(trl_odbc_sql_type(type, declared));
}

/* Where row `r` of a rowset puts what `at` points to, bound with
   elements of `element` bytes. */
static void *at_row(const trl_odbc_desc *ard, void *at, size_t r, size_t element) {
    if (at == NULL) {
        return NULL;
    }
    size_t step = ard->bind_type == SQL_BIND_BY_COLUMN ? element : (size_t)ard->bind_type;
    size_t by = ard->bind_offset != NULL ? (size_t)*ard->bind_offset : 0U;
    return (char *)at + by + (r * step);
}

/* Gives column `column` of row `row` to what is bound for row `r` of the rowset. */
static SQLRETURN column_put(trl_odbc_stmt *stmt, size_t row, size_t r, SQLUSMALLINT column) {
    const trl_odbc_field *field = &stmt->ard->fields[column];
    if (field->data == NULL && field->indicator_ptr == NULL) {
        return SQL_SUCCESS;
    }
    SQLSMALLINT c_type = c_type_of(stmt, column, field->concise_type);
    size_t element = c_size(c_type) != 0U ? c_size(c_type) : (size_t)field->octet_length;
    trl_odbc_target target = {
        c_type,
        at_row(stmt->ard, field->data, r, element),
        field->octet_length,
        at_row(stmt->ard, field->length_ptr, r, sizeof(SQLLEN)),
        at_row(stmt->ard, field->indicator_ptr, r, sizeof(SQLLEN)),
        field->scale,
        field->precision,
    };
    trl_odbc_piece piece = {
        trl_result_value(stmt->result, row, (size_t)column - 1U),
        trl_result_text(stmt->result, row, (size_t)column - 1U),
        NULL,
        0U,
        false,
    };
    SQLRETURN given = trl_odbc_give(&stmt->diag, stmt->dbc->conn, &piece, &target);
    free(piece.owned);
    return given;
}

/* Gives row `row` to what is bound for row `r` of the rowset; its status. */
static SQLUSMALLINT row_put(trl_odbc_stmt *stmt, size_t row, size_t r) {
    SQLUSMALLINT status = SQL_ROW_SUCCESS;
    SQLSMALLINT columns = trl_odbc_column_count(stmt);
    for (SQLSMALLINT c = 1; c <= stmt->ard->count && c <= columns; ++c) {
        SQLRETURN given = column_put(stmt, row, r, (SQLUSMALLINT)c);
        if (given == SQL_ERROR) {
            status = SQL_ROW_ERROR;
        } else if (given == SQL_SUCCESS_WITH_INFO && status == SQL_ROW_SUCCESS) {
            status = SQL_ROW_SUCCESS_WITH_INFO;
        }
    }
    return status;
}

/* The rows a cursor has, at most SQL_ATTR_MAX_ROWS. */
static size_t rows_of(const trl_odbc_stmt *stmt) {
    size_t rows = trl_result_row_count(stmt->result);
    return stmt->max_rows > 0U && stmt->max_rows < rows ? (size_t)stmt->max_rows : rows;
}

/* Gives each row of a rowset of `count` rows from row `first` to what is
   bound, each row's status told; the rows that failed into `failed`,
   whether one warned. */
static bool rows_put(trl_odbc_stmt *stmt, size_t first, size_t count, size_t *failed) {
    trl_odbc_desc *ird = &stmt->implicit[TRL_ODBC_IRD];
    size_t size = stmt->ard->array_size == 0U ? 1U : (size_t)stmt->ard->array_size;
    bool warned = false;
    for (size_t r = 0U; r < size; ++r) {
        SQLUSMALLINT status = r < count ? row_put(stmt, first + r, r) : SQL_ROW_NOROW;
        *failed += status == SQL_ROW_ERROR ? 1U : 0U;
        warned = warned || status == SQL_ROW_SUCCESS_WITH_INFO;
        if (ird->array_status != NULL) {
            ird->array_status[r] = status;
        }
    }
    if (ird->rows_processed != NULL) {
        *ird->rows_processed = (SQLULEN)count;
    }
    return warned;
}

/* Fetches the rowset beginning at row `first`, from 0; SQL_NO_DATA past the last. */
static SQLRETURN rowset_fetch(trl_odbc_stmt *stmt, size_t first) {
    size_t total = rows_of(stmt);
    size_t size = stmt->ard->array_size == 0U ? 1U : (size_t)stmt->ard->array_size;
    size_t left = first < total ? total - first : 0U;
    size_t count = left < size ? left : size;
    free(stmt->got.owned);
    memset(&stmt->got, 0, sizeof stmt->got);
    stmt->got_column = 0U;
    stmt->fetched = true;
    stmt->rowset = first < total ? first : total;
    stmt->rowset_rows = count;
    size_t failed = 0U;
    bool warned = rows_put(stmt, first, count, &failed);
    if (count == 0U) {
        return SQL_NO_DATA;
    }
    if (failed == count) {
        return SQL_ERROR;
    }
    return failed > 0U || warned ? trl_odbc_return(&stmt->diag, SQL_SUCCESS_WITH_INFO) : SQL_SUCCESS;
}

/* The first row of the rowset `orientation` and `offset` ask for, or
   `total` past the last, `SIZE_MAX` before the first. */
static size_t first_of(const trl_odbc_stmt *stmt, SQLSMALLINT orientation, SQLLEN offset) {
    size_t total = rows_of(stmt);
    size_t size = stmt->ard->array_size == 0U ? 1U : (size_t)stmt->ard->array_size;
    long long current = stmt->fetched ? (long long)stmt->rowset : -(long long)size;
    long long first = 0;
    switch (orientation) {
    case SQL_FETCH_NEXT:
        first = current + (long long)size;
        break;
    case SQL_FETCH_PRIOR:
        first = current - (long long)size;
        first = current > 0 && first < 0 ? 0 : first;
        break;
    case SQL_FETCH_FIRST:
        first = 0;
        break;
    case SQL_FETCH_LAST:
        first = total > size ? (long long)(total - size) : 0;
        break;
    case SQL_FETCH_ABSOLUTE:
        first = offset < 0 ? (long long)total + offset : offset - 1;
        break;
    default:
        /* Relative to the rowset, or from before the first row, as an absolute row. */
        first = (stmt->fetched ? (long long)stmt->rowset : -1) + offset;
        break;
    }
    if (first < 0) {
        return SIZE_MAX;
    }
    return (size_t)first > total ? total : (size_t)first;
}

/* SQLFetchScroll's work. */
static SQLRETURN scrolled(trl_odbc_stmt *stmt, SQLSMALLINT orientation, SQLLEN offset) {
    trl_odbc_clear(&stmt->diag);
    if (stmt->result == NULL) {
        return trl_odbc_fail(&stmt->diag, "24000", "Invalid cursor state");
    }
    bool known = orientation == SQL_FETCH_NEXT || orientation == SQL_FETCH_PRIOR || orientation == SQL_FETCH_FIRST ||
                 orientation == SQL_FETCH_LAST || orientation == SQL_FETCH_ABSOLUTE ||
                 orientation == SQL_FETCH_RELATIVE;
    if (!known) {
        return trl_odbc_fail(&stmt->diag, "HY106", "Fetch type out of range");
    }
    if (orientation != SQL_FETCH_NEXT && stmt->cursor_type == SQL_CURSOR_FORWARD_ONLY) {
        return trl_odbc_fail(&stmt->diag, "HY106", "Fetch type out of range: the cursor is forward only");
    }
    size_t first = first_of(stmt, orientation, offset);
    if (first == SIZE_MAX) {
        stmt->fetched = false;
        stmt->rowset_rows = 0U;
        return SQL_NO_DATA;
    }
    return rowset_fetch(stmt, first);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLFetch(SQLHSTMT handle) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : scrolled(stmt, SQL_FETCH_NEXT, 0);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT handle, SQLSMALLINT orientation, SQLLEN offset) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    return stmt == NULL ? SQL_INVALID_HANDLE : scrolled(stmt, orientation, offset);
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLBindCol(SQLHSTMT handle, SQLUSMALLINT column, SQLSMALLINT c_type, SQLPOINTER data,
                                             SQLLEN capacity, SQLLEN *indicator) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (column < 1 || column > INT16_MAX) {
        return trl_odbc_fail(&stmt->diag, "07009", "Invalid descriptor index: bookmarks are not given");
    }
    if (data == NULL && indicator == NULL) {
        if ((SQLSMALLINT)column <= stmt->ard->count) {
            stmt->ard->fields[column].data = NULL;
            stmt->ard->fields[column].indicator_ptr = NULL;
            stmt->ard->fields[column].length_ptr = NULL;
            while (stmt->ard->count > 0 && stmt->ard->fields[stmt->ard->count].data == NULL &&
                   stmt->ard->fields[stmt->ard->count].indicator_ptr == NULL) {
                --stmt->ard->count;
            }
        }
        return SQL_SUCCESS;
    }
    trl_odbc_field *field = trl_odbc_desc_field(stmt->ard, (SQLSMALLINT)column);
    if (field == NULL) {
        return trl_odbc_no_memory(&stmt->diag);
    }
    trl_odbc_field_type(field, c_type);
    field->data = data;
    field->octet_length = capacity;
    field->length_ptr = indicator;
    field->indicator_ptr = indicator;
    return SQL_SUCCESS;
}

/* SQLGetData's target of column `column`: its C type as asked, SQL_ARD_TYPE the bound one's. */
static void target_of(const trl_odbc_stmt *stmt, SQLUSMALLINT column, SQLSMALLINT asked, trl_odbc_target *target) {
    const trl_odbc_field *bound = (SQLSMALLINT)column <= stmt->ard->count ? &stmt->ard->fields[column] : NULL;
    if (asked == SQL_ARD_TYPE && bound != NULL) {
        target->c_type = c_type_of(stmt, column, bound->concise_type);
        target->scale = bound->scale;
        target->precision = bound->precision;
    } else {
        target->c_type = c_type_of(stmt, column, asked == SQL_ARD_TYPE ? SQL_C_DEFAULT : asked);
    }
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLGetData(SQLHSTMT handle, SQLUSMALLINT column, SQLSMALLINT c_type, SQLPOINTER data,
                                             SQLLEN capacity, SQLLEN *indicator) {
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    if (stmt->result == NULL || !stmt->fetched || stmt->rowset_rows == 0U) {
        return trl_odbc_fail(&stmt->diag, "24000", "Invalid cursor state");
    }
    if (column < 1 || column > (SQLUSMALLINT)trl_odbc_column_count(stmt)) {
        return trl_odbc_fail(&stmt->diag, "07009", "Invalid descriptor index");
    }
    if (column != stmt->got_column) {
        free(stmt->got.owned);
        memset(&stmt->got, 0, sizeof stmt->got);
        stmt->got_column = column;
        stmt->got.value = trl_result_value(stmt->result, stmt->rowset, (size_t)column - 1U);
        stmt->got.text = trl_result_text(stmt->result, stmt->rowset, (size_t)column - 1U);
    }
    trl_odbc_target target = {SQL_C_DEFAULT, data, capacity, indicator, indicator, 0, 0};
    target_of(stmt, column, c_type, &target);
    return trl_odbc_give(&stmt->diag, stmt->dbc->conn, &stmt->got, &target);
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLSetPos(SQLHSTMT handle, SQLSETPOSIROW row, SQLUSMALLINT operation,
                                            SQLUSMALLINT lock) {
    (void)row;
    (void)operation;
    (void)lock;
    trl_odbc_stmt *stmt = trl_odbc_stmt_of(handle);
    if (stmt == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&stmt->diag);
    return trl_odbc_fail(&stmt->diag, "HYC00", "Optional feature not implemented: positioned operations");
}
