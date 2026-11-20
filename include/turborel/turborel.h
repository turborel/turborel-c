/**
 * @file turborel.h
 * @brief libturborel, the C driver for turborel.
 *
 * libturborel connects a C program to a turborel server over turborel's
 * native protocol, version 1, which it speaks itself (RULES.md D1). This
 * header is the whole public interface. Every function is named `trl_`
 * and every macro `TRL_`.
 *
 * This release holds the library's version and its exact numbers.
 * Connections, statements and their results come in later releases.
 *
 * @par Version
 * The library's version is the turborel release it is made for, the same
 * number as the server's (VERSIONING.md). The alpha is 0.0.0, and speaks
 * protocol version 1.
 *
 * @par Thread safety
 * The library keeps no global mutable state. Every function in this
 * header may be called from any thread, at any time, concurrently.
 *
 * @par Linking
 * A program linking the static library defines `TRL_STATIC` before it
 * includes this header; the CMake package and the pkg-config file define
 * it for the program.
 */
#ifndef TRL_TURBOREL_H
#define TRL_TURBOREL_H

/*
 * Symbols are hidden unless marked TRL_API (RULES.md D13): only what this
 * header declares is part of the library's interface.
 */
#ifdef TRL_STATIC
/**
 * @brief Marks a function as part of the library's interface.
 *
 * Empty in a static library, `__declspec(dllexport)` or
 * `__declspec(dllimport)` in a Windows DLL, as it is built or used, and
 * the default visibility elsewhere, as symbols are hidden otherwise.
 */
#define TRL_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#ifdef TRL_BUILDING
#define TRL_API __declspec(dllexport)
#else
#define TRL_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define TRL_API __attribute__((visibility("default")))
#else
#define TRL_API
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The major part of the version this header declares. */
#define TRL_VERSION_MAJOR 0
/** @brief The minor part of the version this header declares. */
#define TRL_VERSION_MINOR 0
/** @brief The patch part of the version this header declares. */
#define TRL_VERSION_PATCH 0
/**
 * @brief The version this header declares, as text.
 *
 * It is the turborel release the library is made for. Compare it with
 * trl_version() to find a header and a library of different releases.
 */
#define TRL_VERSION_STRING "0.0.0"
/** @brief The native protocol's version the library speaks. */
#define TRL_PROTOCOL_VERSION 1

/**
 * @brief What a call came to (RULES.md D9): TRL_OK, or why it failed.
 */
typedef enum trl_status {
    /** @brief It did what was asked. */
    TRL_OK = 0,
    /** @brief A pointer it needs was null, or a buffer given is too short. */
    TRL_INVALID_ARGUMENT,
    /** @brief The text given is not of the form asked for. */
    TRL_INVALID_TEXT,
    /** @brief The value does not fit the type asked for. */
    TRL_OUT_OF_RANGE,
} trl_status;

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

/**
 * @brief The version of the library linked, as text, such as "0.0.0".
 *
 * @return A string the library owns, valid for as long as the program
 * runs; it is never freed.
 */
TRL_API const char *trl_version(void);

#ifdef __cplusplus
}
#endif

#endif
