/**
 * @file numerals.h
 * @brief Numbers written as turborel writes them in JSON text: an
 * approximate number as its shortest `<approximate numeric literal>`, a
 * DECFLOAT plain.
 */
#ifndef TRL_CODEC_NUMERALS_H
#define TRL_CODEC_NUMERALS_H

#include "codec/buffer.h"
#include "codec/decfloat.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Appends the shortest numeral that reads back as the double of
 * `bits`, as turborel's `approximate_literal`: its digits with a point
 * after the first, `.0` if there is one digit, `E` and the exponent, as
 * "1.5E300", "2.5E-1" or "1.0E0"; zero, either sign, as "0E0".
 * @param buffer Where it goes.
 * @param bits The double's bits.
 * @return Nonzero if written; a double not finite has no numeral.
 */
bool trl_put_approximate_numeral(trl_buffer *buffer, uint64_t bits);

/**
 * @brief Appends a DECFLOAT plain, without an exponent, as turborel's
 * `numeral`: its digits, then zeros for a positive exponent, or a decimal
 * point before its last digits for a negative one, zeros before them where
 * it has fewer.
 * @param buffer Where it goes.
 * @param value The number, canonical.
 */
void trl_put_decfloat_numeral(trl_buffer *buffer, trl_decfloat value);

#endif
