/**
 * @file decfloat.h
 * @brief Decimal floating-point numbers as turborel makes them
 * [PROTOCOL Values].
 */
#ifndef TRL_CODEC_DECFLOAT_H
#define TRL_CODEC_DECFLOAT_H

#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief A decimal floating-point number: coefficient times ten to the exponent. */
typedef struct trl_decfloat {
    /** @brief Its digits, signed, with no trailing zero. */
    trl_int128 coefficient;
    /** @brief The power of ten. */
    int32_t exponent;
} trl_decfloat;

/**
 * @brief The DECFLOAT turborel makes of `value`: rounded to 34 digits,
 * half to even, its trailing zeros taken into its exponent, zero of
 * exponent 0.
 * @param value The number, as read.
 * @param made The number made.
 * @return Nonzero if one is made: its adjusted exponent is within -6143 to
 * 6144 and its exponent a 32-bit integer.
 */
bool trl_decfloat_made(trl_decfloat value, trl_decfloat *made);

/**
 * @brief Whether a DECFLOAT is as the wire carries one: the number
 * trl_decfloat_made() makes of it is itself, as turborel reads one.
 * @param value The number.
 * @return Nonzero if so.
 */
bool trl_decfloat_canonical(trl_decfloat value);

#endif
