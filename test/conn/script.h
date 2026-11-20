/**
 * @file script.h
 * @brief A server of a test's own, over a program's stream: each request
 * the client writes is handed to the test's function, which writes the
 * replies the client then reads.
 */
#ifndef TRL_TEST_CONN_SCRIPT_H
#define TRL_TEST_CONN_SCRIPT_H

#include "codec/buffer.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The most requests a script records. */
#define SCRIPT_REQUESTS 32U

typedef struct script script;

/** @brief Answers one request of `kind` whose body is `body`. */
typedef void (*script_respond)(script *s, uint8_t kind, trl_slice body);

/** @brief A server of a test's own. */
struct script {
    /** @brief What the client reads. */
    trl_buffer to_client;
    /** @brief Where the client reads from. */
    size_t read_at;
    /** @brief What the client wrote. */
    trl_buffer from_client;
    /** @brief The bytes of it already handed on. */
    size_t handed;
    /** @brief The test's function; null answers nothing. */
    script_respond respond;
    /** @brief The test's own data. */
    void *data;
    /** @brief The kinds of the requests written, in order. */
    uint8_t kinds[SCRIPT_REQUESTS];
    /** @brief How many. */
    size_t requests;
    /** @brief Whether its stream bounds itself. */
    bool bounds;
    /** @brief The last bound set, and whether one was. */
    uint32_t bound_ms;
    /** @brief Whether a bound was set. */
    bool bounded;
    /** @brief Whether a read past what was written fails, rather than ends. */
    bool fail_reads;
    /** @brief Whether a write fails. */
    bool fail_writes;
    /** @brief Whether the client closed the stream. */
    bool closed;
};

/** @brief Makes an empty script answering by `respond`. */
void script_init(script *s, script_respond respond, void *data);

/** @brief Frees what a script holds. */
void script_free(script *s);

/** @brief The stream's functions, reaching `s`. */
trl_stream script_stream(script *s);

/** @brief Writes a frame of `kind` of `body`, for the client to read. */
void script_reply(script *s, uint8_t kind, const trl_buffer *body);

/** @brief Writes a Challenge of `bytes`. */
void script_challenge(script *s, const uint8_t *bytes, size_t length);

/** @brief Writes Ready of an authorization, a key of sixteen `7`s, capabilities, and the server's product. */
void script_ready(script *s, const char *authorization, const char *const *capabilities, size_t count);

/** @brief Writes an Error of `sqlcode`, `sqlstate` and `message`, citing `rule` if not null. */
void script_error(script *s, int32_t sqlcode, const char *sqlstate, const char *message, const char *rule);

/** @brief Writes a Status of a setting's name and value. */
void script_status(script *s, const char *name, const char *value);

/** @brief Writes a reply of `kind` and no body. */
void script_bare(script *s, uint8_t kind);

/** @brief Writes a Description of `count` columns, each named C<n>, CHARACTER VARYING(20). */
void script_description(script *s, uint32_t count);

/** @brief Writes Rows of `rows` rows, each of `count` character values "v". */
void script_rows(script *s, uint32_t rows, uint32_t count);

/** @brief Writes a Complete of `sqlcode` and `sqlstate`, active or not, tag 5 if `more`. */
void script_complete(script *s, int32_t sqlcode, const char *sqlstate, bool active, bool more);

/** @brief Writes Prepared of `number`, `parameters` parameters and `columns` columns. */
void script_prepared(script *s, uint32_t number, uint32_t parameters, uint32_t columns);

/**
 * @brief Writes a Complete of every tail: a warning 01003, a commit's
 * position of 42, 2 rows, identity values 7 and 8, a condition, and the
 * server's time of 30, 2 and 1.
 */
void script_complete_tails(script *s);

/** @brief Writes a Notice of `sqlstate` and `message`. */
void script_notice(script *s, const char *sqlstate, const char *message);

/** @brief Puts a string, a u32 count and its bytes. */
void script_put_string(trl_buffer *body, const char *text);

/** @brief Connects over the script as `user`, by `mechanism`, with `options`. */
trl_status script_connect(script *s, trl_mechanism *mechanism, const trl_conn_options *options, trl_conn **out);

#endif
