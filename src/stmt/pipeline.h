/**
 * @file pipeline.h
 * @brief What a pool's reset adds to a pipeline: a statement's cursor
 * closed and the statement destroyed, by its number.
 */
#ifndef TRL_STMT_PIPELINE_H
#define TRL_STMT_PIPELINE_H

#include "turborel/turborel.h"

#include <stdint.h>

/**
 * @brief Queues the closing of statement `number`'s cursor, then its
 * destruction, each answered by a completion: a pool's reset of what a
 * borrower left.
 */
trl_status trl_pipeline_release(trl_pipeline *pipeline, uint32_t number);

#endif
