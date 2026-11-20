/**
 * @file faults.h
 * @brief Memory not had, the harness of every fault test (RULES.md
 * R-MEM-2): a scenario run with each allocation failing in turn.
 */
#ifndef TRL_TEST_FAULTS_H
#define TRL_TEST_FAULTS_H

#include "turborel/turborel.h"

#include <stdbool.h>

/** @brief A scenario: whether what it came to is what memory not had allows. */
typedef bool (*scenario)(void);

/** @brief Whether a status is success, or failure of memory or the connection. */
bool allowed(trl_status status, const trl_failure *failure);

/**
 * @brief Runs `run` with each allocation failing in turn until it runs
 * whole; whether every run came to what is allowed.
 */
bool every_fault(scenario run);

#endif
