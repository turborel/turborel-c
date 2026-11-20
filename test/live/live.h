/**
 * @file live.h
 * @brief What the live tests share: the server started for the run, and
 * ways to reach it.
 */
#ifndef TRL_TEST_LIVE_LIVE_H
#define TRL_TEST_LIVE_LIVE_H

#include "codec/replies.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief The server of the run. */
extern server live_server;

/** @brief A path of the server's directory. */
const char *live_path(const char *name);

/** @brief TLS settings trusting the test authority, presenting ann's certificate if `present`. */
trl_tls *live_tls(bool present);

/** @brief Connects to `target` as ann by `mechanism`, with `options`; null if it failed. */
trl_conn *live_connect(trl_target *target, trl_mechanism *mechanism, const trl_conn_options *options);

/** @brief Connects over TCP, by trust. */
trl_conn *live_trusted(const trl_conn_options *options);

/**
 * @brief Runs a statement by Query, reading its replies to its Complete
 * or Error; `rows` gets the rows it counted, if told.
 */
trl_status live_run(trl_conn *conn, const char *sql, uint64_t *rows);

#endif
