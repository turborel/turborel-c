/**
 * @file crypto.c
 * @brief SCRAM's cryptography, of OpenSSL 3.5 or later or AWS-LC, which
 * share these functions (RULES.md D12).
 */
#include "auth/crypto.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef TRL_HAVE_TLS

/* The TLS library's headers declare more than the include checker can
   map to its own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#ifndef OPENSSL_IS_AWSLC
#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#endif

bool trl_crypto_built(void) {
    return true;
}

/* The TLS library's digest of `hash`. */
static const EVP_MD *digest_of(trl_hash hash) {
    switch (hash) {
    case TRL_HASH_SHA256:
        return EVP_sha256();
    case TRL_HASH_SHA384:
        return EVP_sha384();
    case TRL_HASH_SHA512:
        return EVP_sha512();
    }
    return NULL;
}

bool trl_hash_of(trl_hash hash, const uint8_t *bytes, size_t length, uint8_t *out) {
    const EVP_MD *digest = digest_of(hash);
    unsigned int written = 0U;
    return digest != NULL && EVP_Digest(bytes, length, out, &written, digest, NULL) == 1 &&
           written == trl_hash_bytes(hash);
}

bool trl_hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *data, size_t length, uint8_t *out) {
    if (key_length > (size_t)INT_MAX) {
        return false;
    }
    unsigned int written = 0U;
    return HMAC(EVP_sha256(), key, (int)key_length, data, length, out, &written) != NULL && written == TRL_SHA256_BYTES;
}

#ifdef OPENSSL_IS_AWSLC
bool trl_pbkdf2_sha256(const uint8_t *password, size_t password_length, const trl_salt *salt, uint8_t *out) {
    return PKCS5_PBKDF2_HMAC((const char *)password, password_length, salt->bytes, salt->length, salt->iterations,
                             EVP_sha256(), TRL_SHA256_BYTES, out) == 1;
}
#else
/* OSSL_PARAM takes what it reads as void *; the KDF only reads it. */
static void *readable(const void *bytes) {
    union {
        const void *read;
        void *given;
    } pointer = {bytes};
    return pointer.given;
}

/* OpenSSL's PKCS5_PBKDF2_HMAC counts iterations in an int, and a server
   may ask for as many as a u32 holds, so its KDF is called instead, with
   SP 800-132's lower bounds off, as PKCS5_PBKDF2_HMAC has them. */
bool trl_pbkdf2_sha256(const uint8_t *password, size_t password_length, const trl_salt *salt, uint8_t *out) {
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, OSSL_KDF_NAME_PBKDF2, NULL);
    EVP_KDF_CTX *context = kdf == NULL ? NULL : EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (context == NULL) {
        return false;
    }
    uint64_t rounds = salt->iterations;
    int lax = 1;
    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, readable(password), password_length),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, readable(salt->bytes), salt->length),
        OSSL_PARAM_construct_uint64(OSSL_KDF_PARAM_ITER, &rounds),
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0U),
        OSSL_PARAM_construct_int(OSSL_KDF_PARAM_PKCS5, &lax),
        OSSL_PARAM_construct_end(),
    };
    bool derived = EVP_KDF_derive(context, out, TRL_SHA256_BYTES, params) == 1;
    EVP_KDF_CTX_free(context);
    return derived;
}
#endif

bool trl_random(uint8_t *out, size_t length) {
    return length <= (size_t)INT_MAX && RAND_bytes(out, (int)length) == 1;
}

bool trl_same(const uint8_t *a, const uint8_t *b, size_t length) {
    return CRYPTO_memcmp(a, b, length) == 0;
}

void trl_cleanse(void *secret, size_t length) {
    if (secret != NULL) {
        OPENSSL_cleanse(secret, length);
    }
}
// NOLINTEND(misc-include-cleaner)

#else

bool trl_crypto_built(void) {
    return false;
}

bool trl_hash_of(trl_hash hash, const uint8_t *bytes, size_t length, uint8_t *out) {
    (void)hash;
    (void)bytes;
    (void)length;
    (void)out;
    return false;
}

bool trl_hmac_sha256(const uint8_t *key, size_t key_length, const uint8_t *data, size_t length, uint8_t *out) {
    (void)key;
    (void)key_length;
    (void)data;
    (void)length;
    (void)out;
    return false;
}

bool trl_pbkdf2_sha256(const uint8_t *password, size_t password_length, const trl_salt *salt, uint8_t *out) {
    (void)password;
    (void)password_length;
    (void)salt;
    (void)out;
    return false;
}

bool trl_random(uint8_t *out, size_t length) {
    (void)out;
    (void)length;
    return false;
}

bool trl_same(const uint8_t *a, const uint8_t *b, size_t length) {
    /* Without the TLS library nothing secret is compared; this compares
       what is given, every byte, all the same. */
    uint8_t differ = 0U;
    for (size_t i = 0U; i < length; ++i) {
        differ |= (uint8_t)(a[i] ^ b[i]);
    }
    return differ == 0U;
}

/* Writes through a volatile pointer, which no optimizer may skip. */
void trl_cleanse(void *secret, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)secret;
    for (size_t i = 0U; secret != NULL && i < length; ++i) {
        bytes[i] = 0U;
    }
}

#endif

size_t trl_hash_bytes(trl_hash hash) {
    switch (hash) {
    case TRL_HASH_SHA256:
        return TRL_SHA256_BYTES;
    case TRL_HASH_SHA384:
        return TRL_SHA384_BYTES;
    case TRL_HASH_SHA512:
        return TRL_HASH_MAX_BYTES;
    }
    return 0U;
}
