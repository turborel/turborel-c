/**
 * @file signals.c
 * @brief A thread that signals another, on POSIX systems.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "signals.h"

/* The platform declares its signals and threads in headers of its own,
   which the include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define NS_PER_MS 1000000L

/* The signals taken, which the handler alone writes. */
static volatile sig_atomic_t taken;

/* Takes a signal, doing nothing more than count it. */
static void take(int number) {
    (void)number;
    taken = taken + 1;
}

bool signals_install(void) {
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = take;
    (void)sigemptyset(&action.sa_mask);
    /* No SA_RESTART: a call the signal interrupts fails with EINTR. */
    action.sa_flags = 0;
    return sigaction(SIGUSR1, &action, NULL) == 0;
}

static void *signal_on(void *context) {
    signaller *s = context;
    struct timespec every = {0, (long)SIGNALS_EVERY_MS * NS_PER_MS};
    for (int sent = 0; sent < SIGNALS_AT_MOST && !atomic_load(&s->stop); ++sent) {
        (void)pthread_kill(s->target, SIGUSR1);
        (void)nanosleep(&every, NULL);
    }
    return NULL;
}

bool signaller_start(signaller *s, pthread_t target) {
    s->target = target;
    atomic_init(&s->stop, false);
    return pthread_create(&s->thread, NULL, signal_on, s) == 0;
}

void signaller_stop(signaller *s) {
    atomic_store(&s->stop, true);
    (void)pthread_join(s->thread, NULL);
}

int signals_taken(void) {
    return (int)taken;
}
// NOLINTEND(misc-include-cleaner)
