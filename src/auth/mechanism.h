/**
 * @file mechanism.h
 * @brief A mechanism's part in a login [PROTOCOL Opening a connection]:
 * its name and first bytes, its answer to each Challenge, and its check
 * of Ready.
 */
#ifndef TRL_AUTH_MECHANISM_H
#define TRL_AUTH_MECHANISM_H

#include "codec/buffer.h"
#include "turborel/turborel.h"

#include <stdbool.h>

/** @brief Writes the mechanism's first bytes, as Hello carries them. */
bool trl_mechanism_initial(trl_mechanism *mechanism, trl_buffer *out, trl_failure *failure);

/** @brief Writes the mechanism's answer to a Challenge's bytes. */
bool trl_mechanism_answer(trl_mechanism *mechanism, trl_slice challenge, trl_buffer *out, trl_failure *failure);

/** @brief Whether the mechanism sends a secret, never sent over plain TCP. */
bool trl_mechanism_secret(const trl_mechanism *mechanism);

/** @brief Checks the exchange is whole as Ready comes. */
bool trl_mechanism_ready(const trl_mechanism *mechanism, trl_failure *failure);

/** @brief Tells the mechanism the server refused the login. */
void trl_mechanism_refused(trl_mechanism *mechanism);

#endif
