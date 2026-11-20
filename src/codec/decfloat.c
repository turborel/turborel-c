/**
 * @file decfloat.c
 * @brief Whether a DECFLOAT read is one turborel would make: rounded to its
 * 34 digits, half to even, its trailing zeros taken into its exponent, and
 * its adjusted exponent within decimal128's, as turborel's `DecFloat::new`
 * makes one [PROTOCOL Values].
 */
#include "codec/decfloat.h"

#include "codec/int128.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/* The digits a DECFLOAT holds, turborel's MAX_DECFLOAT_PRECISION. */
#define TRL_DECFLOAT_DIGITS 34U
/* The least and greatest adjusted exponents, decimal128's. */
#define TRL_DECFLOAT_MIN_ADJUSTED (-6143)
#define TRL_DECFLOAT_MAX_ADJUSTED 6144
/* A dropped digit at which rounding goes up, or to even. */
#define TRL_HALF 5U

/* Adds one to `value`; it is below 10^34, so no carry leaves it. */
static void add_one(trl_uint128 *value) {
    value->low += 1U;
    value->high += (uint64_t)(value->low == 0U);
}

/* Rounds `magnitude` to 34 digits, half to even, taking each digit dropped
   into `exponent`. */
static void round_to_digits(trl_uint128 *magnitude, int64_t *exponent) {
    unsigned int digits = trl_uint128_digits(*magnitude);
    unsigned int dropped = 0U;
    bool sticky = false;
    for (unsigned int i = TRL_DECFLOAT_DIGITS; i < digits; ++i) {
        sticky = sticky || dropped != 0U;
        dropped = trl_uint128_divide_ten(magnitude);
        *exponent += 1;
    }
    bool odd = (magnitude->low & 1U) != 0U;
    if (dropped > TRL_HALF || (dropped == TRL_HALF && (sticky || odd))) {
        add_one(magnitude);
    }
}

bool trl_decfloat_made(trl_decfloat value, trl_decfloat *made) {
    trl_uint128 magnitude = trl_int128_magnitude(value.coefficient);
    int64_t exponent = value.exponent;
    if (trl_uint128_zero(magnitude)) {
        made->coefficient = value.coefficient;
        made->exponent = 0;
        return true;
    }
    round_to_digits(&magnitude, &exponent);
    trl_uint128 quotient = magnitude;
    while (trl_uint128_divide_ten(&quotient) == 0U) {
        magnitude = quotient;
        exponent += 1;
        quotient = magnitude;
    }
    int64_t adjusted = exponent + (int64_t)trl_uint128_digits(magnitude) - 1;
    if (exponent > INT32_MAX || exponent < INT32_MIN || adjusted < TRL_DECFLOAT_MIN_ADJUSTED ||
        adjusted > TRL_DECFLOAT_MAX_ADJUSTED) {
        return false;
    }
    made->exponent = (int32_t)exponent;
    return trl_int128_of(magnitude, trl_int128_negative(value.coefficient), &made->coefficient);
}

bool trl_decfloat_canonical(trl_decfloat value) {
    trl_decfloat made = {{0U, 0}, 0};
    return trl_decfloat_made(value, &made) && trl_int128_equal(made.coefficient, value.coefficient) &&
           made.exponent == value.exponent;
}
