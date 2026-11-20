/**
 * @file values.h
 * @brief A value, as a row, a parameter or a copy carries it: its tag, then
 * its body [PROTOCOL Values], read from a reply or written into a request.
 *
 * Strings, bytes and names are views: of a value read, into the body it
 * was read from; of a value written, into the caller's memory.
 */
#ifndef TRL_CODEC_VALUES_H
#define TRL_CODEC_VALUES_H

#include "codec/buffer.h"
#include "codec/decfloat.h"
#include "codec/reader.h"
#include "codec/types.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief A date, a time or a timestamp's body [PROTOCOL Values]. */
typedef struct trl_datetime {
    /** @brief Whether it is of a type WITH TIME ZONE. */
    bool zoned;
    /** @brief Its fractional seconds' precision. */
    uint8_t precision;
    /** @brief The zone's displacement from UTC, in minutes. */
    int16_t zone;
    /**
     * @brief The instant, microseconds in UTC from 0001-01-01 00:00:00: of a
     * date, its midnight's; of a time, from midnight, within a day.
     */
    int64_t utc;
} trl_datetime;

/** @brief An interval's body: its qualifier, then its units [PROTOCOL Values]. */
typedef struct trl_interval {
    /** @brief Its qualifier, as an INTERVAL type writes it. */
    trl_interval_qualifier qualifier;
    /** @brief Months of a year-month interval, microseconds of a day-time one. */
    trl_int128 units;
} trl_interval;

/** @brief A value. */
typedef struct trl_value trl_value;

/** @brief A value, by its tag (`tags.h`). */
struct trl_value {
    /** @brief Its tag. */
    uint8_t tag;
    /** @brief Its body. */
    union {
        /**
         * @brief A character string, binary string, reference, JSON
         * stored form, XML serialization, or UUID's octets.
         */
        trl_slice bytes;
        /** @brief An exact number. */
        trl_decimal exact;
        /** @brief An approximate number, the bits of its double, kept as they are. */
        uint64_t approximate;
        /** @brief A plug-in's value: its type's name, its text, its bytes. */
        struct {
            trl_slice type_name;
            trl_slice text;
            trl_slice bytes;
        } plugin;
        /** @brief A date, time or timestamp. */
        trl_datetime datetime;
        /** @brief An interval. */
        trl_interval interval;
        /** @brief A truth value. */
        bool truth;
        /** @brief A DECFLOAT. */
        trl_decfloat decfloat;
        /** @brief An array's elements, a row's fields, or a multiset's elements. */
        struct {
            trl_value *items;
            uint32_t count;
        } list;
        /** @brief A structured value: its most specific type, and its attributes. */
        struct {
            trl_name type;
            trl_value *attributes;
            uint32_t count;
        } structured;
        /** @brief A large object whole: its kind and its octets. */
        struct {
            uint8_t kind;
            trl_slice octets;
        } lob;
        /** @brief A large object by locator: its kind, locator and lengths. */
        struct {
            uint8_t kind;
            uint32_t locator;
            uint64_t octets;
            uint64_t characters;
        } locator;
    } as;
};

/**
 * @brief Reads a value [PROTOCOL Values].
 * @param reader The reader; its arena holds what the value holds.
 * @param out The value.
 * @return Nonzero if read; else the reader failed.
 */
bool trl_read_value(trl_reader *reader, trl_value *out);

/**
 * @brief Reads a row: a list of values [PROTOCOL Composite fields].
 * @param reader The reader.
 * @param values The values, from the reader's arena.
 * @param count How many.
 * @return Nonzero if read.
 */
bool trl_read_row(trl_reader *reader, trl_value **values, uint32_t *count);

/**
 * @brief Writes a value [PROTOCOL Values], checking what a server would
 * refuse: strings UTF-8, a UUID of 16 octets, a large object's kind, a
 * JSON value's stored form, parts nested within TRL_MAX_NESTING.
 * @param buffer The buffer.
 * @param value The value.
 * @param error Where a value that cannot be sent is recorded.
 * @return Nonzero if written; else `error` says why.
 */
bool trl_write_value(trl_buffer *buffer, const trl_value *value, trl_codec_error *error);

/**
 * @brief Writes a row: the count, then each value.
 * @param buffer The buffer.
 * @param values The values; may be null if `count` is 0.
 * @param count How many.
 * @param error Where a value that cannot be sent is recorded.
 * @return Nonzero if written.
 */
bool trl_write_row(trl_buffer *buffer, const trl_value *values, size_t count, trl_codec_error *error);

#endif
