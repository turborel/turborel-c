/**
 * @file tail.h
 * @brief A message's tail: the fields version 1 gained after its first
 * form, each a tag and its field, in increasing order of tag, to the end of
 * the body [PROTOCOL The tail].
 */
#ifndef TRL_CODEC_TAIL_H
#define TRL_CODEC_TAIL_H

#include "codec/buffer.h"
#include "codec/reader.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Tag 1: attributes, of Hello, Query and Execute. */
#define TRL_TAIL_ATTRIBUTES 1U
/** @brief Tag 2: multi, of Query. */
#define TRL_TAIL_MULTI 2U
/** @brief Tag 3: cursor, of Execute, Fetch and Close. */
#define TRL_TAIL_CURSOR 3U
/** @brief Tag 4: position, of Complete. */
#define TRL_TAIL_POSITION 4U
/** @brief Tag 5: more, of Complete. */
#define TRL_TAIL_MORE 5U
/** @brief Tag 6: rows, of Complete. */
#define TRL_TAIL_ROWS 6U
/** @brief Tag 7: severity, the first of a failure's and a notice's diagnostics. */
#define TRL_TAIL_SEVERITY 7U
/** @brief Tag 15: constraint, the last of the diagnostics. */
#define TRL_TAIL_CONSTRAINT 15U
/** @brief Tag 16: generated, of Complete. */
#define TRL_TAIL_GENERATED 16U
/** @brief Tag 17: conditions, of Complete. */
#define TRL_TAIL_CONDITIONS 17U
/** @brief Tag 18: time, of Complete. */
#define TRL_TAIL_TIME 18U
/** @brief Tag 19: declared, of Prepared and Description. */
#define TRL_TAIL_DECLARED 19U

/** @brief The most attributes a message takes [PROTOCOL The tail]. */
#define TRL_MAX_ATTRIBUTES 32U
/** @brief The longest name of an attribute, in bytes. */
#define TRL_MAX_ATTRIBUTE_NAME 64U
/** @brief The longest value of an attribute, in bytes. */
#define TRL_MAX_ATTRIBUTE_VALUE 1024U

/** @brief The bit of a tag in a set of tags a message takes. */
#define TRL_TAG_BIT(tag) (UINT32_C(1) << (tag))

/** @brief A tail being read: the last tag read. */
typedef struct trl_tail {
    /** @brief The last tag read; 0 before the first. */
    uint8_t last;
} trl_tail;

/** @brief An attribute: a name and a value. */
typedef struct trl_attribute {
    /** @brief Its name, 1 to 64 bytes. */
    trl_slice name;
    /** @brief Its value, at most 1024 bytes. */
    trl_slice value;
} trl_attribute;

/**
 * @brief Reads the next tag of a tail: one past the last, among `taken`.
 * @param reader The reader.
 * @param tail The tail.
 * @param taken The tags the message takes, each TRL_TAG_BIT(tag).
 * @param out The tag; 0 at the end of the body.
 * @return Nonzero if read; else a tag out of place failed the reader.
 */
bool trl_tail_next(trl_reader *reader, trl_tail *tail, uint32_t taken, uint8_t *out);

/**
 * @brief Reads attributes: a count, at most 32, then each name and value.
 * @param reader The reader.
 * @param out The attributes, from the reader's arena.
 * @param count How many.
 * @return Nonzero if read.
 */
bool trl_read_attributes(trl_reader *reader, trl_attribute **out, uint32_t *count);

/**
 * @brief Writes a tail's attributes, unless there are none, checking
 * their bounds.
 * @param buffer The buffer.
 * @param attributes The attributes.
 * @param count How many.
 * @param error Where attributes out of bounds are recorded.
 * @return Nonzero if written.
 */
bool trl_write_attributes(trl_buffer *buffer, const trl_attribute *attributes, size_t count, trl_codec_error *error);

#endif
