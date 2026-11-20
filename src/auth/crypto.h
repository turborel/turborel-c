/**
 * @file crypto.h
 * @brief The hashes, MACs, key derivation and randomness SCRAM and its
 * channel binding take, of the TLS library (RULES.md D12); in a library
 * built without one, each fails, and trl_crypto_built() says so.
 */
#ifndef TRL_AUTH_CRYPTO_H
#define TRL_AUTH_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The bytes of a SHA-256 hash, and of SCRAM's keys. */
#define TRL_SHA256_BYTES 32U
/** @brief The bytes of the longest hash a binding takes, SHA-512's. */
#define TRL_HASH_MAX_BYTES 64U
/** @brief The bytes of a SHA-384 hash. */
#define TRL_SHA384_BYTES 48U

/** @brief The hashes a channel binding takes [PROTOCOL Mechanisms]. */
typedef enum trl_hash {
    /** @brief SHA-256. */
    TRL_HASH_SHA256 = 0,
    /** @brief SHA-384. */
    TRL_HASH_SHA384,
    /** @brief SHA-512. */
    TRL_HASH_SHA512,
} trl_hash;

/** @brief A salt, and the iterations of PBKDF2 over it. */
typedef struct trl_salt {
    /** @brief The salt's bytes. */
    const uint8_t *bytes;
    /** @brief How many. */
    size_t length;
    /** @brief The iterations, at least 1. */
    uint32_t iterations;
} trl_salt;

/** @brief Whether the library was built with a TLS library's hashes. */
bool trl_crypto_built(void);

/** @brief The bytes of `hash`'s digests. */
size_t trl_hash_bytes(trl_hash hash);

/** @brief Hashes `length` bytes into `out`, of trl_hash_bytes(hash) bytes. */
bool trl_hash_of(trl_hash hash, const uint8_t *bytes, size_t length, uint8_t *out);

/** @brief HMAC-SHA-256 of `data` under `key`, into 32 bytes of `out`. */
bool trl_hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *data, size_t length, uint8_t *out);

/** @brief PBKDF2 with HMAC-SHA-256 of a password, into 32 bytes of `out`. */
bool trl_pbkdf2_sha256(const uint8_t *password, size_t password_length, const trl_salt *salt, uint8_t *out);

/** @brief Fills `out` with `length` bytes the system gave at random. */
bool trl_random(uint8_t *out, size_t length);

/** @brief Whether two arrays are the same, in time that tells nothing of where they differ. */
bool trl_same(const uint8_t *a, const uint8_t *b, size_t length);

/** @brief Clears a secret from memory, as no optimizer may skip. */
void trl_cleanse(void *secret, size_t length);

#endif
