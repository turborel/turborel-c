/**
 * @file settings.h
 * @brief Connection settings' state: the names of the settings, a layer
 * of them, laid one over another, the files they come from, and the
 * settings laid.
 */
#ifndef TRL_SETTINGS_SETTINGS_H
#define TRL_SETTINGS_SETTINGS_H

#include "codec/buffer.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief How many settings there are. */
#define TRL_SETTING_COUNT 18U

/** @brief Each setting's place among them, in the order of their names. */
typedef enum trl_key {
    TRL_KEY_HOST = 0,
    TRL_KEY_PORT,
    TRL_KEY_USER,
    TRL_KEY_PASSWORD,
    TRL_KEY_TLS_MODE,
    TRL_KEY_CHANNEL_BINDING,
    TRL_KEY_APPLICATION_NAME,
    TRL_KEY_CONNECT_TIMEOUT,
    TRL_KEY_SERVICE,
    TRL_KEY_SERVICE_FILE,
    TRL_KEY_PASSFILE,
    TRL_KEY_TARGET,
    TRL_KEY_LOAD_BALANCE,
    TRL_KEY_TLS_ROOTS,
    TRL_KEY_TLS_SYSTEM_ROOTS,
    TRL_KEY_TLS_CERTIFICATE,
    TRL_KEY_TLS_KEY,
    TRL_KEY_SERVER_NAME,
} trl_key;

/** @brief A layer of settings: each one's text, or null. */
struct trl_options {
    /** @brief Each setting's value, the layer's own copy. */
    char *values[TRL_SETTING_COUNT];
    /** @brief Its last failure. */
    trl_failure failure;
};

/** @brief Layers laid one over another, as a view: each setting's text, borrowed, or null. */
typedef struct trl_layer {
    /** @brief Each setting's value. */
    const char *values[TRL_SETTING_COUNT];
} trl_layer;

/** @brief A server the settings name. */
typedef struct trl_host {
    /** @brief Its host name or address, or its local socket's path. */
    char *name;
    /** @brief Its port; 0 of a local one. */
    uint16_t port;
    /** @brief Whether it is a Unix socket, or on Windows a named pipe. */
    bool local;
} trl_host;

/** @brief Settings laid. */
struct trl_settings {
    /** @brief Why they were not laid, or the last connection's failure. */
    trl_failure failure;
    /** @brief Each setting's text, as laid, the settings' own. */
    char *laid[TRL_SETTING_COUNT];
    /** @brief The servers. */
    trl_host *hosts;
    /** @brief How many. */
    size_t host_count;
    /** @brief How TLS is used. */
    trl_tls_mode tls_mode;
    /** @brief Whether SCRAM binds its channel. */
    trl_channel_binding channel_binding;
    /** @brief Milliseconds each step of connecting waits; 0 for no limit. */
    uint32_t connect_ms;
    /** @brief What a server must be. */
    trl_target_role target;
    /** @brief Whether the servers are tried in order. */
    trl_load_balance load_balance;
    /** @brief Whether the platform's authorities are trusted too. */
    bool system_roots;
    /** @brief The password file, laid or in the home directory; null if none. */
    char *passfile;
    /** @brief The key cache their connections share; null without SCRAM. */
    trl_key_cache *key_cache;
};

/** @brief The name of the setting at `key`. */
const char *trl_setting_name(trl_key key);

/** @brief The place of the setting named `name`, or -1 if none is. */
int trl_setting_of(const char *name);

/** @brief Fails as settings no connection can be made by: SQLCODE -919, SQLSTATE 08001, `message`. */
void trl_settings_unable(trl_failure *failure, const char *message);

/** @brief Sets the setting at `key` of a layer, copied; false if memory cannot be had. */
bool trl_options_put(trl_options *options, trl_key key, const char *value, size_t length);

/** @brief A view of a layer, null giving none. */
void trl_layer_of(trl_layer *out, const trl_options *options);

/** @brief `upper` laid over `lower`: each of `lower`'s where `upper` has none. */
void trl_layer_over(trl_layer *out, const trl_layer *upper, const trl_layer *lower);

/**
 * @brief The layer the environment gives into `out`, empty: each variable
 * of its table `variable` gives, an empty one giving none.
 */
bool trl_options_environment(trl_options *out, trl_variable variable, void *context);

/**
 * @brief The settings of the section `name` of the service file `text`,
 * read from `file`, into `out`, empty; false with `failure` of a line
 * malformed, or no such section [CLIENT The service file].
 */
bool trl_service_parse(const char *text, const char *name, const char *file, trl_options *out, trl_failure *failure);

/** @brief The section `name` of the service file at `path`, as trl_service_parse() gives it, or why not. */
bool trl_service_read(const char *path, const char *name, trl_options *out, trl_failure *failure);

/**
 * @brief The password the password file `text` gives `user` at `host`
 * and `port`, that of its first line matching all three, appended to
 * `out`; whether there is one [CLIENT The password file].
 */
bool trl_password_find(const char *text, const char *host, const char *port, const char *user, trl_buffer *out);

/** @brief The text of the regular file at `path`, the caller's to free; null if it is none, or cannot be read. */
char *trl_read_regular(const char *path);

/** @brief The file `name` in the home directory, the caller's to free; null if there is none. */
char *trl_at_home(const char *name);

/** @brief `count` places put in an order drawn from `seed`, each order as likely for a seed drawn at random. */
void trl_shuffle(size_t *order, size_t count, uint64_t seed);

#endif
