/**
 * @file int128.h
 * @brief Arithmetic on 128-bit integers held as two 64-bit halves
 * (RULES.md D3): what the exact numbers, DECFLOAT and intervals of the
 * protocol need, and nothing more.
 */
#ifndef TRL_CODEC_INT128_H
#define TRL_CODEC_INT128_H

#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief The bytes of an `i128` on the wire [PROTOCOL Values and fields]. */
#define TRL_I128_BYTES 16U

/** @brief The most decimal digits an unsigned 128-bit integer has. */
#define TRL_U128_DIGITS 39U

/** @brief An unsigned 128-bit integer, a magnitude. */
typedef struct trl_uint128 {
    /** @brief The low 64 bits. */
    uint64_t low;
    /** @brief The high 64 bits. */
    uint64_t high;
} trl_uint128;

/**
 * @brief Whether `value` is below zero.
 * @param value The integer.
 * @return Nonzero if negative.
 */
bool trl_int128_negative(trl_int128 value);

/**
 * @brief Whether `value` is zero.
 * @param value The integer.
 * @return Nonzero if zero.
 */
bool trl_uint128_zero(trl_uint128 value);

/**
 * @brief The magnitude of `value`; that of the least `i128` is 2^127.
 * @param value The integer.
 * @return Its absolute value, unsigned.
 */
trl_uint128 trl_int128_magnitude(trl_int128 value);

/**
 * @brief The signed integer of `magnitude` and a sign.
 * @param magnitude Its absolute value.
 * @param negative Whether it is below zero.
 * @param out The integer.
 * @return Nonzero if it fits an `i128`, which 2^127 does only negative.
 */
bool trl_int128_of(trl_uint128 magnitude, bool negative, trl_int128 *out);

/**
 * @brief Divides `value` by ten in place.
 * @param value The integer, its quotient after.
 * @return The remainder, 0 to 9.
 */
unsigned int trl_uint128_divide_ten(trl_uint128 *value);

/**
 * @brief Divides `value` by `divisor` in place, the quotient kept.
 * @param value The integer.
 * @param divisor The divisor, not 0.
 * @return The remainder.
 */
uint64_t trl_uint128_divide(trl_uint128 *value, uint64_t divisor);

/**
 * @brief Multiplies `value` by ten and adds `digit`, in place.
 * @param value The integer.
 * @param digit 0 to 9.
 * @return Nonzero if the result fits 128 bits; else `value` is unchanged.
 */
bool trl_uint128_times_ten_plus(trl_uint128 *value, unsigned int digit);

/**
 * @brief How many decimal digits `value` has; 1 of zero.
 * @param value The integer.
 * @return Its digits.
 */
unsigned int trl_uint128_digits(trl_uint128 value);

/**
 * @brief Writes the decimal digits of `value`, most significant first,
 * with no null after them.
 * @param value The integer.
 * @param out At least TRL_U128_DIGITS bytes.
 * @return How many digits were written.
 */
unsigned int trl_uint128_to_digits(trl_uint128 value, char *out);

/**
 * @brief Writes `value` as 16 bytes, least significant first.
 * @param value The integer.
 * @param out At least TRL_I128_BYTES bytes.
 */
void trl_int128_to_bytes(trl_int128 value, uint8_t *out);

/**
 * @brief Reads 16 bytes, least significant first, as an integer.
 * @param bytes At least TRL_I128_BYTES bytes.
 * @return The integer.
 */
trl_int128 trl_int128_of_bytes(const uint8_t *bytes);

/**
 * @brief Whether two integers are equal.
 * @param a One.
 * @param b The other.
 * @return Nonzero if equal.
 */
bool trl_int128_equal(trl_int128 a, trl_int128 b);

/**
 * @brief The signed integer whose two's complement bits are `bits`, as a
 * field written `i64 as u64` holds one [PROTOCOL Values and fields].
 * @param bits The bits.
 * @return The integer.
 */
int64_t trl_bits_i64(uint64_t bits);

/**
 * @brief The signed integer whose bits are `bits`, of `i32 as u32`.
 * @param bits The bits.
 * @return The integer.
 */
int32_t trl_bits_i32(uint32_t bits);

/**
 * @brief The signed integer whose bits are the low 16 of `bits`.
 * @param bits The bits.
 * @return The integer.
 */
int16_t trl_bits_i16(uint16_t bits);

#endif
