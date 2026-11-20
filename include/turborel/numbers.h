/**
 * @file numbers.h
 * @brief Exact numbers, as the protocol carries them, with their text.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_NUMBERS_H
#define TRL_NUMBERS_H

#include "turborel/base.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A signed 128-bit integer, as two 64-bit halves (RULES.md D3).
 *
 * Its value is `high` times 2^64 plus `low`, two's complement, as the
 * protocol's `i128` carries it [PROTOCOL Values and fields]; MSVC has no
 * 128-bit integer type of its own.
 */
typedef struct trl_int128 {
    /** @brief The low 64 bits. */
    uint64_t low;
    /** @brief The high 64 bits, the sign among them. */
    int64_t high;
} trl_int128;

/**
 * @brief An exact number: `units` divided by ten to the power `scale`
 * [PROTOCOL Values].
 *
 * Every exact number the protocol carries is one, the full range of its
 * units and scale, with nothing lost.
 */
typedef struct trl_decimal {
    /** @brief Its digits, as an integer. */
    trl_int128 units;
    /** @brief How many of them are after the decimal point. */
    uint32_t scale;
} trl_decimal;

/**
 * @brief The bytes of the longest text trl_decimal_text() writes, its null
 * included.
 */
#define TRL_DECIMAL_TEXT_BYTES 64

/**
 * @brief Writes `number` as text, as turborel writes an exact number.
 *
 * The digits of its units, a minus sign before them if it is negative, and
 * a decimal point before its last `scale` digits, with zeros before them
 * where it has fewer, as "-0.050". A scale past 18, which no type of
 * turborel's has, is written as a power of ten instead, as "5E-40".
 *
 * @param number The number.
 * @param out Where the text goes, ended by a null.
 * @param capacity The bytes `out` holds; TRL_DECIMAL_TEXT_BYTES always
 * suffices.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null or `out` is too
 * short, with nothing written.
 */
TRL_API trl_status trl_decimal_text(const trl_decimal *number, char *out, size_t capacity);

/**
 * @brief Reads an exact number from its text, with nothing lost.
 *
 * The text is an optional sign, digits, optionally a decimal point and
 * more digits, and optionally `E` or `e` and a signed power of ten, as
 * "-12.50" or "5E-40", with no spaces. The scale is the digits after the
 * point, less the power; a power that leaves it negative multiplies the
 * units instead.
 *
 * @param text The text, ended by a null.
 * @param out Where the number goes; left as it was unless TRL_OK.
 * @return TRL_OK; TRL_INVALID_ARGUMENT if a pointer is null;
 * TRL_INVALID_TEXT if it is not of that form; TRL_OUT_OF_RANGE if its units
 * pass 128 bits or its scale 32.
 */
TRL_API trl_status trl_decimal_parse(const char *text, trl_decimal *out);

#ifdef __cplusplus
}
#endif

#endif
