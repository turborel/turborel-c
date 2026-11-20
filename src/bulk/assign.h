/**
 * @file assign.h
 * @brief A source's value as an import sends it for its column.
 */
#ifndef TRL_BULK_ASSIGN_H
#define TRL_BULK_ASSIGN_H

#include "codec/types.h"
#include "codec/values.h"
#include "failure.h"
#include "turborel/turborel.h"

/**
 * @brief Adds `value` to `out` as it is sent for a parameter of `type`: a
 * character string for a character string type assigned as INSERT
 * assigns, a long one for a large object type sent ahead by its locator
 * over `conn`, any other as it is; TRL_FAILED with `failure` set.
 */
trl_status trl_import_value(trl_conn *conn, const trl_value *value, const trl_data_type *type, trl_params *out,
                            trl_failure *failure);

#endif
