/**
 * @file session_test.c
 * @brief A connection's session, against a server of the test's own:
 * Ping [PROTOCOL Ping 'G'], Terminate [PROTOCOL Terminate 'X'], what is
 * asked for [CLIENT What a connection asks for], Statuses kept
 * [CLIENT What the server tells], compressed frames
 * [PROTOCOL Compressed frames], and the longest reply taken
 * [CLIENT Timeouts].
 */
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HEADER_BYTES 5U
#define U32_BYTES 4U
#define LONG_STATUS 2048U

/* What a test's server does after Ready. */
typedef enum after {
    AFTER_PONG = 0,
    AFTER_STATUS_PONG,
    AFTER_ERROR_08,
    AFTER_ERROR_42,
    AFTER_COMPRESSED_PONG,
    AFTER_LONG_PONG,
    AFTER_ROWS,
    AFTER_TERMINATE_ERROR,
    AFTER_TERMINATE_NOTICE_ERROR,
    AFTER_TERMINATE_NOTICE,
} after;

/* The capabilities a test's server grants. */
typedef struct serving {
    after after;
    bool compression;
} serving;

/* Writes a long Status compressed, a z frame holding it, then a Pong. */
static void compressed_pong(script *s) {
    trl_buffer frame;
    trl_buffer_init(&frame);
    size_t start = trl_frame_start(&frame, TRL_KIND_STATUS);
    for (size_t i = 0U; i < 2U; ++i) {
        trl_put_u32(&frame, LONG_STATUS / 2U);
        for (size_t j = 0U; j < LONG_STATUS / 2U; ++j) {
            trl_put_u8(&frame, 'x');
        }
    }
    trl_frame_finish(&frame, start);
    trl_codec_error error = {0, "", ""};
    trl_slice frames = {frame.data, frame.length};
    (void)trl_compress_frames(&s->to_client, frames, 0U, &error);
    trl_buffer_free(&frame);
    script_bare(s, TRL_KIND_PONG);
}

/* Answers each request after Ready as the test asks. */
static void after_ready(script *s, const serving *plan, uint8_t kind) {
    static const uint8_t long_header[] = {TRL_KIND_PONG, 100U, 0U, 0U, 0U};
    if (kind == TRL_KIND_TERMINATE) {
        if (plan->after == AFTER_TERMINATE_NOTICE_ERROR || plan->after == AFTER_TERMINATE_NOTICE) {
            script_notice(s, "01000", "a warning");
        }
        if (plan->after == AFTER_TERMINATE_NOTICE_ERROR) {
            script_status(s, "READ_ONLY", "NO");
        }
        if (plan->after == AFTER_TERMINATE_ERROR || plan->after == AFTER_TERMINATE_NOTICE_ERROR) {
            script_error(s, -320, "40002", "a deferred constraint is not satisfied", NULL);
        }
        return;
    }
    switch (plan->after) {
    case AFTER_STATUS_PONG:
        script_status(s, "SESSION.TIME_ZONE", "+02:00");
        script_bare(s, TRL_KIND_PONG);
        break;
    case AFTER_ERROR_08:
        script_error(s, -919, "08006", "the server is stopping", NULL);
        break;
    case AFTER_ERROR_42:
        script_error(s, -900, "0A000", "not now", NULL);
        break;
    case AFTER_COMPRESSED_PONG:
        compressed_pong(s);
        break;
    case AFTER_LONG_PONG:
        trl_put_raw(&s->to_client, long_header, sizeof long_header);
        break;
    case AFTER_ROWS:
        script_bare(s, TRL_KIND_SUSPENDED);
        break;
    case AFTER_PONG:
    case AFTER_TERMINATE_ERROR:
    case AFTER_TERMINATE_NOTICE_ERROR:
    case AFTER_TERMINATE_NOTICE:
    default:
        script_bare(s, TRL_KIND_PONG);
        break;
    }
}

/* A server granting `ping` and `status`, and `compression` if asked. */
static void respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    const serving *plan = s->data;
    static const char *const granted[] = {TRL_CAPABILITY_STATUS, TRL_CAPABILITY_PING, TRL_CAPABILITY_COMPRESSION};
    if (kind == TRL_KIND_HELLO) {
        script_status(s, "SESSION.TIME_ZONE", "+00:00");
        script_ready(s, "ANN", granted, plan->compression ? 3U : 2U);
        return;
    }
    after_ready(s, plan, kind);
}

/* Connects to a server answering as `plan` says, by trust. */
static trl_conn *connected(script *s, serving *plan, const trl_conn_options *options) {
    script_init(s, respond, plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, options, &conn);
    trl_mechanism_free(trust);
    return conn;
}

/**
 * Verifies: [PROTOCOL Ping 'G'] Ping is answered by Pong, a Status before
 * it kept at once [CLIENT What the server tells]; an Error of class 08
 * loses the connection, another does not, and a reply out of turn loses it.
 */
static void ping_is_answered_by_pong(trl_test *test) {
    serving plan = {AFTER_STATUS_PONG, false};
    script s;
    trl_conn *conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK);
    TRL_CHECK(test, s.kinds[1] == TRL_KIND_PING && strcmp(trl_conn_setting(conn, "SESSION.TIME_ZONE"), "+02:00") == 0);
    TRL_CHECK(test, trl_conn_setting_count(conn) == 1U);
    plan.after = AFTER_ERROR_42;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && !trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "0A000") == 0);
    plan.after = AFTER_ERROR_08;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "the server is stopping") == 0);
    trl_conn_free(conn);
    script_free(&s);
    plan.after = AFTER_ROWS;
    conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "the server sent no Pong") == 0);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT What a connection asks for] a method needing a
 * capability not granted fails before it sends anything, the connection
 * kept.
 */
static void a_capability_not_granted_sends_nothing(trl_test *test) {
    serving plan = {AFTER_PONG, false};
    script s;
    script_init(&s, NULL, &plan);
    script_ready(&s, "ANN", NULL, 0U);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_OK);
    size_t written = s.from_client.length;
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && s.from_client.length == written);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "the server did not grant ping") == 0);
    TRL_CHECK(test, !trl_conn_is_lost(conn));
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    script_free(&s);
}

/* The capabilities field of the Hello the client wrote: their count. */
static uint32_t asked_count(const script *s) {
    const uint8_t *at = s->from_client.data + HEADER_BYTES + U32_BYTES;
    for (int field = 0; field < 3; ++field) {
        at += U32_BYTES + ((uint32_t)at[0] | ((uint32_t)at[1] << 8U));
    }
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8U);
}

/**
 * Verifies: [CLIENT What a connection asks for] an offer of the program's
 * own: none, compression added, a connection attribute carried in
 * Hello's tail, tag 1 [PROTOCOL Hello 'H'].
 */
static void an_offer_of_the_programs_own(trl_test *test) {
    trl_conn_options *options = NULL;
    TRL_CHECK(test, trl_conn_options_new(&options) == TRL_OK);
    serving plan = {AFTER_PONG, false};
    script s;
    trl_conn *conn = connected(&s, &plan, options);
    TRL_CHECK(test, asked_count(&s) == 18U);
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, trl_conn_options_ask_none(options) == TRL_OK);
    conn = connected(&s, &plan, options);
    TRL_CHECK(test, asked_count(&s) == 0U && s.from_client.data[s.from_client.length - 1U] == 0U);
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, trl_conn_options_ask(options, TRL_CAPABILITY_COMPRESSION) == TRL_OK);
    TRL_CHECK(test, trl_conn_options_ask(options, TRL_CAPABILITY_COMPRESSION) == TRL_OK);
    TRL_CHECK(test, trl_conn_options_attribute(options, "application", "payroll") == TRL_OK);
    conn = connected(&s, &plan, options);
    TRL_CHECK(test, asked_count(&s) == (trl_compression_built() ? 1U : 0U));
    static const char tail[] = "application\x07\x00\x00\x00payroll";
    TRL_CHECK(test,
              memcmp(s.from_client.data + s.from_client.length - (sizeof tail - 1U), tail, sizeof tail - 1U) == 0);
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, trl_conn_options_attribute(options, "", "x") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_ask(options, "") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_compress_from(options, 1U) == TRL_OK);
    TRL_CHECK(test, trl_conn_options_timeouts(options, 1U, 2U) == TRL_OK);
    TRL_CHECK(test, trl_conn_options_new(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_ask_none(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_compress_from(NULL, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_options_timeouts(NULL, 1U, 1U) == TRL_INVALID_ARGUMENT);
    trl_conn_options_free(options);
    trl_conn_options_free(NULL);
}

/**
 * Verifies: [PROTOCOL Compressed frames] once `compression` is granted, a
 * z frame received is inflated and read; one received when it was not
 * granted fails the connection.
 */
static void compressed_frames_only_once_granted(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    serving plan = {AFTER_COMPRESSED_PONG, true};
    script s;
    trl_conn *conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK);
    TRL_CHECK(test, s.kinds[1] == TRL_KIND_PING);
    TRL_CHECK(test, trl_conn_setting_count(conn) == 2U);
    trl_conn_free(conn);
    script_free(&s);
    plan.compression = false;
    conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Timeouts] a reply longer than the bytes the program
 * takes loses the connection; a stream the program opened is bounded by
 * its maker, so its read timeout is not set.
 */
static void a_longer_reply_loses_the_connection(trl_test *test) {
    serving plan = {AFTER_LONG_PONG, false};
    script s;
    trl_conn *conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_set_max_body(conn, 0U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_max_body(conn, TRL_MAX_BODY_BYTES + 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_max_body(conn, 10U) == TRL_OK);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "a message of 100 bytes is longer than the 10 taken") == 0);
    TRL_CHECK(test, trl_conn_set_read_timeout(conn, 5U) == TRL_FAILED);
    TRL_CHECK(test, trl_conn_read_timeout(conn) == TRL_NO_LIMIT);
    trl_canceller *canceller = NULL;
    TRL_CHECK(test, trl_canceller_new(conn, &canceller) == TRL_FAILED && canceller == NULL);
    TRL_CHECK(test, trl_failure_sqlcode(trl_conn_failure(conn)) == -910);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL Terminate 'X'] Terminate ends the session; a
 * failing commit's Error, sent before the server closes, is its failure;
 * every call after fails, the connection lost.
 */
static void terminate_gives_a_failing_commit(trl_test *test) {
    serving plan = {AFTER_PONG, false};
    script s;
    trl_conn *conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_OK && s.kinds[1] == TRL_KIND_TERMINATE);
    TRL_CHECK(test, trl_conn_is_lost(conn) && trl_conn_ping(conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "the connection was terminated") == 0);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_FAILED);
    trl_conn_free(conn);
    script_free(&s);
    plan.after = AFTER_TERMINATE_ERROR;
    conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "40002") == 0);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Connecting] a Notice and a Status before the Error
 * that ends Terminate are read past, and the Error reported; a Notice
 * before the close ends it well.
 */
static void terminate_reads_past_a_notice_to_its_error(trl_test *test) {
    serving plan = {AFTER_TERMINATE_NOTICE_ERROR, false};
    script s;
    trl_conn *conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "40002") == 0);
    trl_conn_free(conn);
    script_free(&s);
    plan.after = AFTER_TERMINATE_NOTICE;
    conn = connected(&s, &plan, NULL);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_OK);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Timeouts] a stream of the program's own that bounds
 * itself is bounded by the read timeout the options give as it opens,
 * its read timeout settable as a connection's, and Terminate waits at most
 * it; one its maker bounds cannot be [CLIENT Connecting].
 */
static void a_stream_that_bounds_itself(trl_test *test) {
    serving plan = {AFTER_PONG, false};
    script s;
    script_init(&s, respond, &plan);
    s.bounds = true;
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_timeouts(options, 0U, 250U);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, trust, options, &conn) == TRL_OK && s.bounded && s.bound_ms == 250U);
    TRL_CHECK(test, trl_conn_read_timeout(conn) == 250U);
    TRL_CHECK(test, trl_conn_set_read_timeout(conn, 0U) == TRL_OK && s.bound_ms == 0U);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_OK && s.bound_ms == 60000U);
    trl_conn_free(conn);
    script_free(&s);
    conn = connected(&s, &plan, options);
    TRL_CHECK(test, trl_conn_set_read_timeout(conn, 10U) == TRL_FAILED && !s.bounded);
    trl_conn_free(conn);
    script_free(&s);
    trl_conn_options_free(options);
    trl_mechanism_free(trust);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(ping_is_answered_by_pong),
        TRL_TEST_CASE(a_capability_not_granted_sends_nothing),
        TRL_TEST_CASE(an_offer_of_the_programs_own),
        TRL_TEST_CASE(compressed_frames_only_once_granted),
        TRL_TEST_CASE(a_longer_reply_loses_the_connection),
        TRL_TEST_CASE(terminate_gives_a_failing_commit),
        TRL_TEST_CASE(terminate_reads_past_a_notice_to_its_error),
        TRL_TEST_CASE(a_stream_that_bounds_itself),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
