/**
 * @file memory.c
 * @brief The library's allocations; with TRL_FAULTS, those a test makes
 * fail. The fault's count is a thread's own, and exists in that build
 * alone, which no package ships (RULES.md D14, R-SAFE-5).
 */
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef TRL_FAULTS

/* A thread's own storage: MSVC's C names it its own way. */
#ifdef _MSC_VER
#define TRL_THREAD_LOCAL __declspec(thread)
#else
#define TRL_THREAD_LOCAL _Thread_local
#endif

/* The allocations allowed before the fault, negative while none is armed;
   and whether it fired. */
static TRL_THREAD_LOCAL long faults_allowed = -1;
static TRL_THREAD_LOCAL bool faults_fired = false;

void trl_faults_arm(long allowed) {
    faults_allowed = allowed;
    faults_fired = false;
}

long trl_faults_suspend(void) {
    long allowed = faults_allowed;
    faults_allowed = -1;
    return allowed;
}

void trl_faults_resume(long allowed) {
    faults_allowed = allowed;
}

bool trl_faults_fired(void) {
    return faults_fired;
}

/* Whether this allocation fails, as the fault armed says. */
static bool faulted(void) {
    if (faults_allowed < 0) {
        return false;
    }
    if (faults_allowed == 0) {
        faults_fired = true;
        return true;
    }
    --faults_allowed;
    return false;
}

bool trl_faults_take(void) {
    return faulted();
}

#else

/* With no fault built, every allocation is the system's. */
static bool faulted(void) {
    return false;
}

#endif

void trl_free_const(const void *block) {
    union {
        const void *read;
        void *owned;
    } pointer = {block};
    free(pointer.owned);
}

void *trl_malloc(size_t size) {
    return faulted() ? NULL : malloc(size);
}

void *trl_calloc(size_t count, size_t size) {
    return faulted() ? NULL : calloc(count, size);
}

void *trl_realloc(void *block, size_t size) {
    return faulted() ? NULL : realloc(block, size);
}
