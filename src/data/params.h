/**
 * @file params.h
 * @brief Parameters as an Execute carries them, and values copied whole.
 */
#ifndef TRL_DATA_PARAMS_H
#define TRL_DATA_PARAMS_H

#include "codec/arena.h"
#include "codec/fields.h"
#include "codec/types.h"
#include "codec/values.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The list's values, in order; trl_params_count() of them. */
const trl_value *trl_params_values(const trl_params *params);

/**
 * @brief Copies `from` into `out`, its strings, bytes and nested values
 * into `arena`, `depth` deep of the protocol's nesting at most.
 */
bool trl_value_copy(trl_arena *arena, const trl_value *from, trl_value *out, unsigned int depth);

/**
 * @brief Copies a type into `arena`, its names and the types nested in it,
 * `depth` deep of the protocol's nesting at most.
 */
bool trl_type_copy(trl_arena *arena, const trl_data_type *from, trl_data_type *out, unsigned int depth);

/** @brief Copies `count` columns into `arena`, their names and types; null if memory cannot be had. */
trl_column *trl_columns_copy(trl_arena *arena, const trl_column *from, uint32_t count);

#endif
