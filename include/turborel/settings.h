/**
 * @file settings.h
 * @brief Connection settings from outside the program
 * [CLIENT Connecting by settings]: where the servers are, as whom to
 * connect, how, and what a server must be, read from layers of options:
 * the program's own or a connection URI's, then a service's section of
 * the service file, then the environment, then the defaults; a password
 * none gives looked up in the password file. A connection made as they
 * say, to the first of several servers that is reached, logs the user in
 * and is of the role asked [CLIENT Several servers], over TLS as its mode
 * says [CLIENT TLS by mode].
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_SETTINGS_H
#define TRL_SETTINGS_H

#include "turborel/base.h"
#include "turborel/connect.h"
#include "turborel/connection.h"
#include "turborel/failure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The settings' names, as the program, a URI's query and a service file write them. */
#define TRL_SETTING_HOST "host"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_PORT "port"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_USER "user"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_PASSWORD "password"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_TLS_MODE "tls_mode"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_CHANNEL_BINDING "channel_binding"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_APPLICATION_NAME "application_name"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_CONNECT_TIMEOUT "connect_timeout"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_SERVICE "service"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_SERVICE_FILE "service_file"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_PASSFILE "passfile"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_TARGET "target"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_LOAD_BALANCE "load_balance"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_TLS_ROOTS "tls_roots"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_TLS_SYSTEM_ROOTS "tls_system_roots"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_TLS_CERTIFICATE "tls_certificate"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_TLS_KEY "tls_key"
/** @brief See TRL_SETTING_HOST. */
#define TRL_SETTING_SERVER_NAME "server_name"

/** @brief The port of the native protocol, where a server listens unless told otherwise. */
#define TRL_DEFAULT_PORT 7422
/** @brief The host connected to when none is named. */
#define TRL_DEFAULT_HOST "localhost"
/** @brief The scheme a connection URI begins with. */
#define TRL_URI_SCHEME "turborel://"

/** @brief A layer of connection settings, each by its name, as text. */
typedef struct trl_options trl_options;

/**
 * @brief Makes a layer of no settings.
 *
 * @param out Where it goes, the caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_options_new(trl_options **out);

/**
 * @brief Sets a setting, by its name, to `value`, copied.
 *
 * @param options The layer.
 * @param key The setting's name, as TRL_SETTING_HOST.
 * @param value Its value, UTF-8.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED,
 * SQLSTATE 08001, the layer's failure, of a key that names no setting;
 * TRL_NO_MEMORY.
 */
TRL_API trl_status trl_options_set(trl_options *options, const char *key, const char *value);

/**
 * @brief A setting's value.
 *
 * @param options The layer.
 * @param key The setting's name.
 * @return Its value, borrowed until it is set again or the layer freed;
 * null if not set.
 */
TRL_API const char *trl_options_get(const trl_options *options, const char *key);

/**
 * @brief Reads a connection URI into a layer [CLIENT The connection URI]:
 * `turborel://[user[:password]@][host[:port][,host[:port]]...][/][?setting=value[&setting=value]...]`,
 * its parts percent-encoded, an IPv6 address in brackets.
 *
 * @param uri The URI.
 * @param out Where the layer goes, the caller's to free; made on
 * TRL_FAILED too, holding why.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED,
 * SQLSTATE 08001, of another scheme, a path, a setting not in the table,
 * a pair without `=`, or bad percent-encoding; TRL_NO_MEMORY, nothing
 * made.
 */
TRL_API trl_status trl_options_from_uri(const char *uri, trl_options **out);

/**
 * @brief The layer's last failure.
 *
 * @param options The layer.
 * @return Its failure, borrowed; null of a null layer.
 */
TRL_API const trl_failure *trl_options_failure(const trl_options *options);

/**
 * @brief Frees a layer.
 *
 * @param options It; null does nothing.
 */
TRL_API void trl_options_free(trl_options *options);

/**
 * @brief Reads one of the environment's variables, as the program gives
 * them.
 *
 * @param context The program's.
 * @param name The variable's name, as TURBOREL_HOST.
 * @return Its value, valid until the next call; null if it is not set.
 */
typedef const char *(*trl_variable)(void *context, const char *name);

/** @brief How a connection over TCP uses TLS. */
typedef enum trl_tls_mode {
    /** @brief Never: plain TCP. */
    TRL_TLS_DISABLE = 0,
    /** @brief TLS if the server takes it, else plain TCP; its certificate not checked. */
    TRL_TLS_PREFER,
    /** @brief TLS or no connection; the server's certificate not checked. */
    TRL_TLS_REQUIRE,
    /** @brief TLS, the server's certificate signed by an authority trusted, its name not checked. */
    TRL_TLS_VERIFY_CA,
    /** @brief TLS, the server's certificate signed by an authority trusted and of the server's name. */
    TRL_TLS_VERIFY_FULL,
} trl_tls_mode;

/** @brief What a server must be for a connection to it to be kept. */
typedef enum trl_target_role {
    /** @brief Any server. */
    TRL_TARGET_ANY = 0,
    /** @brief One that takes changes: a server alone, or its group's primary. */
    TRL_TARGET_READ_WRITE,
    /** @brief One that takes none: a replica following its group. */
    TRL_TARGET_READ_ONLY,
} trl_target_role;

/** @brief Whether servers are tried in the order given. */
typedef enum trl_load_balance {
    /** @brief In order. */
    TRL_LOAD_BALANCE_DISABLE = 0,
    /** @brief In an order drawn at random for each connection. */
    TRL_LOAD_BALANCE_RANDOM,
} trl_load_balance;

/** @brief How to connect, as the settings say. */
typedef struct trl_settings trl_settings;

/**
 * @brief Lays settings: `options`, then the service they or the process's
 * environment name, then the environment, then the defaults, each setting
 * taken from the first layer that has it
 * [CLIENT Where settings come from]. The environment is read once, as it is laid.
 *
 * @param options The program's layer; null for none.
 * @param out Where the settings go, the caller's to free; made on
 * TRL_FAILED too, holding why.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED,
 * SQLSTATE 08001, of a setting that cannot be read, a service file
 * unread or without the section, or no user; TRL_NO_MEMORY, nothing made.
 */
TRL_API trl_status trl_settings_resolve(const trl_options *options, trl_settings **out);

/**
 * @brief Lays settings as trl_settings_resolve() does, `variable` reading
 * the environment's variables.
 *
 * @param options The program's layer; null for none.
 * @param variable The reader of the environment.
 * @param context Its context.
 * @param out Where the settings go, as trl_settings_resolve() gives them.
 * @return As trl_settings_resolve() returns.
 */
TRL_API trl_status trl_settings_resolve_in(const trl_options *options, trl_variable variable, void *context,
                                           trl_settings **out);

/**
 * @brief Lays the settings of a connection URI, as trl_settings_resolve()
 * lays a layer.
 *
 * @param uri The URI.
 * @param out Where the settings go, as trl_settings_resolve() gives them.
 * @return As trl_options_from_uri() and trl_settings_resolve() return.
 */
TRL_API trl_status trl_settings_from_uri(const char *uri, trl_settings **out);

/**
 * @brief The settings' failure: why they were not laid, or the last
 * connection's they made.
 *
 * @param settings The settings.
 * @return The failure, borrowed; null of null settings.
 */
TRL_API const trl_failure *trl_settings_failure(const trl_settings *settings);

/**
 * @brief How many servers the settings name.
 *
 * @param settings The settings; null gives 0.
 * @return How many.
 */
TRL_API size_t trl_settings_host_count(const trl_settings *settings);

/**
 * @brief A server the settings name: its host name or address, and port,
 * or the path of its Unix socket or named pipe.
 *
 * @param settings The settings.
 * @param host Which, from 0, in the order given.
 * @param port Where its port goes, 0 of a local one; may be null.
 * @param local Where whether it is a local one goes; may be null.
 * @return Its name or path, borrowed; null past the last.
 */
TRL_API const char *trl_settings_host(const trl_settings *settings, size_t host, uint16_t *port, bool *local);

/**
 * @brief The user to connect as.
 *
 * @param settings The settings.
 * @return The user, borrowed; null of null settings.
 */
TRL_API const char *trl_settings_user(const trl_settings *settings);

/**
 * @brief A setting's value, as laid, by its name; of `password`, null.
 *
 * @param settings The settings.
 * @param key The setting's name, as TRL_SETTING_APPLICATION_NAME.
 * @return Its value, borrowed; null if no layer set it, or of `password`.
 */
TRL_API const char *trl_settings_get(const trl_settings *settings, const char *key);

/**
 * @brief How TLS is used.
 *
 * @param settings The settings.
 * @return The mode; TRL_TLS_PREFER of null settings.
 */
TRL_API trl_tls_mode trl_settings_tls_mode(const trl_settings *settings);

/**
 * @brief Whether SCRAM binds its channel.
 *
 * @param settings The settings.
 * @return How; TRL_CHANNEL_BINDING_PREFER of null settings.
 */
TRL_API trl_channel_binding trl_settings_channel_binding(const trl_settings *settings);

/**
 * @brief How long each step of connecting waits.
 *
 * @param settings The settings.
 * @return Milliseconds, 0 for no limit.
 */
TRL_API uint32_t trl_settings_connect_timeout(const trl_settings *settings);

/**
 * @brief What a server must be.
 *
 * @param settings The settings.
 * @return The role; TRL_TARGET_ANY of null settings.
 */
TRL_API trl_target_role trl_settings_target(const trl_settings *settings);

/**
 * @brief Whether the servers are tried in order.
 *
 * @param settings The settings.
 * @return Which; TRL_LOAD_BALANCE_DISABLE of null settings.
 */
TRL_API trl_load_balance trl_settings_load_balance(const trl_settings *settings);

/**
 * @brief Whether the platform's authorities are trusted too.
 *
 * @param settings The settings.
 * @return Whether.
 */
TRL_API bool trl_settings_tls_system_roots(const trl_settings *settings);

/**
 * @brief The password to prove at a server: the settings', else the
 * password file's for the server, its port and the user, if the file is
 * a regular file that can be read [CLIENT The password file].
 *
 * @param settings The settings.
 * @param host Which server, from 0.
 * @param out Where the password goes, ended by a null, cut short to fit;
 * may be null if `capacity` is 0.
 * @param capacity The bytes `out` holds.
 * @param length Where the bytes of the whole password go; may be null.
 * @return Whether there is one.
 */
TRL_API bool trl_settings_password_for(const trl_settings *settings, size_t host, char *out, size_t capacity,
                                       size_t *length);

/**
 * @brief The SCRAM key cache every connection the settings make shares,
 * so a reconnection skips a password's derivation
 * [CLIENT The SCRAM key cache].
 *
 * @param settings The settings.
 * @return The cache, borrowed, the settings' own; null of null settings,
 * or of a library built without TLS, which has no SCRAM.
 */
TRL_API trl_key_cache *trl_settings_key_cache(const trl_settings *settings);

/**
 * @brief Connects as the settings say: to the first server, of the hosts
 * in the order they say, at each address its name resolves to, that can
 * be reached, logs the user in and is of the role asked
 * [CLIENT Several servers]. A password is proved by SCRAM-SHA-256, with its channel bound
 * as the settings say; with none, the certificate named is presented;
 * else the user is trusted. A secret mechanism is refused over plain TCP.
 * The connection asks for every capability but compression, and gives
 * `application_name` as the connection attribute `application`.
 *
 * Settings may connect from several threads at once: each connection
 * shares their key cache, which takes every thread.
 *
 * @param settings The settings.
 * @param out Where the connection goes, the caller's to free; made on
 * TRL_FAILED too, holding why.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED with
 * the connection's failure: of one server tried, its own; of several,
 * SQLSTATE 08001 naming each address and why, 28000 where the last
 * refused the user; 28000 before any connection of a channel binding
 * required that cannot bind; TRL_NO_MEMORY, nothing made.
 */
TRL_API trl_status trl_settings_connect(const trl_settings *settings, trl_conn **out);

/**
 * @brief Connects as a `turborel://` URI says, the standard API's way to
 * a connection, as libpq's PQconnectdb: the URI laid with the rest of
 * the settings, as trl_settings_from_uri() lays it, then connected by
 * trl_settings_connect().
 *
 * @param uri The URI.
 * @param out Where the connection goes, the caller's to free; made on
 * TRL_FAILED too, holding why, a URI that cannot be read as well.
 * @return As trl_settings_connect() returns.
 */
TRL_API trl_status trl_connect_uri(const char *uri, trl_conn **out);

/**
 * @brief Frees settings, and their hold of their key cache.
 *
 * @param settings They; null does nothing.
 */
TRL_API void trl_settings_free(trl_settings *settings);

/** @brief What a server is, to a connection's session. */
typedef enum trl_server_role {
    /** @brief It takes changes. */
    TRL_SERVER_READ_WRITE = 0,
    /** @brief It takes none: a replica following its group. */
    TRL_SERVER_READ_ONLY,
    /** @brief A replica whose role the session cannot see. */
    TRL_SERVER_UNKNOWN,
} trl_server_role;

/**
 * @brief The role of a connection's server, as its session sees it: as
 * SERVER_READ_ONLY last told it, if the server tells it; else read from
 * INFORMATION_SCHEMA.REPLICAS by a statement that is its own transaction
 * [CLIENT Several servers].
 *
 * @param conn The connection.
 * @param out Where the role goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_FAILED with
 * the connection's failure, the query's, but of a server alone, which
 * has no REPLICAS.
 */
TRL_API trl_status trl_conn_role(trl_conn *conn, trl_server_role *out);

#ifdef __cplusplus
}
#endif

#endif
