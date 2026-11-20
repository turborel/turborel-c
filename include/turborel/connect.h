/**
 * @file connect.h
 * @brief What a connection is made of [CLIENT Connecting]: where the
 * server is (a target), the TLS it is reached over, the mechanism that
 * proves the user, the SCRAM key cache, and the options of what to ask
 * for and how long to wait.
 *
 * Each is a handle the program makes and frees (RULES.md D10). One is
 * used by one thread at a time, but a key cache, which every thread may
 * use at once (RULES.md D11).
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_CONNECT_H
#define TRL_CONNECT_H

#include "turborel/base.h"
#include "turborel/failure.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The port a server listens on unless told another. */
#define TRL_DEFAULT_PORT 7422
/** @brief The named pipe of a server on Windows, as an operator names it. */
#define TRL_DEFAULT_PIPE "\\\\.\\pipe\\turborel"

/** @brief No limit, of a timeout in milliseconds. */
#define TRL_NO_LIMIT 0U
/** @brief How long connecting, TLS and each step of the login wait, by default. */
#define TRL_CONNECT_WITHIN_MS 30000U
/** @brief How long trl_conn_terminate() waits, with no read timeout. */
#define TRL_TERMINATE_WITHIN_MS 60000U
/** @brief How long trl_cancel() waits for the server. */
#define TRL_CANCEL_WITHIN_MS 10000U
/** @brief The longest reply taken, by default: the protocol's longest. */
#define TRL_MAX_BODY_BYTES 16777216U
/** @brief The shortest frame sent compressed, by default. */
#define TRL_COMPRESS_FROM_BYTES 1024U
/** @brief The most entries a SCRAM key cache keeps. */
#define TRL_KEY_CACHE_ENTRIES 64U

/** @brief The capability of a Notice before a statement's completion. */
#define TRL_CAPABILITY_NOTICES "notices"
/** @brief The capability of a Status of each session setting. */
#define TRL_CAPABILITY_STATUS "status"
/** @brief The capability of a failure's diagnostics. */
#define TRL_CAPABILITY_DIAGNOSTICS "diagnostics"
/** @brief The capability of the request Describe. */
#define TRL_CAPABILITY_DESCRIBE "describe"
/** @brief The capability of several numbered cursors of one statement. */
#define TRL_CAPABILITY_CURSORS "cursors"
/** @brief The capability of the request Ping. */
#define TRL_CAPABILITY_PING "ping"
/** @brief The capability of the request Reset. */
#define TRL_CAPABILITY_RESET "reset"
/** @brief The capability of the request ChangeUser. */
#define TRL_CAPABILITY_CHANGE_USER "change-user"
/** @brief The capability of attributes on Query and Execute. */
#define TRL_CAPABILITY_QUERY_ATTRIBUTES "query-attributes"
/** @brief The capability that says Hello carries attributes. */
#define TRL_CAPABILITY_CONNECTION_ATTRIBUTES "connection-attributes"
/** @brief The capability of a Query of several statements. */
#define TRL_CAPABILITY_MULTI_STATEMENT "multi-statement"
/** @brief The capability of compressed frames. */
#define TRL_CAPABILITY_COMPRESSION "compression"
/** @brief The capability of a commit's log position. */
#define TRL_CAPABILITY_COMMIT_POSITION "commit-position"
/** @brief The capability of CopyIn, CopyData and CopyEnd. */
#define TRL_CAPABILITY_COPY "copy"
/** @brief The capability of a statement's row count and time. */
#define TRL_CAPABILITY_ROW_COUNTS "row-counts"
/** @brief The capability of an INSERT's generated identity values. */
#define TRL_CAPABILITY_GENERATED_VALUES "generated-values"
/** @brief The capability of a transaction ended with its cursor. */
#define TRL_CAPABILITY_FETCH_ENDS "fetch-ends"
/** @brief The capability of a statement's conditions in its completion. */
#define TRL_CAPABILITY_CONDITIONS "conditions"

/**
 * @brief TLS settings: the certificate authorities trusted, and the
 * certificate the client presents, if any.
 *
 * Each target made of them shares them, so they are set before the
 * first target is made; a change after is refused.
 */
typedef struct trl_tls trl_tls;

/**
 * @brief Makes TLS settings that trust no authority yet, of TLS 1.2 and
 * 1.3, checking the server's certificate and its name.
 *
 * @param out Where the settings go. On TRL_FAILED they are made, holding
 * the failure (a library built without TLS); free them either way.
 * @return TRL_OK; TRL_FAILED; TRL_INVALID_ARGUMENT if `out` is null;
 * TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_tls_new(trl_tls **out);

/**
 * @brief Trusts the certificate authorities of a PEM file.
 *
 * @param tls The settings.
 * @param path The file.
 * @return TRL_OK; TRL_FAILED if the file cannot be read or holds no
 * certificate (SQLSTATE 08006); TRL_INVALID_ARGUMENT if a pointer is null.
 */
TRL_API trl_status trl_tls_trust_file(trl_tls *tls, const char *path);

/**
 * @brief Trusts the platform's certificate authorities: its store, or the
 * files SSL_CERT_FILE and SSL_CERT_DIR name instead, if either is set.
 *
 * @param tls The settings.
 * @return TRL_OK; TRL_FAILED if they cannot be had; TRL_INVALID_ARGUMENT
 * if `tls` is null.
 */
TRL_API trl_status trl_tls_trust_platform(trl_tls *tls);

/**
 * @brief Presents a certificate chain and its private key, of two PEM
 * files, as the mechanism `certificate` needs.
 *
 * @param tls The settings.
 * @param chain The file of the certificate chain, the client's first.
 * @param key The file of its private key.
 * @return TRL_OK; TRL_FAILED if either cannot be read or the key does not
 * fit the certificate; TRL_INVALID_ARGUMENT if a pointer is null.
 */
TRL_API trl_status trl_tls_present(trl_tls *tls, const char *chain, const char *key);

/**
 * @brief The settings' last failure.
 *
 * @param tls The settings.
 * @return The failure, owned by the settings; null if `tls` is null.
 */
TRL_API const trl_failure *trl_tls_failure(const trl_tls *tls);

/**
 * @brief Frees the program's hold of the settings; each target made of
 * them keeps its own.
 *
 * @param tls The settings; null does nothing.
 */
TRL_API void trl_tls_free(trl_tls *tls);

/** @brief Where a server is: TCP, a Unix socket or named pipe, or TLS. */
typedef struct trl_target trl_target;

/**
 * @brief A server over TCP, at each address its host resolves to, tried
 * in turn.
 *
 * @param host The host's name or address.
 * @param port Its port, as TRL_DEFAULT_PORT.
 * @param out Where the target goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null or `host` is
 * empty; TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_target_tcp(const char *host, uint16_t port, trl_target **out);

/**
 * @brief A server over a Unix socket, or on Windows a named pipe, as
 * TRL_DEFAULT_PIPE.
 *
 * @param path The socket's path, or the pipe's name.
 * @param out Where the target goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null or `path` is
 * empty; TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_target_local(const char *path, trl_target **out);

/**
 * @brief A server over TLS on TCP, its certificate checked against
 * `server_name`.
 *
 * @param host The host's name or address.
 * @param port Its port.
 * @param server_name The name its certificate must have; null for `host`.
 * @param tls The TLS settings, which the target shares from then on.
 * @param out Where the target goes. On TRL_FAILED it is made, holding the
 * failure (the settings trust no authority, or failed); free it either way.
 * @return TRL_OK; TRL_FAILED; TRL_INVALID_ARGUMENT if a pointer is null or
 * `host` is empty; TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_target_tls(const char *host, uint16_t port, const char *server_name, trl_tls *tls,
                                  trl_target **out);

/**
 * @brief The target's last failure.
 *
 * @param target The target.
 * @return The failure, owned by the target; null if `target` is null.
 */
TRL_API const trl_failure *trl_target_failure(const trl_target *target);

/**
 * @brief Frees a target.
 *
 * @param target The target; null does nothing.
 */
TRL_API void trl_target_free(trl_target *target);

/**
 * @brief A cache of the keys SCRAM derives from a password
 * [CLIENT The SCRAM key cache].
 *
 * It keeps the keys of a login the server proved, so a later login that
 * meets the same salt and iterations skips the derivation. An entry is
 * found by the salt, the iterations and a tag of the password under a
 * random secret of the cache's own; it keeps TRL_KEY_CACHE_ENTRIES at
 * most, the oldest dropped first, and drops one when a login that used it
 * fails. It lives in the process's memory alone. Every thread may use one
 * at once.
 */
typedef struct trl_key_cache trl_key_cache;

/**
 * @brief Makes an empty key cache.
 *
 * @param out Where the cache goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `out` is null; TRL_NO_MEMORY,
 * with nothing made.
 */
TRL_API trl_status trl_key_cache_new(trl_key_cache **out);

/**
 * @brief How many entries it keeps.
 *
 * @param cache The cache; null gives 0.
 * @return The count.
 */
TRL_API size_t trl_key_cache_size(trl_key_cache *cache);

/**
 * @brief Its lookups: those that found keys, and those that derived them.
 *
 * @param cache The cache.
 * @param hits Where the hits go; may be null.
 * @param misses Where the misses go; may be null.
 */
TRL_API void trl_key_cache_lookups(trl_key_cache *cache, uint64_t *hits, uint64_t *misses);

/**
 * @brief Drops every entry.
 *
 * @param cache The cache; null does nothing.
 */
TRL_API void trl_key_cache_clear(trl_key_cache *cache);

/**
 * @brief Frees the program's hold of the cache; each mechanism given it
 * keeps its own, and the last frees it.
 *
 * @param cache The cache; null does nothing.
 */
TRL_API void trl_key_cache_free(trl_key_cache *cache);

/** @brief How SCRAM binds its exchange to the TLS connection carrying it. */
typedef enum trl_channel_binding {
    /** @brief Binds where the connection has a binding. */
    TRL_CHANNEL_BINDING_PREFER = 0,
    /** @brief Binds nothing. */
    TRL_CHANNEL_BINDING_DISABLE,
    /** @brief Refuses a login not bound (SQLSTATE 28000). */
    TRL_CHANNEL_BINDING_REQUIRE,
} trl_channel_binding;

/** @brief What proves the user, and the state of its exchange. */
typedef struct trl_mechanism trl_mechanism;

/**
 * @brief The mechanism `trust`: the server trusts the user as it names
 * itself, as a server whose SERVER.TRUST_LOCAL is ON does of a client of
 * its own machine.
 *
 * @param out Where the mechanism goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `out` is null; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_mechanism_trust(trl_mechanism **out);

/**
 * @brief The mechanism `password`: the password sent whole, only over
 * TLS, a Unix socket or a named pipe.
 *
 * @param password The password, UTF-8; copied.
 * @param out Where the mechanism goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_mechanism_password(const char *password, trl_mechanism **out);

/**
 * @brief The mechanism `certificate`: the user proved by the certificate
 * presented over TLS, as trl_tls_present() sets it.
 *
 * @param out Where the mechanism goes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `out` is null; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_mechanism_certificate(trl_mechanism **out);

/**
 * @brief The mechanism SCRAM-SHA-256 of a password, SCRAM-SHA-256-PLUS
 * over TLS, bound to the connection.
 *
 * The password never travels, and the login is refused (SQLSTATE 08006)
 * unless the server proves it knows the user's verifier. It is used only
 * over TLS, a Unix socket or a named pipe, unless
 * trl_mechanism_scram_in_the_clear() says otherwise.
 *
 * @param password The password, UTF-8; copied.
 * @param out Where the mechanism goes. On TRL_FAILED it is made, holding
 * the failure (a library built without the TLS library's hashes); free it
 * either way.
 * @return TRL_OK; TRL_FAILED; TRL_INVALID_ARGUMENT if a pointer is null;
 * TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_mechanism_scram(const char *password, trl_mechanism **out);

/**
 * @brief Lets SCRAM go over plain TCP, to a server whose
 * SERVER.SCRAM_WITHOUT_TLS is ON.
 *
 * @param mechanism A SCRAM mechanism.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if it is null or not SCRAM.
 */
TRL_API trl_status trl_mechanism_scram_in_the_clear(trl_mechanism *mechanism);

/**
 * @brief Says how SCRAM binds its exchange; TRL_CHANNEL_BINDING_PREFER
 * by default.
 *
 * @param mechanism A SCRAM mechanism.
 * @param binding How.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if it is null, not SCRAM, or
 * `binding` is none of the three.
 */
TRL_API trl_status trl_mechanism_scram_channel_binding(trl_mechanism *mechanism, trl_channel_binding binding);

/**
 * @brief Gives SCRAM a key cache, which it holds until it is freed.
 *
 * @param mechanism A SCRAM mechanism.
 * @param cache The cache.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null or the
 * mechanism is not SCRAM.
 */
TRL_API trl_status trl_mechanism_scram_key_cache(trl_mechanism *mechanism, trl_key_cache *cache);

/**
 * @brief Binds SCRAM to a TLS stream the program opened itself, before
 * trl_connect_stream(): over TLS 1.3 by the 32 bytes exported of its keys
 * under the label "EXPORTER-Channel-Binding" with an empty context, else
 * by the certificate the server presented. trl_connect() binds a TLS
 * target itself.
 *
 * @param mechanism The mechanism; any but SCRAM ignores it.
 * @param certificate The server's certificate, DER; null if not had.
 * @param certificate_length Its bytes.
 * @param exporter The exported bytes; null if not had.
 * @param exporter_length Their bytes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `mechanism` is null;
 * TRL_NO_MEMORY, with nothing changed.
 */
TRL_API trl_status trl_mechanism_bind(trl_mechanism *mechanism, const uint8_t *certificate, size_t certificate_length,
                                      const uint8_t *exporter, size_t exporter_length);

/**
 * @brief The name the mechanism is sent by, as "scram-sha-256-plus" once
 * it is bound.
 *
 * @param mechanism The mechanism; null gives "".
 * @return A string the library owns.
 */
TRL_API const char *trl_mechanism_name(const trl_mechanism *mechanism);

/**
 * @brief The mechanism's last failure.
 *
 * @param mechanism The mechanism.
 * @return The failure, owned by the mechanism; null if it is null.
 */
TRL_API const trl_failure *trl_mechanism_failure(const trl_mechanism *mechanism);

/**
 * @brief Frees a mechanism, its password cleared from memory first.
 *
 * @param mechanism The mechanism; null does nothing.
 */
TRL_API void trl_mechanism_free(trl_mechanism *mechanism);

/**
 * @brief What a connection asks for as it opens, and how long it waits
 * [CLIENT What a connection asks for] [CLIENT Timeouts].
 *
 * Made, it asks for every capability but compression, carries no
 * connection attribute, compresses frames from TRL_COMPRESS_FROM_BYTES,
 * waits TRL_CONNECT_WITHIN_MS for each step of connecting, and has no
 * read timeout.
 */
typedef struct trl_conn_options trl_conn_options;

/**
 * @brief Makes options as the defaults say.
 *
 * @param out Where the options go.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `out` is null; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_conn_options_new(trl_conn_options **out);

/**
 * @brief Asks for no capability: the protocol's first form alone.
 *
 * @param options The options.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `options` is null.
 */
TRL_API trl_status trl_conn_options_ask_none(trl_conn_options *options);

/**
 * @brief Asks for a capability too, as TRL_CAPABILITY_COMPRESSION; one
 * asked for already is not asked twice, and the server ignores a name it
 * does not know. A library built without zlib never asks for
 * compression, as it could not read a compressed frame.
 *
 * @param options The options.
 * @param capability Its name; copied.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null or the name is
 * empty; TRL_NO_MEMORY, with nothing changed.
 */
TRL_API trl_status trl_conn_options_ask(trl_conn_options *options, const char *capability);

/**
 * @brief Adds a connection attribute, which the server shows in
 * INFORMATION_SCHEMA.CONNECTION_ATTRIBUTES, the one named `application`
 * in CONNECTIONS too.
 *
 * @param options The options.
 * @param name Its name, 1 to 64 bytes; copied.
 * @param value Its value, at most 1,024 bytes; copied.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null, or past the
 * protocol's limits of 32 attributes and those lengths; TRL_NO_MEMORY,
 * with nothing changed.
 */
TRL_API trl_status trl_conn_options_attribute(trl_conn_options *options, const char *name, const char *value);

/**
 * @brief The shortest frame sent compressed, once compression is granted.
 *
 * @param options The options.
 * @param bytes The bytes.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `options` is null.
 */
TRL_API trl_status trl_conn_options_compress_from(trl_conn_options *options, size_t bytes);

/**
 * @brief How long a connection waits on its server: `connect_ms` to
 * connect, begin TLS, and for each step of the login; `read_ms` for each
 * read or write of a request or reply after. TRL_NO_LIMIT is no limit.
 *
 * @param options The options.
 * @param connect_ms The connect timeout.
 * @param read_ms The read timeout.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if `options` is null.
 */
TRL_API trl_status trl_conn_options_timeouts(trl_conn_options *options, uint32_t connect_ms, uint32_t read_ms);

/**
 * @brief Frees options.
 *
 * @param options The options; null does nothing.
 */
TRL_API void trl_conn_options_free(trl_conn_options *options);

#ifdef __cplusplus
}
#endif

#endif
