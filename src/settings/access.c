/**
 * @file access.c
 * @brief What settings laid say [CLIENT Where settings come from]: the
 * servers, the user, each setting as laid but the password, and the
 * password to prove at a server, the settings' or the password file's
 * [CLIENT The password file].
 */
#include "codec/buffer.h"
#include "format.h"
#include "settings/settings.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Room for a port's text. */
#define TRL_PORT_BYTES 8U

const trl_failure *trl_settings_failure(const trl_settings *settings) {
    return settings == NULL ? NULL : &settings->failure;
}

size_t trl_settings_host_count(const trl_settings *settings) {
    return settings == NULL ? 0U : settings->host_count;
}

const char *trl_settings_host(const trl_settings *settings, size_t host, uint16_t *port, bool *local) {
    if (settings == NULL || host >= settings->host_count) {
        return NULL;
    }
    if (port != NULL) {
        *port = settings->hosts[host].port;
    }
    if (local != NULL) {
        *local = settings->hosts[host].local;
    }
    return settings->hosts[host].name;
}

const char *trl_settings_user(const trl_settings *settings) {
    return settings == NULL ? NULL : settings->laid[TRL_KEY_USER];
}

const char *trl_settings_get(const trl_settings *settings, const char *key) {
    int at = settings == NULL || key == NULL ? -1 : trl_setting_of(key);
    return at < 0 || at == TRL_KEY_PASSWORD ? NULL : settings->laid[at];
}

trl_tls_mode trl_settings_tls_mode(const trl_settings *settings) {
    return settings == NULL ? TRL_TLS_PREFER : settings->tls_mode;
}

trl_channel_binding trl_settings_channel_binding(const trl_settings *settings) {
    return settings == NULL ? TRL_CHANNEL_BINDING_PREFER : settings->channel_binding;
}

uint32_t trl_settings_connect_timeout(const trl_settings *settings) {
    return settings == NULL ? 0U : settings->connect_ms;
}

trl_target_role trl_settings_target(const trl_settings *settings) {
    return settings == NULL ? TRL_TARGET_ANY : settings->target;
}

trl_load_balance trl_settings_load_balance(const trl_settings *settings) {
    return settings == NULL ? TRL_LOAD_BALANCE_DISABLE : settings->load_balance;
}

bool trl_settings_tls_system_roots(const trl_settings *settings) {
    return settings != NULL && settings->system_roots;
}

trl_key_cache *trl_settings_key_cache(const trl_settings *settings) {
    return settings == NULL ? NULL : settings->key_cache;
}

/* Gives `text` to `out` of `capacity`, ended by a null, cut short to fit. */
static void give(const uint8_t *text, size_t length, char *out, size_t capacity, size_t *whole) {
    if (whole != NULL) {
        *whole = length;
    }
    if (capacity > 0U) {
        size_t kept = length < capacity - 1U ? length : capacity - 1U;
        if (kept > 0U) {
            memcpy(out, text, kept);
        }
        out[kept] = '\0';
    }
}

bool trl_settings_password_for(const trl_settings *settings, size_t host, char *out, size_t capacity, size_t *length) {
    if (settings == NULL || host >= settings->host_count || (out == NULL && capacity > 0U)) {
        return false;
    }
    const char *given = settings->laid[TRL_KEY_PASSWORD];
    if (given != NULL) {
        give((const uint8_t *)given, strlen(given), out, capacity, length);
        return true;
    }
    char *text = settings->passfile == NULL ? NULL : trl_read_regular(settings->passfile);
    if (text == NULL) {
        return false;
    }
    const trl_host *at = &settings->hosts[host];
    char port[TRL_PORT_BYTES];
    trl_format(port, sizeof port, "%u", at->local ? (unsigned int)TRL_DEFAULT_PORT : (unsigned int)at->port);
    trl_buffer password;
    trl_buffer_init(&password);
    bool found = trl_password_find(text, at->name, port, settings->laid[TRL_KEY_USER], &password) &&
                 password.state == TRL_BUFFER_OK;
    if (found) {
        give(password.data, password.length, out, capacity, length);
    }
    trl_buffer_free(&password);
    free(text);
    return found;
}
