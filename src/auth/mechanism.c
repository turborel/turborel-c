/**
 * @file mechanism.c
 * @brief The mechanisms [PROTOCOL Mechanisms] [CLIENT Connecting]:
 * `trust`, `password`, `certificate`, and SCRAM-SHA-256, bound as
 * SCRAM-SHA-256-PLUS over TLS, its keys found in a cache if given one.
 */
#include "auth/mechanism.h"

#include "auth/binding.h"
#include "auth/crypto.h"
#include "auth/key_cache.h"
#include "auth/scram.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The names of the mechanisms that are not SCRAM. */
#define TRL_TRUST "trust"
#define TRL_PASSWORD "password"
#define TRL_CERTIFICATE "certificate"
/* The bytes of an attribute's name and its equals sign, as "v=". */
#define TRL_ATTRIBUTE_PREFIX 2U

/* Which mechanism it is. */
typedef enum kind {
    KIND_TRUST = 0,
    KIND_PASSWORD,
    KIND_CERTIFICATE,
    KIND_SCRAM,
} kind;

struct trl_mechanism {
    kind kind;
    trl_failure failure;
    /* The password, of `password` and SCRAM, null-ended; cleared as freed. */
    char *password;
    /* SCRAM: whether it may go over plain TCP. */
    bool clear;
    /* SCRAM: how it binds. */
    trl_channel_binding binding;
    /* SCRAM: the key cache, held, or null. */
    trl_key_cache *cache;
    /* SCRAM: the binding the next exchange takes, as bound. */
    trl_scram_channel channel;
    uint8_t channel_data[TRL_HASH_MAX_BYTES];
    size_t channel_length;
    /* SCRAM: the exchange, once begun. */
    bool begun;
    trl_scram exchange;
    /* SCRAM: the salt and iterations the keys of this exchange came of. */
    bool used;
    uint8_t *used_salt;
    size_t used_salt_length;
    uint32_t used_iterations;
};

/* Makes a mechanism of kind `of`, of a copy of `password` if not null. */
static trl_status make(kind of, const char *password, trl_mechanism **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    trl_mechanism *mechanism = trl_calloc(1U, sizeof *mechanism);
    if (mechanism == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&mechanism->failure);
    mechanism->kind = of;
    trl_scram_init(&mechanism->exchange, TRL_SCRAM_UNBOUND, NULL, 0U);
    if (password != NULL) {
        size_t length = strlen(password);
        mechanism->password = trl_malloc(length + 1U);
        if (mechanism->password == NULL) {
            free(mechanism);
            return TRL_NO_MEMORY;
        }
        memcpy(mechanism->password, password, length + 1U);
    }
    *out = mechanism;
    return TRL_OK;
}

trl_status trl_mechanism_trust(trl_mechanism **out) {
    return make(KIND_TRUST, NULL, out);
}

trl_status trl_mechanism_password(const char *password, trl_mechanism **out) {
    if (password == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    return make(KIND_PASSWORD, password, out);
}

trl_status trl_mechanism_certificate(trl_mechanism **out) {
    return make(KIND_CERTIFICATE, NULL, out);
}

trl_status trl_mechanism_scram(const char *password, trl_mechanism **out) {
    if (password == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status made = make(KIND_SCRAM, password, out);
    if (made == TRL_OK && !trl_crypto_built()) {
        trl_failure_set(&(*out)->failure, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                        "SCRAM needs a TLS library's hashes, and this library was built without one");
        return TRL_FAILED;
    }
    return made;
}

/* Whether `mechanism` is SCRAM, as a SCRAM setter needs. */
static bool is_scram(const trl_mechanism *mechanism) {
    return mechanism != NULL && mechanism->kind == KIND_SCRAM;
}

trl_status trl_mechanism_scram_in_the_clear(trl_mechanism *mechanism) {
    if (!is_scram(mechanism)) {
        return TRL_INVALID_ARGUMENT;
    }
    mechanism->clear = true;
    return TRL_OK;
}

trl_status trl_mechanism_scram_channel_binding(trl_mechanism *mechanism, trl_channel_binding binding) {
    bool known = binding == TRL_CHANNEL_BINDING_PREFER || binding == TRL_CHANNEL_BINDING_DISABLE ||
                 binding == TRL_CHANNEL_BINDING_REQUIRE;
    if (!is_scram(mechanism) || !known) {
        return TRL_INVALID_ARGUMENT;
    }
    mechanism->binding = binding;
    return TRL_OK;
}

trl_status trl_mechanism_scram_key_cache(trl_mechanism *mechanism, trl_key_cache *cache) {
    if (!is_scram(mechanism) || cache == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_key_cache_hold(cache);
    trl_key_cache_free(mechanism->cache);
    mechanism->cache = cache;
    return TRL_OK;
}

/* Binds by the exporter where it is had, else by the certificate's hash,
   else says it could bind and believes the server cannot, as
   turborel-client does; with binding disabled, binds nothing. */
trl_status trl_mechanism_bind(trl_mechanism *mechanism, const uint8_t *certificate, size_t certificate_length,
                              const uint8_t *exporter, size_t exporter_length) {
    if (mechanism == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (mechanism->kind != KIND_SCRAM || mechanism->binding == TRL_CHANNEL_BINDING_DISABLE) {
        return TRL_OK;
    }
    mechanism->channel = TRL_SCRAM_UNOFFERED;
    mechanism->channel_length = 0U;
    if (exporter != NULL && exporter_length > 0U && exporter_length <= sizeof mechanism->channel_data) {
        memcpy(mechanism->channel_data, exporter, exporter_length);
        mechanism->channel_length = exporter_length;
        mechanism->channel = TRL_SCRAM_EXPORTER;
    } else if (certificate != NULL &&
               trl_end_point(certificate, certificate_length, mechanism->channel_data, &mechanism->channel_length)) {
        mechanism->channel = TRL_SCRAM_END_POINT;
    }
    return TRL_OK;
}

const char *trl_mechanism_name(const trl_mechanism *mechanism) {
    if (mechanism == NULL) {
        return "";
    }
    switch (mechanism->kind) {
    case KIND_TRUST:
        return TRL_TRUST;
    case KIND_PASSWORD:
        return TRL_PASSWORD;
    case KIND_CERTIFICATE:
        return TRL_CERTIFICATE;
    case KIND_SCRAM:
        break;
    }
    return trl_scram_is_bound_by(mechanism->channel) ? TRL_SCRAM_MECHANISM_PLUS : TRL_SCRAM_MECHANISM;
}

const trl_failure *trl_mechanism_failure(const trl_mechanism *mechanism) {
    return mechanism == NULL ? NULL : &mechanism->failure;
}

/* Forgets the salt and iterations this exchange's keys came of. */
static void forget_used(trl_mechanism *mechanism) {
    free(mechanism->used_salt);
    mechanism->used_salt = NULL;
    mechanism->used_salt_length = 0U;
    mechanism->used = false;
}

void trl_mechanism_free(trl_mechanism *mechanism) {
    if (mechanism == NULL) {
        return;
    }
    if (mechanism->password != NULL) {
        trl_cleanse(mechanism->password, strlen(mechanism->password));
        free(mechanism->password);
    }
    forget_used(mechanism);
    trl_scram_free(&mechanism->exchange);
    trl_key_cache_free(mechanism->cache);
    trl_failure_clear(&mechanism->failure);
    trl_cleanse(mechanism, sizeof *mechanism);
    free(mechanism);
}

/* Begins a SCRAM exchange with the binding bound, which it takes. */
static bool begin_scram(trl_mechanism *mechanism, trl_buffer *out) {
    trl_scram_free(&mechanism->exchange);
    trl_scram_init(&mechanism->exchange, mechanism->channel, mechanism->channel_data, mechanism->channel_length);
    mechanism->channel = TRL_SCRAM_UNBOUND;
    mechanism->channel_length = 0U;
    mechanism->begun = true;
    forget_used(mechanism);
    char nonce[TRL_SCRAM_NONCE_TEXT];
    trl_scram_nonce(nonce);
    return trl_scram_client_first(&mechanism->exchange, "", nonce, out);
}

bool trl_mechanism_initial(trl_mechanism *mechanism, trl_buffer *out, trl_failure *failure) {
    bool written = true;
    switch (mechanism->kind) {
    case KIND_PASSWORD:
        trl_put_raw(out, (const uint8_t *)mechanism->password, strlen(mechanism->password));
        break;
    case KIND_SCRAM:
        written = begin_scram(mechanism, out);
        break;
    case KIND_TRUST:
    case KIND_CERTIFICATE:
        break;
    }
    if (!written || out->state != TRL_BUFFER_OK) {
        trl_failure_no_memory(failure);
        return false;
    }
    return true;
}

/* Drops the cache's entry of this exchange's keys, a login that used
   them having failed. */
static void forget(trl_mechanism *mechanism) {
    if (mechanism->cache != NULL && mechanism->used) {
        trl_salt used = {mechanism->used_salt, mechanism->used_salt_length, mechanism->used_iterations};
        trl_key_cache_forget(mechanism->cache, mechanism->password, &used);
    }
    forget_used(mechanism);
}

/* Remembers the salt and iterations of this exchange's keys. */
static bool remember(trl_mechanism *mechanism, const trl_scram_first *first) {
    forget_used(mechanism);
    mechanism->used_salt = trl_malloc(first->salt_length == 0U ? 1U : first->salt_length);
    if (mechanism->used_salt == NULL) {
        return false;
    }
    if (first->salt_length > 0U) {
        memcpy(mechanism->used_salt, first->salt, first->salt_length);
    }
    mechanism->used_salt_length = first->salt_length;
    mechanism->used_iterations = first->iterations;
    mechanism->used = true;
    return true;
}

/* Answers server-first with client-final, its keys of the cache if given. */
static bool prove(trl_mechanism *mechanism, trl_slice challenge, trl_buffer *out, trl_failure *failure) {
    if (mechanism->binding == TRL_CHANNEL_BINDING_REQUIRE && !trl_scram_is_bound(&mechanism->exchange)) {
        trl_failure_set(failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_INVALID_AUTHORIZATION,
                        "SCRAM: channel binding is required, and this connection is not bound");
        return false;
    }
    trl_scram_first first;
    if (!trl_scram_read_first(&mechanism->exchange, challenge, &first, failure)) {
        return false;
    }
    trl_scram_keys keys;
    trl_salt salt = trl_scram_salt(&first);
    bool derived = mechanism->cache != NULL ? trl_key_cache_derived(mechanism->cache, mechanism->password, &salt, &keys)
                                            : trl_scram_derive(mechanism->password, &salt, &keys);
    bool proved = derived && remember(mechanism, &first) && trl_scram_prove(&mechanism->exchange, &first, &keys, out);
    trl_cleanse(&keys, sizeof keys);
    trl_scram_first_free(&first);
    if (!proved) {
        trl_failure_no_memory(failure);
    }
    return proved;
}

/* Answers a SCRAM Challenge: server-final verified, with an empty
   Answer, or server-first proved. */
static bool answer_scram(trl_mechanism *mechanism, trl_slice challenge, trl_buffer *out, trl_failure *failure) {
    bool last = challenge.length >= TRL_ATTRIBUTE_PREFIX && (challenge.bytes[0] == 'v' || challenge.bytes[0] == 'e') &&
                challenge.bytes[1] == '=';
    if (!mechanism->begun) {
        trl_failure_broken(failure, "SCRAM was not begun");
        return false;
    }
    return last ? trl_scram_verify(&mechanism->exchange, challenge, failure)
                : prove(mechanism, challenge, out, failure);
}

bool trl_mechanism_answer(trl_mechanism *mechanism, trl_slice challenge, trl_buffer *out, trl_failure *failure) {
    if (mechanism->kind == KIND_SCRAM) {
        bool answered = answer_scram(mechanism, challenge, out, failure);
        if (!answered) {
            forget(mechanism);
        }
        return answered;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    (void)snprintf(message, sizeof message, "%s has no answer to a challenge", trl_mechanism_name(mechanism));
    trl_failure_broken(failure, message);
    return false;
}

bool trl_mechanism_secret(const trl_mechanism *mechanism) {
    return mechanism->kind == KIND_PASSWORD || (mechanism->kind == KIND_SCRAM && !mechanism->clear);
}

bool trl_mechanism_ready(const trl_mechanism *mechanism, trl_failure *failure) {
    if (mechanism->kind != KIND_SCRAM || (mechanism->begun && trl_scram_verified(&mechanism->exchange))) {
        return true;
    }
    trl_failure_broken(failure, "the server did not prove it knows the user's verifier");
    return false;
}

void trl_mechanism_refused(trl_mechanism *mechanism) {
    if (mechanism->kind == KIND_SCRAM) {
        forget(mechanism);
    }
}
