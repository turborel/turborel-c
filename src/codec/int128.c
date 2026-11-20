/**
 * @file int128.c
 * @brief 128-bit integers as two 64-bit halves, by schoolbook arithmetic on
 * 32-bit pieces, which every C17 compiler has (RULES.md D3).
 */
#include "codec/int128.h"

#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/* The decimal radix. */
#define TRL_TEN 10U
/* The bits of a 32-bit piece, and its mask. */
#define TRL_HALF_BITS 32U
#define TRL_HALF_MASK 0xFFFFFFFFU
/* The bits of one byte, and its mask. */
#define TRL_BYTE_BITS 8U
#define TRL_BYTE_MASK 0xFFU
/* The bytes of each 64-bit half. */
#define TRL_HALF_BYTES 8U
/* The top bit of a 64-bit half, the sign of the high one. */
#define TRL_TOP_BIT 0x8000000000000000U

bool trl_int128_negative(trl_int128 value) {
    return value.high < 0;
}

bool trl_uint128_zero(trl_uint128 value) {
    return value.low == 0U && value.high == 0U;
}

/* The two's complement negation of the bits of `value`. */
static trl_uint128 negated(trl_uint128 value) {
    trl_uint128 out = {
        ~value.low + 1U,
        ~value.high,
    };
    if (out.low == 0U) {
        out.high += 1U;
    }
    return out;
}

trl_uint128 trl_int128_magnitude(trl_int128 value) {
    trl_uint128 bits = {value.low, (uint64_t)value.high};
    return trl_int128_negative(value) ? negated(bits) : bits;
}

bool trl_int128_of(trl_uint128 magnitude, bool negative, trl_int128 *out) {
    bool top = (magnitude.high & TRL_TOP_BIT) != 0U;
    if (top && !(negative && magnitude.high == TRL_TOP_BIT && magnitude.low == 0U)) {
        return false;
    }
    trl_uint128 bits = negative ? negated(magnitude) : magnitude;
    out->low = bits.low;
    out->high = trl_bits_i64(bits.high);
    return true;
}

/* The 32-bit pieces of a 128-bit integer, most significant first. */
enum { TRL_PIECE_TOP, TRL_PIECE_UPPER, TRL_PIECE_LOWER, TRL_PIECE_BOTTOM, TRL_PIECES };

/* The pieces of `value`, most significant first. */
static void split(trl_uint128 value, uint64_t pieces[TRL_PIECES]) {
    pieces[TRL_PIECE_TOP] = value.high >> TRL_HALF_BITS;
    pieces[TRL_PIECE_UPPER] = value.high & TRL_HALF_MASK;
    pieces[TRL_PIECE_LOWER] = value.low >> TRL_HALF_BITS;
    pieces[TRL_PIECE_BOTTOM] = value.low & TRL_HALF_MASK;
}

/* The integer of `pieces`, most significant first, each below 2^32. */
static trl_uint128 joined(const uint64_t pieces[TRL_PIECES]) {
    trl_uint128 value = {
        (pieces[TRL_PIECE_LOWER] << TRL_HALF_BITS) | pieces[TRL_PIECE_BOTTOM],
        (pieces[TRL_PIECE_TOP] << TRL_HALF_BITS) | pieces[TRL_PIECE_UPPER],
    };
    return value;
}

unsigned int trl_uint128_divide_ten(trl_uint128 *value) {
    uint64_t pieces[TRL_PIECES];
    split(*value, pieces);
    uint64_t rest = 0U;
    for (unsigned int i = 0U; i < TRL_PIECES; ++i) {
        uint64_t part = (rest << TRL_HALF_BITS) | pieces[i];
        pieces[i] = part / TRL_TEN;
        rest = part % TRL_TEN;
    }
    *value = joined(pieces);
    return (unsigned int)rest;
}

bool trl_uint128_times_ten_plus(trl_uint128 *value, unsigned int digit) {
    uint64_t pieces[TRL_PIECES];
    split(*value, pieces);
    uint64_t carry = digit;
    for (unsigned int i = TRL_PIECES; i > 0U; --i) {
        uint64_t part = (pieces[i - 1U] * TRL_TEN) + carry;
        pieces[i - 1U] = part & TRL_HALF_MASK;
        carry = part >> TRL_HALF_BITS;
    }
    if (carry != 0U) {
        return false;
    }
    *value = joined(pieces);
    return true;
}

unsigned int trl_uint128_digits(trl_uint128 value) {
    unsigned int digits = 1U;
    (void)trl_uint128_divide_ten(&value);
    while (!trl_uint128_zero(value)) {
        ++digits;
        (void)trl_uint128_divide_ten(&value);
    }
    return digits;
}

unsigned int trl_uint128_to_digits(trl_uint128 value, char *out) {
    char reversed[TRL_U128_DIGITS];
    unsigned int count = 0U;
    do {
        reversed[count++] = (char)('0' + trl_uint128_divide_ten(&value));
    } while (!trl_uint128_zero(value) && count < TRL_U128_DIGITS);
    for (unsigned int i = 0U; i < count; ++i) {
        out[i] = reversed[count - 1U - i];
    }
    return count;
}

void trl_int128_to_bytes(trl_int128 value, uint8_t *out) {
    uint64_t high = (uint64_t)value.high;
    for (unsigned int i = 0U; i < TRL_HALF_BYTES; ++i) {
        out[i] = (uint8_t)((value.low >> (i * TRL_BYTE_BITS)) & TRL_BYTE_MASK);
        out[TRL_HALF_BYTES + i] = (uint8_t)((high >> (i * TRL_BYTE_BITS)) & TRL_BYTE_MASK);
    }
}

trl_int128 trl_int128_of_bytes(const uint8_t *bytes) {
    trl_uint128 bits = {0U, 0U};
    for (unsigned int i = 0U; i < TRL_HALF_BYTES; ++i) {
        bits.low |= (uint64_t)bytes[i] << (i * TRL_BYTE_BITS);
        bits.high |= (uint64_t)bytes[TRL_HALF_BYTES + i] << (i * TRL_BYTE_BITS);
    }
    trl_int128 out = {bits.low, trl_bits_i64(bits.high)};
    return out;
}

bool trl_int128_equal(trl_int128 a, trl_int128 b) {
    return a.low == b.low && a.high == b.high;
}

/* Each conversion goes through the unsigned value's range, as converting
   one past the signed maximum is implementation-defined in C17. */
int64_t trl_bits_i64(uint64_t bits) {
    return (bits & TRL_TOP_BIT) != 0U ? -(int64_t)(~bits) - 1 : (int64_t)bits;
}

int32_t trl_bits_i32(uint32_t bits) {
    return bits > (uint32_t)INT32_MAX ? -(int32_t)(~bits) - 1 : (int32_t)bits;
}

int16_t trl_bits_i16(uint16_t bits) {
    return bits > (uint16_t)INT16_MAX ? (int16_t)(-(int32_t)(uint16_t)(~bits) - 1) : (int16_t)bits;
}
