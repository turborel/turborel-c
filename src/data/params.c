/**
 * @file params.c
 * @brief A prepared statement's parameters [CLIENT Prepared statements]:
 * values of each kind, copied into memory the list owns.
 */
#include "data/params.h"

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/tags.h"
#include "codec/utf8.h"
#include "codec/values.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The values a list holds before it grows. */
#define TRL_PARAMS_FIRST 8U
/* How much the list grows each time it is full. */
#define TRL_PARAMS_GROWTH 2U

struct trl_params {
    trl_arena arena;
    trl_value *values;
    size_t count;
    size_t capacity;
};

trl_status trl_params_new(trl_params **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = trl_calloc(1U, sizeof **out);
    if (*out == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_arena_init(&(*out)->arena);
    return TRL_OK;
}

/* A new value at the list's end, of `tag`, or null if memory cannot be had. */
static trl_value *add(trl_params *params, uint8_t tag) {
    if (params->count == params->capacity) {
        size_t capacity = params->capacity == 0U ? TRL_PARAMS_FIRST : params->capacity * TRL_PARAMS_GROWTH;
        trl_value *grown = trl_realloc(params->values, capacity * sizeof *grown);
        if (grown == NULL) {
            return NULL;
        }
        params->values = grown;
        params->capacity = capacity;
    }
    trl_value *value = &params->values[params->count++];
    memset(value, 0, sizeof *value);
    value->tag = tag;
    return value;
}

/* A copy of `bytes` in the list's memory, or false if memory cannot be had. */
static bool keep(trl_params *params, const uint8_t *bytes, size_t length, trl_slice *out) {
    out->length = length;
    out->bytes = NULL;
    if (length == 0U) {
        return true;
    }
    uint8_t *copy = trl_arena_alloc(&params->arena, length, 1U);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, bytes, length);
    out->bytes = copy;
    return true;
}

/* Adds a value whose body is `bytes`, of `tag`. */
static trl_status add_bytes(trl_params *params, uint8_t tag, const uint8_t *bytes, size_t length) {
    trl_value *value = add(params, tag);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    if (!keep(params, bytes, length, &value->as.bytes)) {
        --params->count;
        return TRL_NO_MEMORY;
    }
    return TRL_OK;
}

trl_status trl_params_add_null(trl_params *params) {
    if (params == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    return add(params, TRL_TAG_NULL) == NULL ? TRL_NO_MEMORY : TRL_OK;
}

trl_status trl_params_add_text(trl_params *params, const char *text, size_t length) {
    if (params == NULL || (text == NULL && length > 0U) || !trl_utf8_valid((const uint8_t *)text, length)) {
        return TRL_INVALID_ARGUMENT;
    }
    return add_bytes(params, TRL_TAG_CHARACTER, (const uint8_t *)text, length);
}

trl_status trl_params_add_bytes(trl_params *params, const uint8_t *bytes, size_t length) {
    if (params == NULL || (bytes == NULL && length > 0U)) {
        return TRL_INVALID_ARGUMENT;
    }
    return add_bytes(params, TRL_TAG_BINARY, bytes, length);
}

trl_status trl_params_add_xml(trl_params *params, const char *text, size_t length) {
    if (params == NULL || (text == NULL && length > 0U) || !trl_utf8_valid((const uint8_t *)text, length)) {
        return TRL_INVALID_ARGUMENT;
    }
    return add_bytes(params, TRL_TAG_XML, (const uint8_t *)text, length);
}

trl_status trl_params_add_uuid(trl_params *params, const uint8_t octets[TRL_UUID_BYTES]) {
    if (params == NULL || octets == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    return add_bytes(params, TRL_TAG_UUID, octets, TRL_UUID_BYTES);
}

trl_status trl_params_add_json(trl_params *params, const char *text, size_t length) {
    if (params == NULL || (text == NULL && length > 0U)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_buffer stored;
    trl_buffer_init(&stored);
    trl_arena scratch;
    trl_arena_init(&scratch);
    trl_codec_error error = {0, "", ""};
    bool made = trl_json_from_text(&stored, (const uint8_t *)text, length, &scratch, &error);
    trl_status refused = error.sqlcode == TRL_SQLCODE_RESOURCES ? TRL_NO_MEMORY : TRL_INVALID_TEXT;
    trl_status added = made ? add_bytes(params, TRL_TAG_JSON, stored.data, stored.length) : refused;
    trl_arena_free(&scratch);
    trl_buffer_free(&stored);
    return added;
}

trl_status trl_params_add_decimal(trl_params *params, trl_decimal number) {
    if (params == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *value = add(params, TRL_TAG_EXACT);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    value->as.exact = number;
    return TRL_OK;
}

trl_status trl_params_add_double(trl_params *params, double number) {
    if (params == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *value = add(params, TRL_TAG_APPROXIMATE);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    memcpy(&value->as.approximate, &number, sizeof number);
    return TRL_OK;
}

trl_status trl_params_add_boolean(trl_params *params, bool truth) {
    if (params == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *value = add(params, TRL_TAG_BOOLEAN);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    value->as.truth = truth;
    return TRL_OK;
}

trl_status trl_params_add_datetime(trl_params *params, trl_kind kind, trl_datetime datetime) {
    bool known = kind == TRL_KIND_DATE || kind == TRL_KIND_TIME || kind == TRL_KIND_TIME_ZONED ||
                 kind == TRL_KIND_TIMESTAMP || kind == TRL_KIND_TIMESTAMP_ZONED;
    if (params == NULL || !known) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *value = add(params, (uint8_t)kind);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    value->as.datetime = datetime;
    return TRL_OK;
}

trl_status trl_params_add_interval(trl_params *params, trl_interval interval) {
    if (params == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *value = add(params, TRL_TAG_INTERVAL);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    value->as.interval = interval;
    return TRL_OK;
}

trl_status trl_params_add_decfloat(trl_params *params, trl_decfloat number) {
    trl_decfloat made;
    if (params == NULL || !trl_decfloat_made(number, &made)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *value = add(params, TRL_TAG_DECFLOAT);
    if (value == NULL) {
        return TRL_NO_MEMORY;
    }
    value->as.decfloat = made;
    return TRL_OK;
}

trl_status trl_params_add_value(trl_params *params, const trl_value *value) {
    if (params == NULL || value == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_value *added = add(params, TRL_TAG_NULL);
    if (added == NULL) {
        return TRL_NO_MEMORY;
    }
    if (!trl_value_copy(&params->arena, value, added, 0U)) {
        --params->count;
        return TRL_NO_MEMORY;
    }
    return TRL_OK;
}

size_t trl_params_count(const trl_params *params) {
    return params == NULL ? 0U : params->count;
}

const trl_value *trl_params_values(const trl_params *params) {
    return params == NULL ? NULL : params->values;
}

void trl_params_clear(trl_params *params) {
    if (params != NULL) {
        trl_arena_free(&params->arena);
        params->count = 0U;
    }
}

void trl_params_free(trl_params *params) {
    if (params != NULL) {
        trl_arena_free(&params->arena);
        free(params->values);
        free(params);
    }
}
