/**
 * @file lob.h
 * @brief Large objects' requests over a connection, and what a reader and
 * a writer share.
 */
#ifndef TRL_LOB_LOB_H
#define TRL_LOB_LOB_H

#include "codec/requests.h"
#include "codec/values.h"
#include "conn/conn.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Sends a Lob request and reads its Lob reply into `out`, whose data the connection's arena holds. */
bool trl_lob_ask(trl_conn *conn, const trl_lob_request *request, trl_reply *out);

/** @brief Sends a Lob request and reads its Complete. */
bool trl_lob_completed(trl_conn *conn, const trl_lob_request *request);

/** @brief Fails the connection's call as a large object misused (SQLCODE -301, SQLSTATE 22000). */
void trl_lob_misused(trl_conn *conn, const char *message);

/** @brief Copies the range `range` of a large object held whole, `bytes`, to `out`; its length, as trl_lob_read() says.
 */
void trl_lob_held_range(const trl_value *lob, trl_lob_range range, uint8_t *out, size_t *length);

#endif
