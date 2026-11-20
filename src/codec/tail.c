/**
 * @file tail.c
 * @brief Tails read in order of tag, and attributes within their bounds
 * [PROTOCOL The tail], as turborel-protocol's `tail.rs` reads them.
 */
#include "codec/tail.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/reader.h"
#include "codec/utf8.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The fewest bytes an attribute takes: its name's and value's lengths. */
#define TRL_ATTRIBUTE_LEAST_BYTES 8U
/* The highest tag a set of tags holds. */
#define TRL_HIGHEST_TAG 31U

bool trl_tail_next(trl_reader *reader, trl_tail *tail, uint32_t taken, uint8_t *out) {
    *out = 0U;
    if (trl_reader_at_end(reader)) {
        return true;
    }
    /* A byte is left, so the read cannot fail. */
    uint8_t tag = 0U;
    (void)trl_read_u8(reader, &tag);
    if (tag <= tail->last || tag > TRL_HIGHEST_TAG || (taken & TRL_TAG_BIT(tag)) == 0U) {
        return trl_reader_fail(reader, "its tail has a tag out of place");
    }
    tail->last = tag;
    *out = tag;
    return true;
}

/* Whether an attribute's name and value are within their bounds. */
static bool attribute_fits(trl_slice name, trl_slice value) {
    return name.length >= 1U && name.length <= TRL_MAX_ATTRIBUTE_NAME && value.length <= TRL_MAX_ATTRIBUTE_VALUE;
}

bool trl_read_attributes(trl_reader *reader, trl_attribute **out, uint32_t *count) {
    if (!trl_read_count(reader, TRL_ATTRIBUTE_LEAST_BYTES, count)) {
        return false;
    }
    if (*count > TRL_MAX_ATTRIBUTES) {
        return trl_reader_fail(reader, "more attributes than the 32 taken");
    }
    *out = trl_reader_alloc(reader, *count, sizeof(trl_attribute));
    if (*out == NULL) {
        return false;
    }
    for (uint32_t i = 0U; i < *count; ++i) {
        trl_attribute *attribute = &(*out)[i];
        if (!trl_read_string(reader, &attribute->name) || !trl_read_string(reader, &attribute->value)) {
            return false;
        }
        if (!attribute_fits(attribute->name, attribute->value)) {
            return trl_reader_fail(reader, "an attribute's name is 1 to 64 bytes and its value at most 1024");
        }
    }
    return true;
}

bool trl_write_attributes(trl_buffer *buffer, const trl_attribute *attributes, size_t count, trl_codec_error *error) {
    if (count == 0U) {
        return true;
    }
    if (count > TRL_MAX_ATTRIBUTES || attributes == NULL) {
        /* Refused before it is sent, the connection kept, with the code the
           server would fail the connection with. */
        trl_codec_fail(error, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_CONNECTION_FAILURE,
                       "more attributes than the 32 a message takes");
        return false;
    }
    trl_put_u8(buffer, TRL_TAIL_ATTRIBUTES);
    trl_put_count(buffer, count);
    for (size_t i = 0U; i < count; ++i) {
        trl_slice name = attributes[i].name;
        trl_slice value = attributes[i].value;
        if (!attribute_fits(name, value) || !trl_utf8_valid(name.bytes, name.length) ||
            !trl_utf8_valid(value.bytes, value.length)) {
            trl_codec_fail(error, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_CONNECTION_FAILURE,
                           "an attribute's name is 1 to 64 bytes and its value at most 1024, UTF-8");
            return false;
        }
        trl_put_bytes(buffer, name);
        trl_put_bytes(buffer, value);
    }
    return true;
}
