/**
 * @file error.h
 * @brief What a codec function that fails says: a SQLCODE, a SQLSTATE and
 * a message (RULES.md D9).
 *
 * A malformed message received fails the connection, SQLCODE -919,
 * SQLSTATE 08006 [PROTOCOL Errors and notices]; memory not had is SQLCODE
 * -918, SQLSTATE TB918, as turborel reports resources it cannot grant.
 */
#ifndef TRL_CODEC_ERROR_H
#define TRL_CODEC_ERROR_H

#include <stddef.h>
#include <stdint.h>

/** @brief The SQLCODE of a connection failure [PROTOCOL Errors and notices]. */
#define TRL_SQLCODE_CONNECTION (-919)
/** @brief The SQLSTATE of a connection failure [PROTOCOL Errors and notices]. */
#define TRL_SQLSTATE_CONNECTION_FAILURE "08006"
/** @brief The SQLCODE of resources not had, as memory, as turborel's. */
#define TRL_SQLCODE_RESOURCES (-918)
/** @brief The SQLSTATE of resources not had, as turborel's. */
#define TRL_SQLSTATE_RESOURCES "TB918"
/** @brief The SQLCODE of a data exception, as turborel's. */
#define TRL_SQLCODE_DATA (-301)
/** @brief The SQLSTATE of a JSON text that is not one, or of an item no JSON text is. */
#define TRL_SQLSTATE_INVALID_JSON_TEXT "22032"
/** @brief The SQLSTATE of a number no type of turborel's holds exactly. */
#define TRL_SQLSTATE_OUT_OF_RANGE "22003"
/** @brief The SQLSTATE of a value the protocol cannot carry. */
#define TRL_SQLSTATE_DATA "22000"
/** @brief The SQLSTATE of text not UTF-8, of characters not in its repertoire. */
#define TRL_SQLSTATE_NOT_UTF8 "22021"
/** @brief The SQLSTATE of an interval's field out of range. */
#define TRL_SQLSTATE_INTERVAL_OVERFLOW "22015"
/** @brief The SQLCODE of a limit passed, as turborel's. */
#define TRL_SQLCODE_UNSUPPORTED (-900)
/** @brief The SQLSTATE of a limit passed, as turborel's. */
#define TRL_SQLSTATE_UNSUPPORTED "0A000"
/** @brief The SQLCODE of a value that is damaged, as turborel's stored data that fails its format. */
#define TRL_SQLCODE_CORRUPT (-916)
/** @brief The SQLSTATE of a value that is damaged, as turborel's. */
#define TRL_SQLSTATE_CORRUPT "TB916"
/** @brief The SQLCODE of a call the program made wrongly, as turborel's. */
#define TRL_SQLCODE_API (-910)
/** @brief The SQLSTATE of a call the program made wrongly, as turborel's. */
#define TRL_SQLSTATE_API "TB910"

/** @brief The bytes of a SQLSTATE: five characters and a null. */
#define TRL_SQLSTATE_BYTES 6U
/** @brief The bytes a codec error's message holds, its null included. */
#define TRL_CODEC_MESSAGE_BYTES 160U

/** @brief A codec function's failure. */
typedef struct trl_codec_error {
    /** @brief Its SQLCODE; 0 while nothing has failed. */
    int32_t sqlcode;
    /** @brief Its SQLSTATE, five characters. */
    char sqlstate[TRL_SQLSTATE_BYTES];
    /** @brief What happened, in plain words. */
    char message[TRL_CODEC_MESSAGE_BYTES];
} trl_codec_error;

/**
 * @brief Records a failure in `error`, unless one is recorded already, so
 * the first cause is the one told.
 *
 * @param error Where to record it; nothing is done if null.
 * @param sqlcode Its SQLCODE.
 * @param sqlstate Its SQLSTATE, five characters.
 * @param message What happened.
 */
void trl_codec_fail(trl_codec_error *error, int32_t sqlcode, const char *sqlstate, const char *message);

/**
 * @brief Records that a message received is malformed: what was being
 * read, the byte it failed at, and why, as a connection failure.
 *
 * @param error Where to record it; nothing is done if null.
 * @param what What was being read, as "reply".
 * @param at The byte of the body it failed at, from 0.
 * @param why Why it is malformed.
 */
void trl_codec_malformed(trl_codec_error *error, const char *what, size_t at, const char *why);

/**
 * @brief Records that memory was not had.
 *
 * @param error Where to record it; nothing is done if null.
 */
void trl_codec_no_memory(trl_codec_error *error);

#endif
