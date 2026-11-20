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

/** @brief Whether a target is TCP without TLS, over which no secret goes. */
bool trl_target_is_plain_tcp(const trl_target *target);

/**
 * @brief Opens a connection to the target, each step within `within_ms`,
 * its reads and writes bounded by it; `channel` gets what a TLS target's
 * connection gives SCRAM to bind. A failure says why (SQLSTATE 08006).
 */
bool trl_target_open(const trl_target *target, uint32_t within_ms, trl_transport *out, trl_tls_channel *channel,
                     trl_failure *failure);

/** @brief A copy of a target, for a canceller; null if memory cannot be had. */
trl_target *trl_target_clone(const trl_target *target);

/** @brief Says why an open, read or write failed, as turborel-client says it. */
void trl_io_failed(trl_failure *failure, const char *what, const trl_io_error *error);

#endif
