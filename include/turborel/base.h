/**
 * @file base.h
 * @brief What every part of the interface stands on: the export macro, the
 * version, and the status every call returns (RULES.md D9).
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_BASE_H
#define TRL_BASE_H

/*
 * Symbols are hidden unless marked TRL_API (RULES.md D13): only what the
 * public headers declare is part of the library's interface.
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
    /**
     * @brief It failed, and the handle it was called on keeps the failure,
     * of SQLCODE, SQLSTATE and message, until the next call on the handle.
     */
    TRL_FAILED,
    /**
     * @brief Memory could not be had, where no handle could keep the
     * failure: the handle asked for is not made, or nothing was changed.
     */
    TRL_NO_MEMORY,
} trl_status;

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
