/**
 * @file sync.c
 * @brief Locks, waits and a clock that only moves on, of each platform.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
/* POSIX's clocks and timed waits, which C17 alone does not declare; the
   feature macro is the C library's name, reserved and so spelled. */
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "sync.h"

#include <stdbool.h>
#include <stdint.h>

/* Each platform declares its clocks and waits in headers of its own,
   which the include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#ifndef _WIN32
#include <errno.h>
#include <time.h>
#endif

/* A second's milliseconds and nanoseconds, and a millisecond's. */
#define TRL_MS_SECOND 1000U
#define TRL_NS_SECOND 1000000000L
#define TRL_NS_MS 1000000L

#ifdef _WIN32

bool trl_lock_init(trl_lock *lock) {
    InitializeSRWLock(lock);
    return true;
}

void trl_lock_destroy(trl_lock *lock) {
    (void)lock;
}

void trl_lock_take(trl_lock *lock) {
    AcquireSRWLockExclusive(lock);
}

void trl_lock_give(trl_lock *lock) {
    ReleaseSRWLockExclusive(lock);
}

bool trl_wait_init(trl_wait *wait) {
    InitializeConditionVariable(wait);
    return true;
}

void trl_wait_destroy(trl_wait *wait) {
    (void)wait;
}

bool trl_wait_for(trl_wait *wait, trl_lock *lock, uint32_t ms) {
    return SleepConditionVariableSRW(wait, lock, (DWORD)ms, 0U) != 0;
}

void trl_wait_wake(trl_wait *wait) {
    WakeConditionVariable(wait);
}

uint64_t trl_now_ms(void) {
    return (uint64_t)GetTickCount64();
}

#else

bool trl_lock_init(trl_lock *lock) {
    return pthread_mutex_init(lock, NULL) == 0;
}

void trl_lock_destroy(trl_lock *lock) {
    (void)pthread_mutex_destroy(lock);
}

void trl_lock_take(trl_lock *lock) {
    (void)pthread_mutex_lock(lock);
}

void trl_lock_give(trl_lock *lock) {
    (void)pthread_mutex_unlock(lock);
}

#ifdef __APPLE__

bool trl_wait_init(trl_wait *wait) {
    return pthread_cond_init(wait, NULL) == 0;
}

bool trl_wait_for(trl_wait *wait, trl_lock *lock, uint32_t ms) {
    struct timespec within = {(time_t)(ms / TRL_MS_SECOND), (long)(ms % TRL_MS_SECOND) * TRL_NS_MS};
    return pthread_cond_timedwait_relative_np(wait, lock, &within) != ETIMEDOUT;
}

#else

bool trl_wait_init(trl_wait *wait) {
    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes) != 0) {
        return false;
    }
    bool made =
        pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) == 0 && pthread_cond_init(wait, &attributes) == 0;
    (void)pthread_condattr_destroy(&attributes);
    return made;
}

bool trl_wait_for(trl_wait *wait, trl_lock *lock, uint32_t ms) {
    struct timespec until;
    (void)clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec += (time_t)(ms / TRL_MS_SECOND);
    until.tv_nsec += (long)(ms % TRL_MS_SECOND) * TRL_NS_MS;
    if (until.tv_nsec >= TRL_NS_SECOND) {
        until.tv_nsec -= TRL_NS_SECOND;
        ++until.tv_sec;
    }
    return pthread_cond_timedwait(wait, lock, &until) != ETIMEDOUT;
}

#endif

void trl_wait_destroy(trl_wait *wait) {
    (void)pthread_cond_destroy(wait);
}

void trl_wait_wake(trl_wait *wait) {
    (void)pthread_cond_signal(wait);
}

uint64_t trl_now_ms(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return ((uint64_t)now.tv_sec * TRL_MS_SECOND) + (uint64_t)(now.tv_nsec / TRL_NS_MS);
}

#endif
// NOLINTEND(misc-include-cleaner)
