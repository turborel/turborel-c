/**
 * @file sync.h
 * @brief What lets several threads share a handle (RULES.md D11): a lock,
 * a wait on it that a thread wakes, and a clock of milliseconds that only
 * moves on; POSIX threads' mutex and condition variable, or Windows' SRW
 * lock and condition variable.
 */
#ifndef TRL_SYNC_H
#define TRL_SYNC_H

#include <stdbool.h>
#include <stdint.h>

/* Each platform declares its locks in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/** @brief A lock. */
typedef SRWLOCK trl_lock;
/** @brief A wait on a lock. */
typedef CONDITION_VARIABLE trl_wait;
#else
#include <pthread.h>
/** @brief A lock. */
typedef pthread_mutex_t trl_lock;
/** @brief A wait on a lock. */
typedef pthread_cond_t trl_wait;
#endif
// NOLINTEND(misc-include-cleaner)

/** @brief Makes a lock; whether it could. */
bool trl_lock_init(trl_lock *lock);

/** @brief Ends a lock no thread holds. */
void trl_lock_destroy(trl_lock *lock);

/** @brief Takes the lock, waiting for it. */
void trl_lock_take(trl_lock *lock);

/** @brief Gives the lock back. */
void trl_lock_give(trl_lock *lock);

/** @brief Makes a wait; whether it could. */
bool trl_wait_init(trl_wait *wait);

/** @brief Ends a wait no thread waits on. */
void trl_wait_destroy(trl_wait *wait);

/**
 * @brief Gives `lock`, held, back and waits to be woken, at most `ms`,
 * then takes it again: false if the time ran out. A thread may wake
 * without being woken, so a waiter checks what it waits for again.
 */
bool trl_wait_for(trl_wait *wait, trl_lock *lock, uint32_t ms);

/** @brief Wakes one thread waiting, if any. */
void trl_wait_wake(trl_wait *wait);

/** @brief Milliseconds of a clock that only moves on, from some time of its own. */
uint64_t trl_now_ms(void);

#endif
