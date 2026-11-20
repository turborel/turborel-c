/**
 * @file text.h
 * @brief A value written as turborel displays it, and an interval's
 * qualifier.
 */
#ifndef TRL_DATA_TEXT_H
#define TRL_DATA_TEXT_H

#include "codec/buffer.h"
#include "codec/values.h"
#include "turborel/turborel.h"

#include <stddef.h>

/** @brief Appends what `value` displays as, as turborel's `Display` writes it. */
void trl_value_put_text(trl_buffer *out, const trl_value *value);

/** @brief Appends an interval's qualifier, as "DAY(2) TO SECOND(6)". */
void trl_value_put_qualifier(trl_buffer *out, const trl_interval_qualifier *qualifier);

/** @brief Appends a data type as SQL writes it, as turborel's `DataType` displays one read from the protocol. */
void trl_type_put_text(trl_buffer *out, const trl_data_type *type);

/**
 * @brief Gives `text` to a caller's `out` of `capacity` bytes, ended by a
 * null and cut short to fit, its whole length in `length` if not null:
 * TRL_OK, or TRL_NO_MEMORY if `text` could not be written.
 */
trl_status trl_text_give(const trl_buffer *text, char *out, size_t capacity, size_t *length);

#endif
