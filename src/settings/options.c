/**
 * @file options.c
 * @brief Connection settings as text [CLIENT Where settings come from]: a
 * layer of settings, each named by one of the table's names, as the
 * program, a URI, a service's section or the environment gives them, laid
 * one over another; and a connection URI read into one
 * [CLIENT The connection URI].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/utf8.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "settings/settings.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The settings' names, in the order of trl_key. */
static const char *const names[TRL_SETTING_COUNT] = {
    TRL_SETTING_HOST,
    TRL_SETTING_PORT,
    TRL_SETTING_USER,
    TRL_SETTING_PASSWORD,
    TRL_SETTING_TLS_MODE,
    TRL_SETTING_CHANNEL_BINDING,
    TRL_SETTING_APPLICATION_NAME,
    TRL_SETTING_CONNECT_TIMEOUT,
    TRL_SETTING_SERVICE,
    TRL_SETTING_SERVICE_FILE,
    TRL_SETTING_PASSFILE,
    TRL_SETTING_TARGET,
    TRL_SETTING_LOAD_BALANCE,
    TRL_SETTING_TLS_ROOTS,
    TRL_SETTING_TLS_SYSTEM_ROOTS,
    TRL_SETTING_TLS_CERTIFICATE,
    TRL_SETTING_TLS_KEY,
    TRL_SETTING_SERVER_NAME,
};

/* The environment's variables, each the setting it gives. */
static const struct {
    const char *variable;
    trl_key key;
} environment[] = {
    {"TURBOREL_HOST", TRL_KEY_HOST},
    {"TURBOREL_PORT", TRL_KEY_PORT},
    {"TURBOREL_USER", TRL_KEY_USER},
    {"TURBOREL_PASSWORD", TRL_KEY_PASSWORD},
    {"TURBOREL_TLS_MODE", TRL_KEY_TLS_MODE},
    {"TURBOREL_APPLICATION_NAME", TRL_KEY_APPLICATION_NAME},
    {"TURBOREL_CONNECT_TIMEOUT", TRL_KEY_CONNECT_TIMEOUT},
    {"TURBOREL_SERVICE", TRL_KEY_SERVICE},
    {"TURBOREL_SERVICE_FILE", TRL_KEY_SERVICE_FILE},
    {"TURBOREL_PASSFILE", TRL_KEY_PASSFILE},
};

/* The SQLSTATE of a client unable to connect. */
#define TRL_SQLSTATE_UNABLE_TO_CONNECT "08001"
/* The base of a hexadecimal digit's value, and the digits of an octet. */
#define TRL_HEX 16U
#define TRL_HEX_DIGITS 2U
#define TRL_TEN 10U

const char *trl_setting_name(trl_key key) {
    return names[key];
}

int trl_setting_of(const char *name) {
    for (size_t i = 0U; i < TRL_SETTING_COUNT; ++i) {
        if (strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void trl_settings_unable(trl_failure *failure, const char *message) {
    trl_failure_set(failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_UNABLE_TO_CONNECT, message);
}

trl_status trl_options_new(trl_options **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = trl_calloc(1U, sizeof **out);
    if (*out == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&(*out)->failure);
    return TRL_OK;
}

bool trl_options_put(trl_options *options, trl_key key, const char *value, size_t length) {
    char *copy = trl_malloc(length + 1U);
    if (copy == NULL) {
        return false;
    }
    if (length > 0U) {
        memcpy(copy, value, length);
    }
    copy[length] = '\0';
    free(options->values[key]);
    options->values[key] = copy;
    return true;
}

/* Sets the setting `key` names, or fails as no setting it names. */
static trl_status set_named(trl_options *options, const char *key, const char *value, size_t length) {
    int at = trl_setting_of(key);
    if (at < 0) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "%s is no connection setting", key);
        trl_settings_unable(&options->failure, message);
        return TRL_FAILED;
    }
    if (!trl_options_put(options, (trl_key)at, value, length)) {
        trl_failure_no_memory(&options->failure);
        return TRL_NO_MEMORY;
    }
    return TRL_OK;
}

trl_status trl_options_set(trl_options *options, const char *key, const char *value) {
    if (options == NULL || key == NULL || value == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    return set_named(options, key, value, strlen(value));
}

const char *trl_options_get(const trl_options *options, const char *key) {
    if (options == NULL || key == NULL) {
        return NULL;
    }
    int at = trl_setting_of(key);
    return at < 0 ? NULL : options->values[at];
}

const trl_failure *trl_options_failure(const trl_options *options) {
    return options == NULL ? NULL : &options->failure;
}

void trl_options_free(trl_options *options) {
    if (options == NULL) {
        return;
    }
    for (size_t i = 0U; i < TRL_SETTING_COUNT; ++i) {
        free(options->values[i]);
    }
    trl_failure_clear(&options->failure);
    free(options);
}

void trl_layer_of(trl_layer *out, const trl_options *options) {
    for (size_t i = 0U; i < TRL_SETTING_COUNT; ++i) {
        out->values[i] = options == NULL ? NULL : options->values[i];
    }
}

void trl_layer_over(trl_layer *out, const trl_layer *upper, const trl_layer *lower) {
    for (size_t i = 0U; i < TRL_SETTING_COUNT; ++i) {
        out->values[i] = upper->values[i] != NULL ? upper->values[i] : lower->values[i];
    }
}

bool trl_options_environment(trl_options *out, trl_variable variable, void *context) {
    for (size_t i = 0U; i < sizeof environment / sizeof environment[0]; ++i) {
        const char *value = variable(context, environment[i].variable);
        if (value != NULL && value[0] != '\0' && !trl_options_put(out, environment[i].key, value, strlen(value))) {
            return false;
        }
    }
    return true;
}

/* The value of a hexadecimal digit, or -1. */
static int hex(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + (int)TRL_TEN;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + (int)TRL_TEN;
    }
    return -1;
}

/* Fails as a URI's part whose percent-encoding is bad. */
static bool decode_failed(const char *text, size_t length, const char *why, trl_failure *failure) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "%.*s %s", (int)length, text, why);
    trl_settings_unable(failure, message);
    return false;
}

/* `length` octets of `text` percent-decoded (RFC 3986 §2.1) into `out`,
   emptied first, ended by a null not counted; false, with `failure`, of a
   % not followed by two hexadecimal digits, or octets not UTF-8. */
static bool decoded(const char *text, size_t length, trl_buffer *out, trl_failure *failure) {
    out->length = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (text[i] != '%') {
            trl_put_u8(out, (uint8_t)text[i]);
            continue;
        }
        int high = i + 1U < length ? hex(text[i + 1U]) : -1;
        int low = i + TRL_HEX_DIGITS < length ? hex(text[i + TRL_HEX_DIGITS]) : -1;
        if (high < 0 || low < 0) {
            return decode_failed(text, length, "has a % without two hexadecimal digits", failure);
        }
        trl_put_u8(out, (uint8_t)(((unsigned int)high * TRL_HEX) + (unsigned int)low));
        i += TRL_HEX_DIGITS;
    }
    if (!trl_utf8_valid(out->data, out->length)) {
        return decode_failed(text, length, "is not UTF-8 once decoded", failure);
    }
    trl_put_u8(out, 0U);
    if (out->state != TRL_BUFFER_OK) {
        trl_failure_no_memory(failure);
        return false;
    }
    --out->length;
    return true;
}

/* Sets the setting `key` names, both percent-decoded. */
static bool set_decoded(trl_options *options, const char *key, size_t key_length, const char *value,
                        size_t value_length) {
    trl_buffer name;
    trl_buffer text;
    trl_buffer_init(&name);
    trl_buffer_init(&text);
    bool set =
        decoded(key, key_length, &name, &options->failure) && decoded(value, value_length, &text, &options->failure) &&
        set_named(options, (const char *)name.data, text.length == 0U ? "" : (const char *)text.data, text.length) ==
            TRL_OK;
    trl_buffer_free(&name);
    trl_buffer_free(&text);
    return set;
}

/* Sets each `key=value` pair of a URI's query, apart by &. */
static bool query_set(trl_options *options, const char *query) {
    const char *pair = query;
    while (*pair != '\0') {
        const char *end = strchr(pair, '&');
        size_t length = end == NULL ? strlen(pair) : (size_t)(end - pair);
        const char *equals = memchr(pair, '=', length);
        if (length > 0U && equals == NULL) {
            char message[TRL_FAILURE_MESSAGE_BYTES];
            trl_format(message, sizeof message, "%.*s in a connection URI has no =", (int)length, pair);
            trl_settings_unable(&options->failure, message);
            return false;
        }
        if (length > 0U &&
            !set_decoded(options, pair, (size_t)(equals - pair), equals + 1, length - (size_t)(equals - pair) - 1U)) {
            return false;
        }
        pair += length + (end == NULL ? 0U : 1U);
    }
    return true;
}

/* Sets the user, the password and the hosts of a URI's authority. */
static bool authority_set(trl_options *options, const char *authority, size_t length) {
    const char *at = NULL;
    for (size_t i = length; i > 0U && at == NULL; --i) {
        at = authority[i - 1U] == '@' ? authority + i - 1U : NULL;
    }
    size_t info = at == NULL ? 0U : (size_t)(at - authority);
    const char *hosts = at == NULL ? authority : at + 1;
    size_t host_length = length - (size_t)(hosts - authority);
    if (info > 0U) {
        const char *colon = memchr(authority, ':', info);
        size_t user = colon == NULL ? info : (size_t)(colon - authority);
        if (!set_decoded(options, TRL_SETTING_USER, strlen(TRL_SETTING_USER), authority, user) ||
            (colon != NULL &&
             !set_decoded(options, TRL_SETTING_PASSWORD, strlen(TRL_SETTING_PASSWORD), colon + 1, info - user - 1U))) {
            return false;
        }
    }
    return host_length == 0U || set_decoded(options, TRL_SETTING_HOST, strlen(TRL_SETTING_HOST), hosts, host_length);
}

/* Reads `uri` into `options`; false, with its failure, if it is none. */
static bool uri_read(trl_options *options, const char *uri) {
    size_t scheme = strlen(TRL_URI_SCHEME);
    if (strncmp(uri, TRL_URI_SCHEME, scheme) != 0) {
        trl_settings_unable(&options->failure, "a connection URI begins with " TRL_URI_SCHEME);
        return false;
    }
    const char *rest = uri + scheme;
    const char *question = strchr(rest, '?');
    size_t before = question == NULL ? strlen(rest) : (size_t)(question - rest);
    const char *slash = memchr(rest, '/', before);
    size_t authority = slash == NULL ? before : (size_t)(slash - rest);
    if (slash != NULL && before - authority > 1U) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "a connection URI names no database, so has no path, not /%.*s",
                   (int)(before - authority - 1U), slash + 1);
        trl_settings_unable(&options->failure, message);
        return false;
    }
    return authority_set(options, rest, authority) && query_set(options, question == NULL ? "" : question + 1);
}

trl_status trl_options_from_uri(const char *uri, trl_options **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (uri == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status made = trl_options_new(out);
    if (made != TRL_OK) {
        return made;
    }
    if (!uri_read(*out, uri)) {
        return trl_failure_sqlcode(&(*out)->failure) == TRL_SQLCODE_RESOURCES ? TRL_NO_MEMORY : TRL_FAILED;
    }
    return TRL_OK;
}
