/**
 * @file fields.h
 * @brief The composite fields replies are made of: lists of strings,
 * columns, parameters, failures and their diagnostics
 * [PROTOCOL Composite fields] [PROTOCOL Errors and notices].
 */
#ifndef TRL_CODEC_FIELDS_H
#define TRL_CODEC_FIELDS_H

#include "codec/buffer.h"
#include "codec/reader.h"
#include "codec/types.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief How many diagnostics a failure or a notice may carry, tags 7 to 15. */
#define TRL_DIAGNOSTICS 9U

/** @brief A list of strings. */
typedef struct trl_strings {
    /** @brief The strings, views into the body. */
    trl_slice *items;
    /** @brief How many. */
    uint32_t count;
} trl_strings;

/** @brief A column: its name if it has one, its type, and whether NOT NULL. */
typedef struct trl_column {
    /** @brief Whether it is named. */
    bool named;
    /** @brief Its name. */
    trl_slice name;
    /** @brief Its type. */
    trl_data_type type;
    /** @brief Whether it is constrained NOT NULL. */
    bool not_null;
    /** @brief Whether the server told the type it was declared with, tail 19 [PROTOCOL Declared types]. */
    bool declared;
    /** @brief That type's name, as the Information Schema's DATA_TYPE gives it. */
    trl_slice declared_name;
    /** @brief That type, as SQL writes it. */
    trl_slice declared_written;
} trl_column;

/** @brief A dynamic parameter: whether CHARACTER VARYING of the longest length, and its type. */
typedef struct trl_parameter {
    /** @brief Whether the server gives it CHARACTER VARYING of the longest length. */
    bool varying;
    /** @brief Its type, written all the same. */
    trl_data_type type;
} trl_parameter;

/**
 * @brief What a failure or a notice says beyond its SQLSTATE and message:
 * severity, detail, hint, context, schema, table, column, data type and
 * constraint, in the order of their tags, 7 to 15.
 */
typedef struct trl_diagnostics {
    /** @brief Whether each is told. */
    bool told[TRL_DIAGNOSTICS];
    /** @brief Each one told. */
    trl_slice text[TRL_DIAGNOSTICS];
} trl_diagnostics;

/** @brief A failure, as an Error and a condition carry it [PROTOCOL Composite fields]. */
typedef struct trl_wire_failure {
    /** @brief Its SQLCODE. */
    int32_t sqlcode;
    /** @brief Its SQLSTATE. */
    trl_slice sqlstate;
    /** @brief Its message. */
    trl_slice message;
    /** @brief Whether it cites a rule. */
    bool cited;
    /** @brief The rule. */
    trl_slice rule;
    /** @brief Whether its position is known. */
    bool placed;
    /** @brief Its position, in characters into the statement. */
    uint64_t position;
    /** @brief Whether the session's transaction is active after it. */
    bool active;
    /** @brief What else it says, tags 7 to 15. */
    trl_diagnostics diagnostics;
} trl_wire_failure;

/**
 * @brief Reads a list of strings.
 * @param reader The reader.
 * @param out The list.
 * @return Nonzero if read.
 */
bool trl_read_strings(trl_reader *reader, trl_strings *out);

/**
 * @brief Writes a list of strings, each UTF-8.
 * @param buffer The buffer.
 * @param strings The strings.
 * @param count How many.
 * @param error Where a string not UTF-8 is recorded.
 * @return Nonzero if written.
 */
bool trl_write_strings(trl_buffer *buffer, const trl_slice *strings, size_t count, trl_codec_error *error);

/**
 * @brief Writes a string, UTF-8, refused otherwise.
 * @param buffer The buffer.
 * @param text The string.
 * @param error Where a string not UTF-8 is recorded.
 * @return Nonzero if written.
 */
bool trl_write_string(trl_buffer *buffer, trl_slice text, trl_codec_error *error);

/**
 * @brief Reads a list of columns.
 * @param reader The reader.
 * @param out The columns, from the reader's arena.
 * @param count How many.
 * @return Nonzero if read.
 */
bool trl_read_columns(trl_reader *reader, trl_column **out, uint32_t *count);

/**
 * @brief Reads a list of parameters.
 * @param reader The reader.
 * @param out The parameters, from the reader's arena.
 * @param count How many.
 * @return Nonzero if read.
 */
bool trl_read_parameters(trl_reader *reader, trl_parameter **out, uint32_t *count);

/**
 * @brief Reads diagnostics, a tail of tags 7 to 15, to the end of the body.
 * @param reader The reader.
 * @param out The diagnostics.
 * @return Nonzero if read.
 */
bool trl_read_diagnostics(trl_reader *reader, trl_diagnostics *out);

/**
 * @brief Reads a failure, its diagnostics to the end of the body.
 * @param reader The reader.
 * @param out The failure.
 * @return Nonzero if read.
 */
bool trl_read_failure(trl_reader *reader, trl_wire_failure *out);

#endif
