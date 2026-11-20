/**
 * @file target.h
 * @brief Where a server is, and how a connection to it is opened
 * [CLIENT Connecting].
 */
#ifndef TRL_CONN_TARGET_H
#define TRL_CONN_TARGET_H

#include "net/tls.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Opens a connection to the target, each step within `within_ms`,
 * its reads and writes bounded by it; `channel` gets what a TLS target's
 * connection gives SCRAM to bind. A failure says why (SQLSTATE 08006).
 */
bool trl_target_open(const trl_target *target, uint32_t within_ms, trl_transport *out, trl_tls_channel *channel,
                     trl_failure *failure);

/** @brief A copy of a target, for a canceller; null if memory cannot be had. */
trl_target *trl_target_clone(const trl_target *target);

/** @brief What an opening under `prefer` came to. */
typedef enum trl_opening {
    /** @brief Not opened, the failure saying why. */
    TRL_NOT_OPENED = 0,
    /** @brief Opened as the target says. */
    TRL_OPENED,
    /** @brief Opened over plain TCP, TLS's handshake having failed. */
    TRL_OPENED_PLAIN,
} trl_opening;

/**
 * @brief Opens a target as trl_target_open() does, but of a TLS target
 * whose server is reached and whose handshake fails, connects to the same
 * address again over plain TCP, as `prefer` does; a server that cannot be
 * reached is not tried twice [CLIENT TLS by mode].
 */
trl_opening trl_target_open_preferring(const trl_target *target, uint32_t within_ms, trl_transport *out,
                                       trl_tls_channel *channel, trl_failure *failure);

/**
 * @brief Of a target over TCP or TLS, the address `transport` reached in
 * place of its host, as turborel-client keeps the address a connection
 * reached, so a canceller reaches the same server however the host's name
 * resolves after; a TLS target's server name is kept, the certificate
 * checked against it. Whether it could; a target of another kind, or a
 * transport that tells no address, is as it was.
 */
bool trl_target_reached(trl_target *target, const trl_transport *transport);

/** @brief Says why an open, read or write failed, as turborel-client says it. */
void trl_io_failed(trl_failure *failure, const char *what, const trl_io_error *error);

#endif
