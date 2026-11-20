/**
 * @file settings_common.h
 * @brief What the settings tests share: an environment of their own,
 * scratch files, and checks of settings laid.
 */
#ifndef TRL_TEST_SETTINGS_COMMON_H
#define TRL_TEST_SETTINGS_COMMON_H

#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Room for a path. */
#define PATH_BYTES 512U
/** @brief The most variables a test's environment holds. */
#define MOST_VARIABLES 12U

/** @brief An environment of the test's own: names and values. */
typedef struct environment {
    /** @brief The variables' names. */
    const char *names[MOST_VARIABLES];
    /** @brief Their values. */
    const char *values[MOST_VARIABLES];
    /** @brief How many. */
    size_t count;
} environment;

/** @brief Reads a variable of `context`, an environment. */
const char *variable(void *context, const char *name);

/** @brief Writes a scratch file of `text`, named for `test`; its path into `path`, of PATH_BYTES. */
bool scratch(const char *test, const char *text, char *path);

/** @brief The settings `options` give, laid in `env`. */
trl_settings *laid(const trl_options *options, environment *env);

/** @brief Whether host `i` of `settings` is `name` at `port`, local if `local`. */
bool host_is(const trl_settings *settings, size_t i, const char *name, uint16_t port, bool local);

/** @brief Whether `failure` is SQLSTATE 08001, its message holding `part`. */
bool unable(const trl_failure *failure, const char *part);

/** @brief Whether the password file `text` gives `expected` to `user` at `host` and `port`, or none if null. */
bool password_is(const char *text, const char *host, const char *port, const char *user, const char *expected);

#endif
