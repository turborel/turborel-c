/**
 * @file options.h
 * @brief What a connection asks for and how long it waits, as a login
 * reads them [CLIENT What a connection asks for] [CLIENT Timeouts].
 */
#ifndef TRL_CONN_OPTIONS_H
#define TRL_CONN_OPTIONS_H

#include "codec/buffer.h"
#include "codec/tail.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The options, their strings each its own. */
struct trl_conn_options {
    /** @brief The capabilities asked for, in the order asked. */
    char **capabilities;
    /** @brief How many. */
    size_t capability_count;
    /** @brief The connection attributes' names. */
    char *names[TRL_MAX_ATTRIBUTES];
    /** @brief Their values. */
    char *values[TRL_MAX_ATTRIBUTES];
    /** @brief How many. */
    size_t attribute_count;
    /** @brief The shortest frame sent compressed. */
    size_t compress_from;
    /** @brief The connect timeout, in milliseconds, 0 for none. */
    uint32_t connect_ms;
    /** @brief The read timeout, in milliseconds, 0 for none. */
    uint32_t read_ms;
};

/** @brief How many capabilities the protocol has [PROTOCOL Capabilities]. */
#define TRL_CAPABILITIES_KNOWN 20U

/** @brief Every capability of the protocol's, in its table's order [PROTOCOL Capabilities]. */
extern const char *const trl_capabilities_known[TRL_CAPABILITIES_KNOWN];

/**
 * @brief The options' capabilities, as Hello carries them, into `out`, of
 * their count; compression left out unless `compressing`, as a library
 * built without zlib cannot read a compressed frame. How many.
 */
size_t trl_conn_options_capabilities(const trl_conn_options *options, trl_slice *out, bool compressing);

/** @brief The options' attributes, as Hello carries them, into `out`, of their count. */
void trl_conn_options_attributes(const trl_conn_options *options, trl_attribute *out);

#endif
