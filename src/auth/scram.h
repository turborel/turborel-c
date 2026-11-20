/**
 * @file scram.h
 * @brief SCRAM-SHA-256's client exchange (RFC 5802, RFC 7677)
 * [PROTOCOL Mechanisms], as turborel's own client runs it: client-first,
 * server-first read, client-final proved, server-final verified.
 */
#ifndef TRL_AUTH_SCRAM_H
#define TRL_AUTH_SCRAM_H

#include "auth/crypto.h"
#include "codec/buffer.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The mechanism's name, unbound. */
#define TRL_SCRAM_MECHANISM "scram-sha-256"
/** @brief The mechanism's name, bound to the TLS channel. */
#define TRL_SCRAM_MECHANISM_PLUS "scram-sha-256-plus"
/** @brief The random bytes of a client's nonce. */
#define TRL_SCRAM_NONCE_BYTES 18U
/** @brief The characters of a client's nonce, its null included. */
#define TRL_SCRAM_NONCE_TEXT 25U
/** @brief The label `tls-exporter` exports under (RFC 9266). */
#define TRL_SCRAM_EXPORTER_LABEL "EXPORTER-Channel-Binding"
/** @brief The bytes `tls-exporter` exports. */
#define TRL_SCRAM_EXPORTER_BYTES 32U

/** @brief How an exchange is bound, as its GS2 header says. */
typedef enum trl_scram_channel {
    /** @brief `n,,`: not over TLS, or binding disabled. */
    TRL_SCRAM_UNBOUND = 0,
    /** @brief `y,,`: the client could bind, and believes the server cannot. */
    TRL_SCRAM_UNOFFERED,
    /** @brief `p=tls-server-end-point,,`: the server certificate's hash. */
    TRL_SCRAM_END_POINT,
    /** @brief `p=tls-exporter,,`: bytes exported of TLS 1.3's keys. */
    TRL_SCRAM_EXPORTER,
} trl_scram_channel;

/** @brief The keys a password derives: ClientKey and ServerKey. */
typedef struct trl_scram_keys {
    /** @brief HMAC(SaltedPassword, "Client Key"). */
    uint8_t client[TRL_SHA256_BYTES];
    /** @brief HMAC(SaltedPassword, "Server Key"). */
    uint8_t server[TRL_SHA256_BYTES];
} trl_scram_keys;

/** @brief The server's first message, read. */
typedef struct trl_scram_first {
    /** @brief The message whole, a view into the reply. */
    trl_slice message;
    /** @brief The nonce, the client's and the server's. */
    trl_slice both;
    /** @brief The salt, decoded; its own, freed by trl_scram_first_free(). */
    uint8_t *salt;
    /** @brief Its bytes. */
    size_t salt_length;
    /** @brief The iterations, at least 1. */
    uint32_t iterations;
} trl_scram_first;

/** @brief An exchange: where it is, and what it binds. */
typedef struct trl_scram {
    /** @brief How it is bound. */
    trl_scram_channel channel;
    /** @brief The binding's data, of END_POINT and EXPORTER. */
    uint8_t binding[TRL_HASH_MAX_BYTES];
    /** @brief Its bytes. */
    size_t binding_length;
    /** @brief 0 begun, 1 sent, 2 proved, 3 verified. */
    int stage;
    /** @brief client-first-message-bare, once sent. */
    trl_buffer bare;
    /** @brief The client's nonce, once sent. */
    char nonce[TRL_SCRAM_NONCE_TEXT];
    /** @brief The server's signature expected, once proved. */
    uint8_t expected[TRL_SHA256_BYTES];
} trl_scram;

/** @brief Begins an exchange, bound as `channel` says by `binding`'s bytes. */
void trl_scram_init(trl_scram *scram, trl_scram_channel channel, const uint8_t *binding, size_t length);

/** @brief Frees what an exchange holds, its secrets cleared. */
void trl_scram_free(trl_scram *scram);

/** @brief Whether `channel` binds: END_POINT or EXPORTER. */
bool trl_scram_is_bound_by(trl_scram_channel channel);

/** @brief Whether it is bound: by END_POINT or EXPORTER. */
bool trl_scram_is_bound(const trl_scram *scram);

/** @brief A nonce of 18 random bytes, in base 64; "" if the system gave none. */
void trl_scram_nonce(char out[TRL_SCRAM_NONCE_TEXT]);

/**
 * @brief Writes client-first: the GS2 header, then `n=` of `user` and
 * `r=` of `nonce`.
 */
bool trl_scram_client_first(trl_scram *scram, const char *user, const char *nonce, trl_buffer *out);

/** @brief Reads server-first; a failure refuses (SQLSTATE 28000). */
bool trl_scram_read_first(const trl_scram *scram, trl_slice message, trl_scram_first *out, trl_failure *failure);

/** @brief The salt and iterations a server-first read gives. */
trl_salt trl_scram_salt(const trl_scram_first *first);

/** @brief Frees what a server-first read holds. */
void trl_scram_first_free(trl_scram_first *first);

/** @brief Derives a password's keys of a salt and iterations. */
bool trl_scram_derive(const char *password, const trl_salt *salt, trl_scram_keys *out);

/** @brief Writes client-final, its proof made of `keys`. */
bool trl_scram_prove(trl_scram *scram, const trl_scram_first *first, const trl_scram_keys *keys, trl_buffer *out);

/** @brief Verifies server-final; a failure refuses (SQLSTATE 28000). */
bool trl_scram_verify(trl_scram *scram, trl_slice message, trl_failure *failure);

/** @brief Whether the server's signature was verified. */
bool trl_scram_verified(const trl_scram *scram);

#endif
