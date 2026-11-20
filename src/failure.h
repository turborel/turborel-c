/**
 * @file failure.h
 * @brief The failure a handle keeps (RULES.md D9), and how it is set: by
 * the driver, from the codec, or from the server's Error.
 */
#ifndef TRL_FAILURE_INTERNAL_H
#define TRL_FAILURE_INTERNAL_H

#include "codec/error.h"
#include "codec/fields.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief The rule the driver's own failures cite, as turborel-client's do. */
#define TRL_RULE_DESIGN "DESIGN"
/** @brief The SQLSTATE of a login refused, or a secret refused over plain TCP. */
#define TRL_SQLSTATE_INVALID_AUTHORIZATION "28000"
/** @brief The longest message the driver writes of its own, its null included. */
#define TRL_FAILURE_MESSAGE_BYTES 512U

/** @brief A failure; every string is null-ended. */
struct trl_failure {
    /** @brief Its SQLCODE; 0 while nothing has failed. */
    int32_t sqlcode;
    /** @brief Its SQLSTATE. */
    char sqlstate[TRL_SQLSTATE_BYTES];
    /** @brief Its message; never null. */
    const char *message;
    /** @brief The rule it cites, or null. */
    const char *rule;
    /** @brief Whether its position is known. */
    bool placed;
    /** @brief Its position, in characters into the statement. */
    uint64_t position;
    /** @brief Whether the session's transaction is active after it. */
    bool active;
    /** @brief Each diagnostic told, or null. */
    const char *diagnostics[TRL_DIAGNOSTICS];
    /** @brief The block its strings are kept in, which it owns, or null. */
    char *block;
};

/** @brief Makes `failure` empty: no failure. */
void trl_failure_init(trl_failure *failure);

/** @brief Frees what `failure` holds, and makes it empty. */
void trl_failure_clear(trl_failure *failure);

/**
 * @brief Sets a failure the driver raises: of `sqlcode`, `sqlstate` and
 * `message`, citing the rule DESIGN, its severity FATAL if it ends the
 * connection, else ERROR, as turborel-client's are.
 */
void trl_failure_set(trl_failure *failure, int32_t sqlcode, const char *sqlstate, const char *message);

/** @brief Sets a failure as trl_failure_set() does, citing `rule` rather than DESIGN. */
void trl_failure_cite(trl_failure *failure, int32_t sqlcode, const char *sqlstate, const char *message,
                      const char *rule);

/** @brief Sets a connection failure (SQLCODE -919, SQLSTATE 08006) of `message`. */
void trl_failure_broken(trl_failure *failure, const char *message);

/** @brief Sets a failure of memory not had (SQLCODE -918, SQLSTATE TB918). */
void trl_failure_no_memory(trl_failure *failure);

/** @brief Sets the failure the codec recorded. */
void trl_failure_from_codec(trl_failure *failure, const trl_codec_error *error);

/** @brief Sets the failure the server sent, all of it. */
void trl_failure_from_wire(trl_failure *failure, const trl_wire_failure *wire);

/** @brief Sets `failure` to a copy of `from`. */
void trl_failure_copy(trl_failure *failure, const trl_failure *from);

#endif
