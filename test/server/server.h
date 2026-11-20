/**
 * @file server.h
 * @brief The live tests' server (RULES.md R-TEST-8): turboreld, as the
 * environment variable TRL_TEST_SERVER names it, started on port 17438
 * with a database kept nowhere, its owner ann, TLS of certificates made
 * for the run, a Unix socket, and trusted local clients; stopped by its
 * own process ID.
 */
#ifndef TRL_TEST_SERVER_SERVER_H
#define TRL_TEST_SERVER_SERVER_H

#include <stdbool.h>
#include <stdint.h>

/** @brief The port the live tests' server listens on, this driver's own. */
#define SERVER_PORT 17438U
/** @brief The owner's password. */
#define SERVER_PASSWORD "ann's secret"
/** @brief The longest path the server's files take. */
#define SERVER_PATH_BYTES 512U

/** @brief The server started, and where its files are. */
typedef struct server {
    /** @brief Its process. */
    long pid;
    /** @brief The directory of its certificates, socket and log. */
    char dir[SERVER_PATH_BYTES];
    /** @brief Its Unix socket. */
    char socket[SERVER_PATH_BYTES];
    /** @brief Its database's directory, of a database kept; else empty. */
    char database[SERVER_PATH_BYTES];
} server;

/**
 * @brief Starts the server TRL_TEST_SERVER names in `dir`: a path of
 * turboreld, or a command that runs it, as `docker run ... IMAGE`, the
 * options appended; of a database kept in a directory of its own, its
 * owner added first, if `kept`, else of one kept nowhere. False if the
 * variable is not set, or it did not start; `why` then says which.
 */
bool server_start(server *s, const char *dir, bool kept, const char **why);

/** @brief Stops the server, waiting on its own process ID. */
void server_stop(server *s);

/** @brief A path of the server's directory, `name` in it. */
void server_path(const server *s, const char *name, char *out, uint32_t capacity);

#endif
