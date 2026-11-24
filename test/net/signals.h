/**
 * @file signals.h
 * @brief A thread that signals another, on POSIX systems: SIGUSR1, of a
 * handler that does nothing, installed without SA_RESTART, so that a
 * read, a write, a connect or a wait the signalled thread is in fails
 * with EINTR, as the client library must bear (turborel's RULES.md D1047).
 */
#ifndef TRL_TEST_NET_SIGNALS_H
#define TRL_TEST_NET_SIGNALS_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

/** @brief How often a signaller signals, in milliseconds. */
#define SIGNALS_EVERY_MS 20U

/** @brief The most signals a signaller sends, so a call that never ends under them still ends. */
#define SIGNALS_AT_MOST 150

/** @brief A thread that signals another until stopped. */
typedef struct signaller {
    /** @brief The thread signalled. */
    pthread_t target;
    /** @brief The signalling thread. */
    pthread_t thread;
    /** @brief Whether it is told to stop. */
    atomic_bool stop;
} signaller;

/** @brief Installs the handler of SIGUSR1, which does nothing, without SA_RESTART. */
bool signals_install(void);

/** @brief Starts signalling `target` with SIGUSR1 every SIGNALS_EVERY_MS. */
bool signaller_start(signaller *s, pthread_t target);

/** @brief Stops signalling, and waits for the signalling thread to end. */
void signaller_stop(signaller *s);

/** @brief How many signals the handler has taken, in all. */
int signals_taken(void);

#endif
