/**
 * @file options.c
 * @brief What a connection asks for, by default every capability but
 * compression, as turborel-client's Offer asks
 * [CLIENT What a connection asks for], and how long it waits [CLIENT Timeouts].
 */
#include "conn/options.h"

#include "codec/buffer.h"
#include "codec/tail.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char *const trl_capabilities_known[TRL_CAPABILITIES_KNOWN] = {
    TRL_CAPABILITY_NOTICES,
    TRL_CAPABILITY_STATUS,
    TRL_CAPABILITY_DIAGNOSTICS,
    TRL_CAPABILITY_DESCRIBE,
    TRL_CAPABILITY_CURSORS,
    TRL_CAPABILITY_PING,
    TRL_CAPABILITY_RESET,
    TRL_CAPABILITY_CHANGE_USER,
    TRL_CAPABILITY_QUERY_ATTRIBUTES,
    TRL_CAPABILITY_CONNECTION_ATTRIBUTES,
    TRL_CAPABILITY_MULTI_STATEMENT,
    TRL_CAPABILITY_COMPRESSION,
    TRL_CAPABILITY_COMMIT_POSITION,
    TRL_CAPABILITY_COPY,
    TRL_CAPABILITY_ROW_COUNTS,
    TRL_CAPABILITY_GENERATED_VALUES,
    TRL_CAPABILITY_FETCH_ENDS,
    TRL_CAPABILITY_CONDITIONS,
    TRL_CAPABILITY_DECLARED_TYPES,
    TRL_CAPABILITY_PARAMETER_MODES,
    TRL_CAPABILITY_DEBUGGER,
};

/* Every capability but compression, in the protocol's table's order. */
static const char *const asked_by_default[] = {
    TRL_CAPABILITY_NOTICES,
    TRL_CAPABILITY_STATUS,
    TRL_CAPABILITY_DIAGNOSTICS,
    TRL_CAPABILITY_DESCRIBE,
    TRL_CAPABILITY_CURSORS,
    TRL_CAPABILITY_PING,
    TRL_CAPABILITY_RESET,
    TRL_CAPABILITY_CHANGE_USER,
    TRL_CAPABILITY_QUERY_ATTRIBUTES,
    TRL_CAPABILITY_CONNECTION_ATTRIBUTES,
    TRL_CAPABILITY_MULTI_STATEMENT,
    TRL_CAPABILITY_COMMIT_POSITION,
    TRL_CAPABILITY_COPY,
    TRL_CAPABILITY_ROW_COUNTS,
    TRL_CAPABILITY_GENERATED_VALUES,
    TRL_CAPABILITY_FETCH_ENDS,
    TRL_CAPABILITY_CONDITIONS,
    TRL_CAPABILITY_DECLARED_TYPES,
    TRL_CAPABILITY_PARAMETER_MODES,
    TRL_CAPABILITY_DEBUGGER,
};

/* A copy of `text`, or null if memory cannot be had. */
static char *copy_of(const char *text) {
    size_t length = strlen(text);
    char *copy = trl_malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

trl_status trl_conn_options_ask_none(trl_conn_options *options) {
    if (options == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    for (size_t i = 0U; i < options->capability_count; ++i) {
        free(options->capabilities[i]);
    }
    free((void *)options->capabilities);
    options->capabilities = NULL;
    options->capability_count = 0U;
    return TRL_OK;
}

trl_status trl_conn_options_ask(trl_conn_options *options, const char *capability) {
    if (options == NULL || capability == NULL || capability[0] == '\0') {
        return TRL_INVALID_ARGUMENT;
    }
    for (size_t i = 0U; i < options->capability_count; ++i) {
        if (strcmp(options->capabilities[i], capability) == 0) {
            return TRL_OK;
        }
    }
    char **grown =
        (char **)trl_realloc((void *)options->capabilities, (options->capability_count + 1U) * sizeof(char *));
    if (grown == NULL) {
        return TRL_NO_MEMORY;
    }
    options->capabilities = grown;
    char *copy = copy_of(capability);
    if (copy == NULL) {
        return TRL_NO_MEMORY;
    }
    options->capabilities[options->capability_count++] = copy;
    return TRL_OK;
}

trl_status trl_conn_options_new(trl_conn_options **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    trl_conn_options *options = trl_calloc(1U, sizeof *options);
    if (options == NULL) {
        return TRL_NO_MEMORY;
    }
    options->compress_from = TRL_COMPRESS_FROM_BYTES;
    options->connect_ms = TRL_CONNECT_WITHIN_MS;
    options->read_ms = TRL_NO_LIMIT;
    for (size_t i = 0U; i < sizeof asked_by_default / sizeof asked_by_default[0]; ++i) {
        if (trl_conn_options_ask(options, asked_by_default[i]) != TRL_OK) {
            trl_conn_options_free(options);
            return TRL_NO_MEMORY;
        }
    }
    *out = options;
    return TRL_OK;
}

trl_status trl_conn_options_attribute(trl_conn_options *options, const char *name, const char *value) {
    if (options == NULL || name == NULL || value == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    size_t named = strlen(name);
    bool fits = options->attribute_count < TRL_MAX_ATTRIBUTES && named > 0U && named <= TRL_MAX_ATTRIBUTE_NAME &&
                strlen(value) <= TRL_MAX_ATTRIBUTE_VALUE;
    if (!fits) {
        return TRL_INVALID_ARGUMENT;
    }
    char *name_copy = copy_of(name);
    char *value_copy = copy_of(value);
    if (name_copy == NULL || value_copy == NULL) {
        free(name_copy);
        free(value_copy);
        return TRL_NO_MEMORY;
    }
    options->names[options->attribute_count] = name_copy;
    options->values[options->attribute_count] = value_copy;
    ++options->attribute_count;
    return TRL_OK;
}

trl_status trl_conn_options_compress_from(trl_conn_options *options, size_t bytes) {
    if (options == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    options->compress_from = bytes;
    return TRL_OK;
}

trl_status trl_conn_options_timeouts(trl_conn_options *options, uint32_t connect_ms, uint32_t read_ms) {
    if (options == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    options->connect_ms = connect_ms;
    options->read_ms = read_ms;
    return TRL_OK;
}

void trl_conn_options_free(trl_conn_options *options) {
    if (options == NULL) {
        return;
    }
    (void)trl_conn_options_ask_none(options);
    for (size_t i = 0U; i < options->attribute_count; ++i) {
        free(options->names[i]);
        free(options->values[i]);
    }
    free(options);
}

size_t trl_conn_options_capabilities(const trl_conn_options *options, trl_slice *out, bool compressing) {
    size_t count = 0U;
    for (size_t i = 0U; i < options->capability_count; ++i) {
        if (!compressing && strcmp(options->capabilities[i], TRL_CAPABILITY_COMPRESSION) == 0) {
            continue;
        }
        out[count].bytes = (const uint8_t *)options->capabilities[i];
        out[count].length = strlen(options->capabilities[i]);
        ++count;
    }
    return count;
}

void trl_conn_options_attributes(const trl_conn_options *options, trl_attribute *out) {
    for (size_t i = 0U; i < options->attribute_count; ++i) {
        out[i].name.bytes = (const uint8_t *)options->names[i];
        out[i].name.length = strlen(options->names[i]);
        out[i].value.bytes = (const uint8_t *)options->values[i];
        out[i].value.length = strlen(options->values[i]);
    }
}
