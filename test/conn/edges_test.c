/**
 * @file edges_test.c
 * @brief A connection at its edges, against a server of the test's own:
 * a stream that fails or ends within a reply, replies longer than what is
 * held ahead, what Terminate may meet, and arguments out of bounds
 * [PROTOCOL Conversations] [CLIENT Connecting].
 */
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A Status longer than what a connection holds ahead of its bytes. */
#define LONG_VALUE 70000U
#define LONG_NAME 2000U

/* What the server does after Ready. */
typedef enum after {
    AFTER_NOTHING = 0,
    AFTER_LONG_STATUS,
    AFTER_HALF_REPLY,
    AFTER_NOTICE_CLOSING,
    AFTER_STATUS_CLOSING,
    AFTER_PONG_CLOSING,
    AFTER_READY_CLOSING,
    AFTER_LONG_CLOSING,
    AFTER_BAD_CLOSING,
    AFTER_NOTICE_PONG,
} after;

/* Writes a Status whose value is `length` bytes. */
static void long_status(script *s, size_t length) {
    trl_buffer body;
    trl_buffer_init(&body);
    script_put_string(&body, "LONG");
    trl_put_u32(&body, (uint32_t)length);
    for (size_t i = 0U; i < length; ++i) {
        trl_put_u8(&body, 'v');
    }
    script_reply(s, TRL_KIND_STATUS, &body);
    trl_buffer_free(&body);
}

/* Writes a Notice of an SQLSTATE and a message. */
static void notice(script *s) {
    trl_buffer body;
    trl_buffer_init(&body);
    script_put_string(&body, "01000");
    script_put_string(&body, "a warning");
    script_reply(s, TRL_KIND_NOTICE, &body);
    trl_buffer_free(&body);
}

/* Answers what follows Terminate as the test asks. */
static void closing(script *s, after wanted) {
    static const uint8_t long_header[] = {TRL_KIND_ERROR, 0xff, 0xff, 0xff, 0x7f};
    static const uint8_t bad_error[] = {TRL_KIND_ERROR, 1, 0, 0, 0, 0};
    switch (wanted) {
    case AFTER_NOTICE_CLOSING:
        notice(s);
        break;
    case AFTER_STATUS_CLOSING:
        script_status(s, "SCHEMA", "ANN");
        break;
    case AFTER_PONG_CLOSING:
        script_bare(s, TRL_KIND_PONG);
        break;
    case AFTER_READY_CLOSING:
        script_ready(s, "ANN", NULL, 0U);
        break;
    case AFTER_LONG_CLOSING:
        trl_put_raw(&s->to_client, long_header, sizeof long_header);
        break;
    case AFTER_BAD_CLOSING:
        trl_put_raw(&s->to_client, bad_error, sizeof bad_error);
        break;
    case AFTER_NOTHING:
    case AFTER_LONG_STATUS:
    case AFTER_HALF_REPLY:
    case AFTER_NOTICE_PONG:
    default:
        break;
    }
}

/* A server of Ready granting `ping`, then answering as the test asks. */
static void respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    after *wanted = s->data;
    static const char *const granted[] = {TRL_CAPABILITY_PING};
    if (kind == TRL_KIND_HELLO) {
        script_ready(s, "ANN", granted, 1U);
    } else if (kind == TRL_KIND_TERMINATE) {
        closing(s, *wanted);
    } else if (*wanted == AFTER_LONG_STATUS) {
        long_status(s, LONG_VALUE);
        script_bare(s, TRL_KIND_PONG);
    } else if (*wanted == AFTER_HALF_REPLY) {
        static const uint8_t half[] = {TRL_KIND_PONG, 9, 0, 0, 0, 1, 2};
        trl_put_raw(&s->to_client, half, sizeof half);
    } else if (*wanted == AFTER_NOTICE_PONG) {
        notice(s);
        script_bare(s, TRL_KIND_PONG);
    }
}

/* A connection over a script answering as `wanted` says. */
static trl_conn *connected(script *s, after *wanted) {
    script_init(s, respond, wanted);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    return conn;
}

/**
 * Verifies: [PROTOCOL Frames] a reply longer than what a connection holds
 * ahead of its bytes is taken whole as they arrive; a stream that ends
 * within a reply loses the connection; a Notice before the reply is
 * passed over.
 */
static void replies_arrive_whole_or_lose_the_connection(trl_test *test) {
    after wanted = AFTER_LONG_STATUS;
    script s;
    trl_conn *conn = connected(&s, &wanted);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK && strlen(trl_conn_setting(conn, "LONG")) == LONG_VALUE);
    wanted = AFTER_NOTICE_PONG;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK);
    wanted = AFTER_HALF_REPLY;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "cannot receive: the connection closed within a reply") == 0);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Connecting] a stream of the program's that fails to
 * write or read loses the connection (SQLSTATE 08006), saying so.
 */
static void a_failing_stream_loses_the_connection(trl_test *test) {
    after wanted = AFTER_NOTHING;
    script s;
    trl_conn *conn = connected(&s, &wanted);
    s.fail_writes = true;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "cannot send: the program's stream failed to write") == 0);
    TRL_CHECK(test, trl_conn_set_read_timeout(conn, 1U) == TRL_FAILED);
    trl_conn_free(conn);
    script_free(&s);
    conn = connected(&s, &wanted);
    s.fail_reads = true;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "cannot receive: the program's stream failed to read") == 0);
    trl_conn_free(conn);
    script_free(&s);
}

/* Whether Terminate, the server answering as `wanted`, gives `expected`. */
static bool terminate_gives(after wanted, trl_status expected, const char *message) {
    script s;
    after asked = wanted;
    trl_conn *conn = connected(&s, &asked);
    bool same = trl_conn_terminate(conn) == expected &&
                (message == NULL || strstr(trl_failure_message(trl_conn_failure(conn)), message) != NULL);
    trl_conn_free(conn);
    script_free(&s);
    return same;
}

/**
 * Verifies: [PROTOCOL Terminate 'X'] after Terminate a Notice, a Status
 * or a Pong is no failure; another reply, one too long or one malformed
 * is.
 */
static void what_terminate_may_meet(trl_test *test) {
    TRL_CHECK(test, terminate_gives(AFTER_NOTICE_CLOSING, TRL_OK, NULL));
    TRL_CHECK(test, terminate_gives(AFTER_STATUS_CLOSING, TRL_OK, NULL));
    TRL_CHECK(test, terminate_gives(AFTER_PONG_CLOSING, TRL_OK, NULL));
    TRL_CHECK(test, terminate_gives(AFTER_READY_CLOSING, TRL_FAILED, "the server answered Terminate"));
    TRL_CHECK(test, terminate_gives(AFTER_LONG_CLOSING, TRL_FAILED, "longer than"));
    TRL_CHECK(test, terminate_gives(AFTER_BAD_CLOSING, TRL_FAILED, "malformed"));
}

/**
 * Verifies: [DESIGN] D9 arguments out of bounds or null are refused with
 * a status: an attribute past the protocol's limits, a setting's name of
 * null, a capability of null.
 */
static void arguments_out_of_bounds_are_refused(trl_test *test) {
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    char name[LONG_NAME];
    memset(name, 'n', sizeof name - 1U);
    name[sizeof name - 1U] = '\0';
    TRL_CHECK(test, trl_conn_options_attribute(options, name, "v") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_attribute(options, "n", name) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_attribute(NULL, "n", "v") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_attribute(options, NULL, "v") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_attribute(options, "n", NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_ask(NULL, "x") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_ask(options, NULL) == TRL_INVALID_ARGUMENT);
    for (int i = 0; i < 32; ++i) {
        TRL_CHECK(test, trl_conn_options_attribute(options, "n", "v") == TRL_OK);
    }
    TRL_CHECK(test, trl_conn_options_attribute(options, "n", "v") == TRL_INVALID_ARGUMENT);
    trl_conn_options_free(options);
    after wanted = AFTER_NOTHING;
    script s;
    trl_conn *conn = connected(&s, &wanted);
    TRL_CHECK(test, trl_conn_setting(conn, NULL) == NULL && !trl_conn_granted(conn, NULL));
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    TRL_CHECK(test, trl_mechanism_scram_key_cache(trust, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_scram_channel_binding(trust, TRL_CHANNEL_BINDING_PREFER) == TRL_INVALID_ARGUMENT);
    trl_mechanism_free(trust);
    trl_conn_free(conn);
    script_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(replies_arrive_whole_or_lose_the_connection),
        TRL_TEST_CASE(a_failing_stream_loses_the_connection),
        TRL_TEST_CASE(what_terminate_may_meet),
        TRL_TEST_CASE(arguments_out_of_bounds_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
