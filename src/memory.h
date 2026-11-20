/**
 * @file memory.h
 * @brief The library's allocations, each checked by its caller (RULES.md
 * R-MEM-2). In a build of TRL_FAULTS, for the tests alone, an allocation
 * may be made to fail on purpose, so every path that handles memory not
 * had is run (RULES.md D14).
 */
#ifndef TRL_MEMORY_H
#define TRL_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

/** @brief malloc, or null as a fault armed says. */
void *trl_malloc(size_t size);

/** @brief calloc, or null as a fault armed says. */
void *trl_calloc(size_t count, size_t size);

/** @brief realloc, or null, `block` kept, as a fault armed says. */
void *trl_realloc(void *block, size_t size);

#ifdef TRL_FAULTS
/**
 * @brief Arms a fault on this thread: the allocation after `allowed` more
 * fails, and every one after it; a negative `allowed` disarms it.
 */
void trl_faults_arm(long allowed);

/**
 * @brief Holds the fault armed, for what a test does around the code it
 * tests: the allocations allowed still, which trl_faults_resume() takes.
 */
long trl_faults_suspend(void);

/** @brief Arms again the fault held, its firing kept. */
void trl_faults_resume(long allowed);

/** @brief Whether an armed fault failed an allocation since it was armed. */
bool trl_faults_fired(void);

/**
 * @brief Whether the next allocation fails, as the fault armed says,
 * counting it; for a test that routes the TLS library's allocations here.
 */
bool trl_faults_take(void);
#endif

#endif
