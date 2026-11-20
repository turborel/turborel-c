/**
 * @file scram_server.c
 * @brief A SCRAM server of a test's own.
 */
#include "scram_server.h"

#include "auth/base64.h"
#include "auth/crypto.h"
#include "auth/scram.h"
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "script.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SALT "c2FsdA=="
#define ITERATIONS 16U
#define U32_BYTES 4U

/* Reads a u32, little-endian, at `at`. */
static uint32_t u32_at(const uint8_t *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8U) | ((uint32_t)at[2] << 16U) | ((uint32_t)at[3] << 24U);
}

/* The bytes field after Hello's version, user and mechanism: the
   mechanism's first bytes, into `out`, null-ended. */
static void hello_initial(trl_slice body, char *out, size_t capacity) {
    size_t at = U32_BYTES;
    for (int field = 0; field < 2; ++field) {
        at += U32_BYTES + u32_at(body.bytes + at);
    }
    size_t length = u32_at(body.bytes + at);
    (void)snprintf(out, capacity, "%.*s", (int)length, (const char *)body.bytes + at + U32_BYTES);
}

/* An Answer's bytes, null-ended. */
static void answer_of(trl_slice body, char *out, size_t capacity) {
    size_t length = u32_at(body.bytes);
    (void)snprintf(out, capacity, "%.*s", (int)length, (const char *)body.bytes + U32_BYTES);
}

/* Writes the server's signature, or a wrong one, of `auth`. */
static void sign(script *s, const scram_server *server, const trl_scram_keys *keys, const char *auth) {
    uint8_t signature[TRL_SHA256_BYTES];
    (void)trl_hmac_sha256(keys->server, sizeof keys->server, (const uint8_t *)auth, strlen(auth), signature);
    if (server->ending == ENDING_WRONG_SIGNATURE) {
        signature[0] ^= 1U;
    }
    char final[TEXT_BYTES] = "v=";
    trl_base64_encode(signature, sizeof signature, final + 2U);
    script_challenge(s, (const uint8_t *) final, strlen(final));
}

/* Checks client-final's proof, and signs, as a server of RFC 5802 does. */
static void check_proof(script *s, scram_server *server, const char *final) {
    const char *proof_at = strstr(final, ",p=");
    char without[TEXT_BYTES];
    (void)snprintf(without, sizeof without, "%.*s", (int)(proof_at - final), final);
    char auth[TEXT_BYTES * 3U];
    (void)snprintf(auth, sizeof auth, "%s,%s,%s", server->bare, server->first, without);
    uint8_t salt[TEXT_BYTES];
    size_t salt_length = 0U;
    (void)trl_base64_decode(SALT, strlen(SALT), salt, &salt_length);
    trl_scram_keys keys;
    trl_salt salted = {salt, salt_length, ITERATIONS};
    (void)trl_scram_derive(PASSWORD, &salted, &keys);
    uint8_t stored[TRL_SHA256_BYTES];
    uint8_t signature[TRL_SHA256_BYTES];
    (void)trl_hash_of(TRL_HASH_SHA256, keys.client, sizeof keys.client, stored);
    (void)trl_hmac_sha256(stored, sizeof stored, (const uint8_t *)auth, strlen(auth), signature);
    uint8_t proof[TEXT_BYTES];
    size_t proof_length = 0U;
    (void)trl_base64_decode(proof_at + 3U, strlen(proof_at + 3U), proof, &proof_length);
    server->proof_right = proof_length == sizeof signature;
    for (size_t i = 0U; server->proof_right && i < sizeof signature; ++i) {
        server->proof_right = (uint8_t)(proof[i] ^ signature[i]) == keys.client[i];
    }
    if (server->ending == ENDING_UNPROVED) {
        script_ready(s, "ANN", NULL, 0U);
        return;
    }
    sign(s, server, &keys, auth);
}

/* Answers a SCRAM login step by step. */
void scram_serving(script *s, uint8_t kind, trl_slice body) {
    scram_server *server = s->data;
    char text[TEXT_BYTES];
    if (kind == TRL_KIND_HELLO) {
        hello_initial(body, text, sizeof text);
        const char *bare = strstr(text, "n=");
        (void)snprintf(server->bare, sizeof server->bare, "%s", bare);
        (void)snprintf(server->first, sizeof server->first, "r=%sSERVER,s=" SALT ",i=%u", strstr(bare, "r=") + 2,
                       ITERATIONS);
        script_challenge(s, (const uint8_t *)server->first, strlen(server->first));
        return;
    }
    answer_of(body, text, sizeof text);
    if (text[0] == '\0') {
        script_ready(s, "ANN", NULL, 0U);
        return;
    }
    check_proof(s, server, text);
}
