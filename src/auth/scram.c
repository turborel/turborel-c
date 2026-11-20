/**
 * @file scram.c
 * @brief SCRAM-SHA-256's client exchange, step by step as turborel's own
 * client takes it [PROTOCOL Mechanisms]; each refusal is SQLCODE -919,
 * SQLSTATE 28000, as a login refused.
 */
#include "auth/scram.h"

#include "auth/base64.h"
#include "auth/crypto.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/utf8.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The GS2 headers of each binding (RFC 5802 section 7). */
#define TRL_GS2_NONE "n,,"
#define TRL_GS2_UNOFFERED "y,,"
#define TRL_GS2_END_POINT "p=tls-server-end-point,,"
#define TRL_GS2_EXPORTER "p=tls-exporter,,"
/* The stages of an exchange. */
#define TRL_STAGE_BEGUN 0
#define TRL_STAGE_SENT 1
#define TRL_STAGE_PROVED 2
#define TRL_STAGE_VERIFIED 3
/* A nonce's printable characters, a comma not among them (RFC 5802). */
#define TRL_NONCE_FIRST 0x21U
#define TRL_NONCE_LAST 0x7eU
/* What the keys of a salted password are made of (RFC 5802 section 3). */
#define TRL_CLIENT_KEY "Client Key"
#define TRL_SERVER_KEY "Server Key"
#define TRL_DECIMAL_BASE 10U
/* The bytes of an attribute's name and its equals sign, as "r=". */
#define TRL_ATTRIBUTE_PREFIX 2U

/* Refuses the login: "SCRAM: " and why. */
static bool refused(trl_failure *failure, const char *why) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    (void)snprintf(message, sizeof message, "SCRAM: %s", why);
    trl_failure_set(failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_INVALID_AUTHORIZATION, message);
    return false;
}

void trl_scram_init(trl_scram *scram, trl_scram_channel channel, const uint8_t *binding, size_t length) {
    memset(scram, 0, sizeof *scram);
    trl_buffer_init(&scram->bare);
    scram->channel = channel;
    if (length > 0U && length <= sizeof scram->binding) {
        memcpy(scram->binding, binding, length);
        scram->binding_length = length;
    }
}

void trl_scram_free(trl_scram *scram) {
    trl_buffer_free(&scram->bare);
    trl_cleanse(scram->expected, sizeof scram->expected);
    trl_cleanse(scram->binding, sizeof scram->binding);
}

bool trl_scram_is_bound_by(trl_scram_channel channel) {
    return channel == TRL_SCRAM_END_POINT || channel == TRL_SCRAM_EXPORTER;
}

bool trl_scram_is_bound(const trl_scram *scram) {
    return trl_scram_is_bound_by(scram->channel);
}

/* The GS2 header of `channel`. */
static const char *header(trl_scram_channel channel) {
    switch (channel) {
    case TRL_SCRAM_UNOFFERED:
        return TRL_GS2_UNOFFERED;
    case TRL_SCRAM_END_POINT:
        return TRL_GS2_END_POINT;
    case TRL_SCRAM_EXPORTER:
        return TRL_GS2_EXPORTER;
    case TRL_SCRAM_UNBOUND:
        break;
    }
    return TRL_GS2_NONE;
}

void trl_scram_nonce(char out[TRL_SCRAM_NONCE_TEXT]) {
    uint8_t bytes[TRL_SCRAM_NONCE_BYTES];
    if (!trl_random(bytes, sizeof bytes)) {
        out[0] = '\0';
        return;
    }
    trl_base64_encode(bytes, sizeof bytes, out);
}

/* Appends `text`, null-ended. */
static void put_text(trl_buffer *out, const char *text) {
    trl_put_raw(out, (const uint8_t *)text, strlen(text));
}

/* Appends a user's name as a saslname: `=` as `=3D`, `,` as `=2C`. */
static void put_saslname(trl_buffer *out, const char *user) {
    for (const char *c = user; *c != '\0'; ++c) {
        if (*c == '=') {
            put_text(out, "=3D");
        } else if (*c == ',') {
            put_text(out, "=2C");
        } else {
            trl_put_u8(out, (uint8_t)*c);
        }
    }
}

bool trl_scram_client_first(trl_scram *scram, const char *user, const char *nonce, trl_buffer *out) {
    trl_buffer_free(&scram->bare);
    put_text(&scram->bare, "n=");
    put_saslname(&scram->bare, user);
    put_text(&scram->bare, ",r=");
    put_text(&scram->bare, nonce);
    (void)snprintf(scram->nonce, sizeof scram->nonce, "%s", nonce);
    put_text(out, header(scram->channel));
    trl_put_raw(out, scram->bare.data, scram->bare.length);
    scram->stage = TRL_STAGE_SENT;
    return scram->bare.state == TRL_BUFFER_OK && out->state == TRL_BUFFER_OK;
}

/* The next field of `message` from `*at`, up to a comma; false past its end. */
static bool next_field(trl_slice message, size_t *at, trl_slice *field) {
    if (*at > message.length) {
        return false;
    }
    size_t end = *at;
    while (end < message.length && message.bytes[end] != ',') {
        ++end;
    }
    field->bytes = message.bytes + *at;
    field->length = end - *at;
    *at = end + 1U;
    return true;
}

/* The value of attribute `name` of `field`, as `name=value`. */
static bool attribute(bool present, trl_slice field, char name, trl_slice *value, trl_failure *failure) {
    char why[TRL_FAILURE_MESSAGE_BYTES];
    if (!present) {
        (void)snprintf(why, sizeof why, "the attribute %c is missing", name);
        return refused(failure, why);
    }
    if (field.length < TRL_ATTRIBUTE_PREFIX || field.bytes[0] != (uint8_t)name || field.bytes[1] != '=') {
        (void)snprintf(why, sizeof why, "the attribute %c was expected, not %.*s", name, (int)field.length,
                       (const char *)field.bytes);
        return refused(failure, why);
    }
    value->bytes = field.bytes + TRL_ATTRIBUTE_PREFIX;
    value->length = field.length - TRL_ATTRIBUTE_PREFIX;
    return true;
}

/* Whether `nonce` is printable, no comma among it, and not empty. */
static bool is_nonce(trl_slice nonce) {
    if (nonce.length == 0U) {
        return false;
    }
    for (size_t i = 0U; i < nonce.length; ++i) {
        uint8_t b = nonce.bytes[i];
        if (b < TRL_NONCE_FIRST || b > TRL_NONCE_LAST || b == ',') {
            return false;
        }
    }
    return true;
}

/* Reads a positive u32, an optional `+` before its digits, as Rust reads one. */
static bool read_iterations(trl_slice text, uint32_t *out) {
    size_t at = text.length > 0U && text.bytes[0] == '+' ? 1U : 0U;
    if (at == text.length) {
        return false;
    }
    uint64_t value = 0U;
    for (; at < text.length; ++at) {
        uint8_t c = text.bytes[at];
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * TRL_DECIMAL_BASE) + (uint64_t)(c - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    *out = (uint32_t)value;
    return value != 0U;
}

/* Reads the salt, base 64, into storage of its own. */
static bool read_salt(trl_slice text, trl_scram_first *out) {
    out->salt = trl_malloc(trl_base64_capacity(text.length));
    return out->salt != NULL && trl_base64_decode((const char *)text.bytes, text.length, out->salt, &out->salt_length);
}

bool trl_scram_read_first(const trl_scram *scram, trl_slice message, trl_scram_first *out, trl_failure *failure) {
    memset(out, 0, sizeof *out);
    if (scram->stage != TRL_STAGE_SENT) {
        return refused(failure, "the server's first message came out of turn");
    }
    if (!trl_utf8_valid(message.bytes, message.length)) {
        return refused(failure, "a message is not UTF-8");
    }
    size_t at = 0U;
    trl_slice field = {NULL, 0U};
    trl_slice salt = {NULL, 0U};
    trl_slice iterations = {NULL, 0U};
    /* Each field is read before it is passed on: the order a call's
       arguments are evaluated in is the compiler's. */
    bool present = next_field(message, &at, &field);
    if (!attribute(present, field, 'r', &out->both, failure)) {
        return false;
    }
    present = next_field(message, &at, &field);
    if (!attribute(present, field, 's', &salt, failure)) {
        return false;
    }
    present = next_field(message, &at, &field);
    if (!attribute(present, field, 'i', &iterations, failure)) {
        return false;
    }
    if (next_field(message, &at, &field)) {
        return refused(failure, "the server's first message has more than it should");
    }
    size_t mine = strlen(scram->nonce);
    if (!is_nonce(out->both) || out->both.length <= mine || memcmp(out->both.bytes, scram->nonce, mine) != 0) {
        return refused(failure, "the server's nonce does not extend the client's");
    }
    if (!read_salt(salt, out)) {
        trl_scram_first_free(out);
        return refused(failure, "the salt is not base 64");
    }
    if (!read_iterations(iterations, &out->iterations)) {
        trl_scram_first_free(out);
        return refused(failure, "the iterations are no positive number");
    }
    out->message = message;
    return true;
}

void trl_scram_first_free(trl_scram_first *first) {
    free(first->salt);
    first->salt = NULL;
    first->salt_length = 0U;
}

trl_salt trl_scram_salt(const trl_scram_first *first) {
    trl_salt salt = {first->salt, first->salt_length, first->iterations};
    return salt;
}

bool trl_scram_derive(const char *password, const trl_salt *salt, trl_scram_keys *out) {
    uint8_t salted[TRL_SHA256_BYTES];
    bool derived = trl_pbkdf2_sha256((const uint8_t *)password, strlen(password), salt, salted) &&
                   trl_hmac_sha256(salted, sizeof salted, (const uint8_t *)TRL_CLIENT_KEY, sizeof TRL_CLIENT_KEY - 1U,
                                   out->client) &&
                   trl_hmac_sha256(salted, sizeof salted, (const uint8_t *)TRL_SERVER_KEY, sizeof TRL_SERVER_KEY - 1U,
                                   out->server);
    trl_cleanse(salted, sizeof salted);
    return derived;
}

/* Appends `length` bytes as base 64. */
static void put_base64(trl_buffer *out, const uint8_t *bytes, size_t length) {
    char *text = (char *)trl_put_space(out, trl_base64_length(length) + 1U);
    if (text != NULL) {
        trl_base64_encode(bytes, length, text);
        out->length -= 1U;
    }
}

/* Writes client-final-message-without-proof: `c=` of the GS2 header and
   the binding's data, `r=` of both nonces. */
static void put_without_proof(const trl_scram *scram, const trl_scram_first *first, trl_buffer *out) {
    trl_buffer input;
    trl_buffer_init(&input);
    put_text(&input, header(scram->channel));
    if (trl_scram_is_bound(scram)) {
        trl_put_raw(&input, scram->binding, scram->binding_length);
    }
    put_text(out, "c=");
    put_base64(out, input.data, input.length);
    if (input.state != TRL_BUFFER_OK) {
        out->state = input.state;
    }
    trl_buffer_free(&input);
    put_text(out, ",r=");
    trl_put_raw(out, first->both.bytes, first->both.length);
}

bool trl_scram_prove(trl_scram *scram, const trl_scram_first *first, const trl_scram_keys *keys, trl_buffer *out) {
    trl_buffer without;
    trl_buffer_init(&without);
    put_without_proof(scram, first, &without);
    trl_buffer auth;
    trl_buffer_init(&auth);
    trl_put_raw(&auth, scram->bare.data, scram->bare.length);
    put_text(&auth, ",");
    trl_put_raw(&auth, first->message.bytes, first->message.length);
    put_text(&auth, ",");
    trl_put_raw(&auth, without.data, without.length);
    uint8_t stored[TRL_SHA256_BYTES] = {0};
    uint8_t proof[TRL_SHA256_BYTES] = {0};
    bool made = without.state == TRL_BUFFER_OK && auth.state == TRL_BUFFER_OK &&
                trl_hash_of(TRL_HASH_SHA256, keys->client, sizeof keys->client, stored) &&
                trl_hmac_sha256(stored, sizeof stored, auth.data, auth.length, proof) &&
                trl_hmac_sha256(keys->server, sizeof keys->server, auth.data, auth.length, scram->expected);
    for (size_t i = 0U; i < sizeof proof; ++i) {
        proof[i] ^= keys->client[i];
    }
    trl_put_raw(out, without.data, without.length);
    put_text(out, ",p=");
    put_base64(out, proof, sizeof proof);
    trl_cleanse(stored, sizeof stored);
    trl_cleanse(proof, sizeof proof);
    trl_buffer_free(&without);
    trl_buffer_free(&auth);
    scram->stage = TRL_STAGE_PROVED;
    return made && out->state == TRL_BUFFER_OK;
}

bool trl_scram_verify(trl_scram *scram, trl_slice message, trl_failure *failure) {
    if (scram->stage != TRL_STAGE_PROVED) {
        return refused(failure, "the server's final message came out of turn");
    }
    if (!trl_utf8_valid(message.bytes, message.length)) {
        return refused(failure, "a message is not UTF-8");
    }
    if (message.length >= TRL_ATTRIBUTE_PREFIX && message.bytes[0] == 'e' && message.bytes[1] == '=') {
        char why[TRL_FAILURE_MESSAGE_BYTES];
        (void)snprintf(why, sizeof why, "the server refused: %.*s", (int)(message.length - TRL_ATTRIBUTE_PREFIX),
                       (const char *)message.bytes + TRL_ATTRIBUTE_PREFIX);
        return refused(failure, why);
    }
    trl_slice text = {NULL, 0U};
    if (!attribute(true, message, 'v', &text, failure)) {
        return false;
    }
    uint8_t *signature = trl_malloc(trl_base64_capacity(text.length));
    if (signature == NULL) {
        trl_failure_no_memory(failure);
        return false;
    }
    size_t length = 0U;
    bool decoded = trl_base64_decode((const char *)text.bytes, text.length, signature, &length);
    bool proved = decoded && length == sizeof scram->expected && trl_same(signature, scram->expected, length);
    free(signature);
    if (!decoded) {
        return refused(failure, "the server's signature is not base 64");
    }
    if (!proved) {
        return refused(failure, "the server did not prove it knows the user");
    }
    scram->stage = TRL_STAGE_VERIFIED;
    return true;
}

bool trl_scram_verified(const trl_scram *scram) {
    return scram->stage == TRL_STAGE_VERIFIED;
}
