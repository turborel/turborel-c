/**
 * @file assign.c
 * @brief A source's value as an import sends it for a parameter of its
 * column's type: a string for a character string column assigned to it as
 * INSERT assigns it [ISO/IEC 9075:1992 §13.8 GR4b iii, §9.2 GR3b, GR3e],
 * so one too long is refused where CAST would cut it; a long string for a
 * large object column sent ahead a piece at a time, by its locator; any
 * other value as it is, for the server to cast.
 */
#include "bulk/assign.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/values.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The rule by which INSERT assigns a value to its column, and the clauses
   of a fixed string's, a varying one's and a large object's assignment. */
#define TRL_INSERT_ASSIGNS                                                                                             \
    "ISO/IEC 9075:1992 \xc2\xa7"                                                                                       \
    "13.8 GR4b iii"
#define TRL_FIXED_CLAUSE                                                                                               \
    ", \xc2\xa7"                                                                                                       \
    "9.2 GR3b ii"
#define TRL_VARYING_CLAUSE                                                                                             \
    ", \xc2\xa7"                                                                                                       \
    "9.2 GR3e ii"
#define TRL_LOB_CLAUSE                                                                                                 \
    "; ISO/IEC 9075-2:2023 \xc2\xa7"                                                                                   \
    "9.2 GR3e ii"
/* The SQLSTATE of a string too long for its column. */
#define TRL_SQLSTATE_RIGHT_TRUNCATION "22001"
/* The bits of a UTF-8 continuation byte, and what they are. */
#define TRL_CONTINUATION_MASK 0xC0U
#define TRL_CONTINUATION 0x80U
/* Room for a type's name, and for a message. */
#define TRL_TYPE_BYTES 64U

/* The units of `length` octets of `text`: characters, or octets. */
static uint64_t units_of(const uint8_t *text, size_t length, bool octets) {
    if (octets) {
        return length;
    }
    uint64_t count = 0U;
    for (size_t i = 0U; i < length; ++i) {
        count += (text[i] & TRL_CONTINUATION_MASK) != TRL_CONTINUATION ? 1U : 0U;
    }
    return count;
}

/* The octets of the first `most` units of `length` octets of `text`. */
static size_t prefix_of(const uint8_t *text, size_t length, uint64_t most, bool octets) {
    if (octets) {
        return most < length ? (size_t)most : length;
    }
    uint64_t seen = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if ((text[i] & TRL_CONTINUATION_MASK) != TRL_CONTINUATION) {
            if (seen == most) {
                return i;
            }
            ++seen;
        }
    }
    return length;
}

/* Whether every octet from `at` of `length` is a space. */
static bool spaces_from(const uint8_t *text, size_t length, size_t at) {
    for (size_t i = at; i < length; ++i) {
        if (text[i] != ' ') {
            return false;
        }
    }
    return true;
}

/* A character string type's name, as Turborel writes it. */
static void type_name(const trl_data_type *type, char *out, size_t capacity) {
    const char *name = "CHARACTER";
    uint64_t length = type->as.character.length;
    uint8_t units = type->as.character.units;
    if (type->tag == TRL_TAG_VARYING) {
        name = "CHARACTER VARYING";
    } else if (type->tag == TRL_TAG_CLOB) {
        name = "CHARACTER LARGE OBJECT";
        length = type->as.lob.length;
        units = type->as.lob.units;
    }
    (void)snprintf(out, capacity, units == TRL_UNITS_OCTETS ? "%s(%llu OCTETS)" : "%s(%llu)", name,
                   (unsigned long long)length);
}

/* Fails: a string too long for its column (SQLSTATE 22001). */
static trl_status too_long(const trl_data_type *type, uint64_t count, bool octets, trl_failure *failure) {
    char name[TRL_TYPE_BYTES];
    type_name(type, name, sizeof name);
    char message[TRL_FAILURE_MESSAGE_BYTES];
    if (type->tag == TRL_TAG_CLOB) {
        (void)snprintf(message, sizeof message, "a large object longer than %s, not padding beyond it, does not fit it",
                       name);
        trl_failure_cite(failure, TRL_SQLCODE_DATA, TRL_SQLSTATE_RIGHT_TRUNCATION, message,
                         TRL_INSERT_ASSIGNS TRL_LOB_CLAUSE);
    } else {
        uint64_t most = type->as.character.length;
        (void)snprintf(message, sizeof message, "a value of %llu %s, not spaces beyond %llu, does not fit %s",
                       (unsigned long long)count, octets ? "octets" : "characters", (unsigned long long)most, name);
        trl_failure_cite(failure, TRL_SQLCODE_DATA, TRL_SQLSTATE_RIGHT_TRUNCATION, message,
                         type->tag == TRL_TAG_CHARACTER ? TRL_INSERT_ASSIGNS TRL_FIXED_CLAUSE
                                                        : TRL_INSERT_ASSIGNS TRL_VARYING_CLAUSE);
    }
    trl_failure_of_type(failure, name);
    return TRL_FAILED;
}

/* Sends `length` octets ahead as a new large object of `character` or not,
   and adds its locator to `out`. */
static trl_status located(trl_conn *conn, bool character, trl_slice bytes, trl_params *out, trl_failure *failure) {
    trl_lob_writer *writer = NULL;
    trl_status status = trl_lob_writer_new(conn, character, &writer);
    status = status == TRL_OK ? trl_lob_write(writer, bytes.bytes, bytes.length) : status;
    status = status == TRL_OK ? trl_lob_writer_finish(writer, out) : status;
    trl_lob_writer_free(writer);
    if (status == TRL_FAILED) {
        trl_failure_copy(failure, trl_conn_failure(conn));
    }
    return status;
}

/* Adds `bytes`, assigned, as a value of `type`: a large object's whole, or
   ahead by its locator if longer than a server sends whole. */
static trl_status add_text(trl_conn *conn, const trl_data_type *type, trl_slice bytes, trl_params *out,
                           trl_failure *failure) {
    if (type->tag != TRL_TAG_CLOB) {
        return trl_params_add_text(out, (const char *)bytes.bytes, bytes.length);
    }
    if (bytes.length > TRL_LOB_INLINE_BYTES) {
        return located(conn, true, bytes, out, failure);
    }
    trl_value lob;
    memset(&lob, 0, sizeof lob);
    lob.tag = TRL_TAG_LOB_WHOLE;
    lob.as.lob.kind = TRL_LOB_CHARACTER;
    lob.as.lob.octets = bytes;
    return trl_params_add_value(out, &lob);
}

/* Assigns a character string to a character string type. */
static trl_status assign_text(trl_conn *conn, const trl_value *value, const trl_data_type *type, trl_params *out,
                              trl_failure *failure) {
    bool lob = type->tag == TRL_TAG_CLOB;
    bool octets = (lob ? type->as.lob.units : type->as.character.units) == TRL_UNITS_OCTETS;
    uint64_t most = lob ? type->as.lob.length : type->as.character.length;
    const uint8_t *text = value->as.bytes.bytes;
    size_t length = value->as.bytes.length;
    uint64_t count = units_of(text, length, octets);
    size_t kept = length;
    if (count > most) {
        kept = prefix_of(text, length, most, octets);
        if (!spaces_from(text, length, kept)) {
            return too_long(type, count, octets, failure);
        }
        count = most;
    }
    if (type->tag != TRL_TAG_CHARACTER || count == most) {
        trl_slice bytes = {text, kept};
        return add_text(conn, type, bytes, out, failure);
    }
    trl_buffer padded;
    trl_buffer_init(&padded);
    trl_put_raw(&padded, text, kept);
    for (uint64_t i = count; i < most; ++i) {
        trl_put_u8(&padded, (uint8_t)' ');
    }
    trl_status added =
        padded.state == TRL_BUFFER_OK
            ? trl_params_add_text(out, padded.length == 0U ? "" : (const char *)padded.data, padded.length)
            : TRL_NO_MEMORY;
    trl_buffer_free(&padded);
    return added;
}

/* The octets of a value a large object column takes, if it holds any. */
static bool octets_of(const trl_value *value, trl_slice *out) {
    switch (value->tag) {
    case TRL_TAG_CHARACTER:
    case TRL_TAG_VARYING:
    case TRL_TAG_BINARY:
    case TRL_TAG_BINARY_VARYING:
        *out = value->as.bytes;
        return true;
    case TRL_TAG_LOB_WHOLE:
        *out = value->as.lob.octets;
        return true;
    default:
        return false;
    }
}

trl_status trl_import_value(trl_conn *conn, const trl_value *value, const trl_data_type *type, trl_params *out,
                            trl_failure *failure) {
    bool text = value->tag == TRL_TAG_CHARACTER || value->tag == TRL_TAG_VARYING;
    bool character = type->tag == TRL_TAG_CHARACTER || type->tag == TRL_TAG_VARYING || (type->tag == TRL_TAG_CLOB);
    if (text && character) {
        return assign_text(conn, value, type, out, failure);
    }
    trl_slice bytes = {NULL, 0U};
    bool lob = type->tag == TRL_TAG_CLOB || type->tag == TRL_TAG_BLOB;
    if (lob && octets_of(value, &bytes) && bytes.length > TRL_LOB_INLINE_BYTES) {
        return located(conn, type->tag == TRL_TAG_CLOB, bytes, out, failure);
    }
    return trl_params_add_value(out, value);
}
