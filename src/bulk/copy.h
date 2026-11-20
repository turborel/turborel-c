/**
 * @file copy.h
 * @brief A copy begun with its transaction's end said, as an import's.
 */
#ifndef TRL_BULK_COPY_H
#define TRL_BULK_COPY_H

#include "turborel/turborel.h"

#include <stdbool.h>

/** @brief Begins a copy as trl_copy_in() does, the transaction ended with it if `ends`. */
trl_status trl_copy_begin(trl_conn *conn, const trl_copy_target *target, bool ends, trl_copy **out);

#endif
