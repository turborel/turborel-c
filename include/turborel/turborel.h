/**
 * @file turborel.h
 * @brief libturborel, the C driver for turborel.
 *
 * libturborel connects a C program to a turborel server over turborel's
 * native protocol, version 1, which it speaks itself (RULES.md D1). This
 * header is the whole public interface. Every function is named `trl_`
 * and every macro `TRL_`.
 *
 * This release holds the library's version alone. Connections,
 * statements and their results come in later releases.
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
