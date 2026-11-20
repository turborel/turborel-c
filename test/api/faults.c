/**
 * @file faults.c
 * @brief Memory not had, the harness of every fault test.
 */
#include "faults.h"

#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* The most allocations a scenario is failed at. */
#define MOST_FAULTS 4000L

/* Whether a status is success, or failure of memory or the connection. */
bool allowed(trl_status status, const trl_failure *failure) {
    if (status == TRL_OK || status == TRL_NO_MEMORY) {
        return true;
    }
    int32_t sqlcode = trl_failure_sqlcode(failure);
    return status == TRL_FAILED && (sqlcode == -918 || sqlcode == -919);
}

/* Runs `run` with each allocation failing in turn until it runs whole;
   whether every run came to what is allowed. */
bool every_fault(scenario run) {
    for (long allowed_count = 0L; allowed_count < MOST_FAULTS; ++allowed_count) {
        trl_faults_arm(allowed_count);
        bool fine = run();
        bool fired = trl_faults_fired();
        trl_faults_arm(-1L);
        if (!fine) {
            (void)fprintf(stderr, "the run failing allocation %ld came to what is not allowed\n", allowed_count);
            return false;
        }
        if (!fired) {
            return true;
        }
    }
    return false;
}
