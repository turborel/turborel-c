/**
 * @file pool.h
 * @brief A pooled connection's check and reset.
 */
#ifndef TRL_POOL_POOL_H
#define TRL_POOL_POOL_H

#include "turborel/turborel.h"

#include <stdbool.h>

/** @brief Whether `conn` answers: Ping where the server grants it, else ROLLBACK, each wait bounded. */
bool trl_pool_answers(trl_conn *conn);

/**
 * @brief Gives `conn`'s session back as it logged in, by Reset or by
 * statements, then runs `config`'s statements, each wait bounded; whether
 * it could, and so whether the connection may be kept.
 */
bool trl_pool_reset(trl_conn *conn, const trl_pool_config *config);

#endif
