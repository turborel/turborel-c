/**
 * @file failure.h
 * @brief Failures (RULES.md D9): what a call that returned TRL_FAILED
 * came to, of SQLCODE, SQLSTATE and message, as turborel-client's
 * `Failure` has it.
 *
 * Each handle that can fail keeps its last failure, which its own
 * function gives, as trl_conn_failure() does; it is valid until the next
 * call on the same handle, or until the handle is freed. A failure read
 * from the server carries what the server sent of it; one the driver
 * raises carries the SQLCODE and SQLSTATE turborel-client gives the same
 * failure, the rule `DESIGN`, and the severity of its diagnostics.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_FAILURE_H
#define TRL_FAILURE_H

#include "turborel/base.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A failure a handle keeps; read only through its functions. */
typedef struct trl_failure trl_failure;

/**
 * @brief The diagnostics a failure may carry, tags 7 to 15 of the
 * protocol's failure [PROTOCOL Errors and notices], in their order.
 */
typedef enum trl_diagnostic {
    /** @brief ERROR, or FATAL of a failure that ends the connection. */
    TRL_DIAGNOSTIC_SEVERITY = 0,
    /** @brief More of what happened than the message says. */
    TRL_DIAGNOSTIC_DETAIL,
    /** @brief What might be done about it. */
    TRL_DIAGNOSTIC_HINT,
    /** @brief Where it happened, as a routine or a trigger. */
    TRL_DIAGNOSTIC_CONTEXT,
    /** @brief The schema of what it is about. */
    TRL_DIAGNOSTIC_SCHEMA,
    /** @brief The table it is about. */
    TRL_DIAGNOSTIC_TABLE,
    /** @brief The column it is about. */
    TRL_DIAGNOSTIC_COLUMN,
    /** @brief The data type it is about. */
    TRL_DIAGNOSTIC_DATA_TYPE,
    /** @brief The constraint it is about. */
    TRL_DIAGNOSTIC_CONSTRAINT,
} trl_diagnostic;

/**
 * @brief Its SQLCODE, as -919 of a connection lost.
 *
 * @param failure The failure; null gives 0.
 * @return The SQLCODE.
 */
TRL_API int32_t trl_failure_sqlcode(const trl_failure *failure);

/**
 * @brief Its SQLSTATE, five characters, as "08006".
 *
 * @param failure The failure; null gives "".
 * @return A string the failure owns.
 */
TRL_API const char *trl_failure_sqlstate(const trl_failure *failure);

/**
 * @brief Its message, in plain words, the rule it cites apart.
 *
 * @param failure The failure; null gives "".
 * @return A string the failure owns.
 */
TRL_API const char *trl_failure_message(const trl_failure *failure);

/**
 * @brief The rule it cites, as "DESIGN", or null if it cites none.
 *
 * @param failure The failure.
 * @return A string the failure owns, or null.
 */
TRL_API const char *trl_failure_rule(const trl_failure *failure);

/**
 * @brief Its position, in characters into the statement, if it has one.
 *
 * @param failure The failure.
 * @param position Where the position goes, if it has one; may be null.
 * @return Whether it has one.
 */
TRL_API bool trl_failure_position(const trl_failure *failure, uint64_t *position);

/**
 * @brief Whether the session's transaction is still active after it.
 *
 * @param failure The failure; null gives false.
 * @return Whether it is.
 */
TRL_API bool trl_failure_active(const trl_failure *failure);

/**
 * @brief One of its diagnostics, or null if it tells none of that.
 *
 * @param failure The failure.
 * @param which The diagnostic.
 * @return A string the failure owns, or null.
 */
TRL_API const char *trl_failure_diagnostic(const trl_failure *failure, trl_diagnostic which);

/**
 * @brief Whether it ends the connection: of SQLSTATE class 08 but 08003,
 * or of class 28 as a connection is made [PROTOCOL Error 'E'].
 *
 * @param failure The failure; null gives false.
 * @return Whether it does.
 */
TRL_API bool trl_failure_ends_connection(const trl_failure *failure);

/**
 * @brief Writes it as one line of text, as turborel-client displays it:
 * "SQLCODE -919 (SQLSTATE 08006): message [rule] (at character n)".
 *
 * @param failure The failure.
 * @param out Where the text goes, ended by a null, cut short to fit; may
 * be null if `capacity` is 0.
 * @param capacity The bytes `out` holds.
 * @return The bytes of the whole text, its null not counted, as snprintf
 * counts them, so a longer buffer can be had; 0 if `failure` is null.
 */
TRL_API size_t trl_failure_text(const trl_failure *failure, char *out, size_t capacity);

/**
 * @brief Says why a call of the program's own failed, into a failure the
 * driver lends it, as an SQL-agent's connector is lent one
 * (trl_agent_connector): its SQLCODE, SQLSTATE and message, citing no
 * rule, as the server's failures are kept.
 *
 * @param failure The failure lent.
 * @param sqlcode Its SQLCODE, below 0.
 * @param sqlstate Its SQLSTATE, five digits or capital letters.
 * @param message Its message, UTF-8.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer, an SQLCODE not
 * below 0, an SQLSTATE of another form, or a message not UTF-8, nothing
 * changed; TRL_NO_MEMORY, the failure then one of memory not had.
 */
TRL_API trl_status trl_failure_tell(trl_failure *failure, int32_t sqlcode, const char *sqlstate, const char *message);

#ifdef __cplusplus
}
#endif

#endif
