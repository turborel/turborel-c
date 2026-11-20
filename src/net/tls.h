/**
 * @file tls.h
 * @brief TLS over a transport, of the TLS library (RULES.md D12): the
 * settings a target shares, the handshake, and the channel SCRAM binds.
 */
#ifndef TRL_NET_TLS_H
#define TRL_NET_TLS_H

#include "auth/scram.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief What a TLS connection gives SCRAM to bind [PROTOCOL Mechanisms]. */
typedef struct trl_tls_channel {
    /** @brief Whether the connection is over TLS, and so binds. */
    bool secured;
    /** @brief The certificate the server presented, DER; its own, or null. */
    uint8_t *certificate;
    /** @brief Its bytes. */
    size_t certificate_length;
    /** @brief Whether the exporter was had, of TLS 1.3 alone. */
    bool has_exporter;
    /** @brief The bytes exported under TRL_SCRAM_EXPORTER_LABEL. */
    uint8_t exporter[TRL_SCRAM_EXPORTER_BYTES];
} trl_tls_channel;

/**
 * @brief A hold of the settings' context, which a target keeps; the
 * settings change no more after. Null, with `failure` set, if they cannot
 * be used: built without TLS, failed, or trusting no authority.
 */
void *trl_tls_hold(trl_tls *tls, trl_failure *failure);

/**
 * @brief Keeps the settings to TLS 1.2 at most, as a server of TLS 1.2
 * alone would; for the tests of SCRAM's binding over TLS 1.2.
 */
bool trl_tls_at_most_tls12(trl_tls *tls);

/** @brief Another hold of a context held; null if it cannot be had. */
void *trl_tls_context_hold(void *context);

/** @brief Gives back a hold of a context. */
void trl_tls_release(void *context);

/**
 * @brief Begins TLS over `transport`, the server's certificate checked
 * against `name`; `transport` is then TLS over what it was, and `channel`
 * gets what SCRAM binds. On failure it is closed and `failure` says why
 * (SQLSTATE 08006).
 */
bool trl_tls_secure(void *context, const char *name, trl_transport *transport, trl_tls_channel *channel,
                    trl_failure *failure);

/** @brief Frees what a channel holds. */
void trl_tls_channel_free(trl_tls_channel *channel);

#endif
