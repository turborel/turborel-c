/**
 * @file convert.c
 * @brief A result's value given as the C type an application asks for
 * (RULES.md D2), as ODBC's reference's Appendix D converts it: as text,
 * SQL_C_CHAR in UTF-8 or SQL_C_WCHAR, the value's text as
 * trl_value_to_text() writes it, a truth value 1 or 0; as SQL_C_BINARY a
 * binary string's octets, else its text's; each a piece at a time as
 * SQLGetData is called again, a large object read by its locator a range
 * at a time. Numbers, datetimes and UUIDs are `convert_scalar.c`'s.
 */
#include "odbc/convert.h"

#include "memory.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* UTF-8's continuing bytes, which a character cut short would end on. */
#define TRL_CONTINUES_MASK 0xC0U
#define TRL_CONTINUES 0x80U
/* A byte's hexadecimal digits, and a digit's bits. */
#define TRL_HEX_DIGITS 2U
#define TRL_NIBBLE_BITS 4U
#define TRL_NIBBLE_MASK 0x0FU

SQLSMALLINT trl_odbc_default_c_type(SQLSMALLINT sql_type) {
    switch (sql_type) {
    case SQL_SMALLINT:
        return SQL_C_SSHORT;
    case SQL_INTEGER:
        return SQL_C_SLONG;
    case SQL_BIGINT:
        return SQL_C_SBIGINT;
    case SQL_REAL:
        return SQL_C_FLOAT;
    case SQL_DOUBLE:
    case SQL_FLOAT:
        return SQL_C_DOUBLE;
    case SQL_BIT:
        return SQL_C_BIT;
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
        return SQL_C_BINARY;
    case SQL_TYPE_DATE:
        return SQL_C_TYPE_DATE;
    case SQL_TYPE_TIME:
        return SQL_C_TYPE_TIME;
    case SQL_TYPE_TIMESTAMP:
        return SQL_C_TYPE_TIMESTAMP;
    case SQL_GUID:
        return SQL_C_GUID;
    default:
        return SQL_C_CHAR;
    }
}

/* Gives the length the value has left, and its indicator. */
static void lengths_put(const trl_odbc_target *target, SQLLEN length) {
    if (target->length != NULL) {
        *target->length = length;
    }
    if (target->indicator != NULL && target->indicator != target->length) {
        *target->indicator = 0;
    }
}

/* What a cut value says. */
static SQLRETURN cut(trl_odbc_diag *diag) {
    return trl_odbc_warn(diag, "01004", "String data, right truncated");
}

/* Gives the octets `bytes` of `length` left after what was given, ended
   by a null if `text`; a text cut at a character's start. */
static SQLRETURN octets_give(trl_odbc_diag *diag, trl_odbc_piece *piece, const trl_odbc_target *target,
                             const uint8_t *bytes, size_t length) {
    if (piece->done) {
        return SQL_NO_DATA;
    }
    bool text = target->c_type == SQL_C_CHAR;
    size_t left = length - piece->given;
    lengths_put(target, (SQLLEN)left);
    size_t room = target->buffer == NULL || target->capacity <= 0 ? 0U : (size_t)target->capacity;
    room = text && room > 0U ? room - 1U : room;
    if (target->buffer == NULL) {
        return left == 0U ? SQL_SUCCESS : cut(diag);
    }
    size_t taken = left < room ? left : room;
    while (text && taken < left && taken > 0U && (bytes[piece->given + taken] & TRL_CONTINUES_MASK) == TRL_CONTINUES) {
        --taken;
    }
    memcpy(target->buffer, bytes + piece->given, taken);
    if (text && target->capacity > 0) {
        ((char *)target->buffer)[taken] = '\0';
    }
    piece->given += taken;
    if (taken < left) {
        return cut(diag);
    }
    piece->done = true;
    return SQL_SUCCESS;
}

/* Gives `text` as SQLWCHAR, from the units given before. */
static SQLRETURN wide_give(trl_odbc_diag *diag, trl_odbc_piece *piece, const trl_odbc_target *target,
                           const char *text) {
    if (piece->done) {
        return SQL_NO_DATA;
    }
    size_t units = trl_odbc_widen(text, NULL, 0U);
    SQLWCHAR *all = trl_malloc((units + 1U) * sizeof(SQLWCHAR));
    if (all == NULL) {
        return trl_odbc_no_memory(diag);
    }
    (void)trl_odbc_widen(text, all, units + 1U);
    size_t left = units - piece->given;
    lengths_put(target, (SQLLEN)(left * sizeof(SQLWCHAR)));
    size_t room = target->buffer == NULL || target->capacity <= 0 ? 0U : (size_t)target->capacity / sizeof(SQLWCHAR);
    room = room > 0U ? room - 1U : 0U;
    size_t taken = left < room ? left : room;
    if (target->buffer != NULL && target->capacity >= (SQLLEN)sizeof(SQLWCHAR)) {
        memcpy(target->buffer, all + piece->given, taken * sizeof(SQLWCHAR));
        ((SQLWCHAR *)target->buffer)[taken] = 0;
    }
    free(all);
    if (target->buffer == NULL || taken < left) {
        piece->given += target->buffer == NULL ? 0U : taken;
        return left == 0U ? SQL_SUCCESS : cut(diag);
    }
    piece->given = units;
    piece->done = true;
    return SQL_SUCCESS;
}

/* Octets as their text in upper-case hexadecimal, as a binary string's
   text is; a new string, null if memory cannot be had. */
static char *hex_of(const uint8_t *octets, size_t count) {
    static const char digits[] = "0123456789ABCDEF";
    char *text = trl_malloc((count * TRL_HEX_DIGITS) + 1U);
    if (text == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < count; ++i) {
        text[i * TRL_HEX_DIGITS] = digits[octets[i] >> TRL_NIBBLE_BITS];
        text[(i * TRL_HEX_DIGITS) + 1U] = digits[octets[i] & TRL_NIBBLE_MASK];
    }
    text[count * TRL_HEX_DIGITS] = '\0';
    return text;
}

/* Reads a large object whole, as text, into the piece: a binary one's
   octets as hexadecimal. */
static SQLRETURN lob_text(trl_odbc_diag *diag, trl_conn *conn, trl_odbc_piece *piece, const trl_lob_info *info) {
    if (piece->owned != NULL) {
        return SQL_SUCCESS;
    }
    size_t octets = (size_t)info->octets;
    char *text = trl_malloc(octets + 1U);
    if (text == NULL) {
        return trl_odbc_no_memory(diag);
    }
    size_t at = 0U;
    while (at < octets) {
        size_t length = octets - at;
        trl_lob_range range = {at, length, false};
        if (trl_lob_read(conn, piece->value, range, (uint8_t *)text + at, &length) != TRL_OK || length == 0U) {
            free(text);
            return trl_odbc_failed(diag, trl_conn_failure(conn));
        }
        at += length;
    }
    text[octets] = '\0';
    piece->owned = info->character ? text : hex_of((const uint8_t *)text, octets);
    if (!info->character) {
        free(text);
    }
    piece->text = piece->owned;
    return piece->owned != NULL ? SQL_SUCCESS : trl_odbc_no_memory(diag);
}

/* Gives a large object's octets, read by ranges. */
static SQLRETURN lob_octets(trl_odbc_diag *diag, trl_conn *conn, trl_odbc_piece *piece, const trl_odbc_target *target,
                            const trl_lob_info *info) {
    if (piece->done) {
        return SQL_NO_DATA;
    }
    size_t left = (size_t)info->octets - piece->given;
    lengths_put(target, (SQLLEN)left);
    if (target->buffer == NULL || target->capacity <= 0) {
        return left == 0U ? SQL_SUCCESS : cut(diag);
    }
    size_t room = (size_t)target->capacity;
    size_t taken = 0U;
    while (taken < left && taken < room) {
        size_t length = room - taken;
        trl_lob_range range = {piece->given + taken, length, false};
        if (trl_lob_read(conn, piece->value, range, (uint8_t *)target->buffer + taken, &length) != TRL_OK ||
            length == 0U) {
            return trl_odbc_failed(diag, trl_conn_failure(conn));
        }
        taken += length;
    }
    piece->given += taken;
    if (taken < left) {
        return cut(diag);
    }
    piece->done = true;
    return SQL_SUCCESS;
}

/* The text a value is given as: a truth value 1 or 0, else its text. */
static const char *text_of(const trl_odbc_piece *piece) {
    if (trl_value_kind(piece->value) == TRL_KIND_BOOLEAN) {
        bool truth = false;
        (void)trl_value_boolean(piece->value, &truth);
        return truth ? "1" : "0";
    }
    return piece->text != NULL ? piece->text : "";
}

/* Gives a large object as the C type asked for. */
static SQLRETURN lob_give(trl_odbc_diag *diag, trl_conn *conn, trl_odbc_piece *piece, const trl_odbc_target *target,
                          const trl_lob_info *info) {
    if (target->c_type == SQL_C_BINARY) {
        return lob_octets(diag, conn, piece, target, info);
    }
    if (target->c_type != SQL_C_CHAR && target->c_type != SQL_C_WCHAR) {
        return trl_odbc_fail(diag, "07006", "Restricted data type attribute violation");
    }
    SQLRETURN read = lob_text(diag, conn, piece, info);
    if (read != SQL_SUCCESS) {
        return read;
    }
    return target->c_type == SQL_C_WCHAR
               ? wide_give(diag, piece, target, piece->text)
               : octets_give(diag, piece, target, (const uint8_t *)piece->text, strlen(piece->text));
}

/* Gives a string's octets as SQL_C_BINARY: a binary string's own, else its text's. */
static SQLRETURN binary_give(trl_odbc_diag *diag, trl_odbc_piece *piece, const trl_odbc_target *target) {
    trl_kind kind = trl_value_kind(piece->value);
    if (kind == TRL_KIND_BINARY) {
        size_t length = 0U;
        const uint8_t *bytes = trl_value_bytes(piece->value, &length);
        return octets_give(diag, piece, target, bytes, length);
    }
    const char *text = text_of(piece);
    return octets_give(diag, piece, target, (const uint8_t *)text, strlen(text));
}

SQLRETURN trl_odbc_give(trl_odbc_diag *diag, trl_conn *conn, trl_odbc_piece *piece, const trl_odbc_target *target) {
    if (trl_value_is_null(piece->value)) {
        if (piece->done) {
            return SQL_NO_DATA;
        }
        piece->done = true;
        if (target->indicator == NULL) {
            return trl_odbc_fail(diag, "22002", "Indicator variable required but not supplied");
        }
        *target->indicator = SQL_NULL_DATA;
        return SQL_SUCCESS;
    }
    trl_lob_info info;
    if (trl_value_lob(piece->value, &info) == TRL_OK) {
        return lob_give(diag, conn, piece, target, &info);
    }
    switch (target->c_type) {
    case SQL_C_CHAR: {
        const char *text = text_of(piece);
        return octets_give(diag, piece, target, (const uint8_t *)text, strlen(text));
    }
    case SQL_C_WCHAR:
        return wide_give(diag, piece, target, text_of(piece));
    case SQL_C_BINARY:
        return binary_give(diag, piece, target);
    default:
        if (piece->done) {
            return SQL_NO_DATA;
        }
        piece->done = true;
        return trl_odbc_give_scalar(diag, piece, target);
    }
}
