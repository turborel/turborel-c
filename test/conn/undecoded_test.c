/**
 * @file undecoded_test.c
 * @brief Replies that cannot be decoded, against a server of the test's
 * own: each loses the connection (SQLCODE -919, SQLSTATE 08006), as every
 * Turborel driver does (RULES.md D8), the rest of the statement's replies
 * being left unread otherwise.
 */
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What the server sends that cannot be decoded. */
typedef enum broken {
    BROKEN_TRUTH = 0,
    BROKEN_UNITS,
    BROKEN_JSON,
    BROKEN_CONDITION,
    BROKEN_STORED_LOB,
} broken;

/* Writes a Rows reply of one row of one value, its tag and body raw. */
static void rows_of(script *s, const uint8_t *value, size_t length) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 1U);
    trl_put_u32(&body, 1U);
    trl_put_raw(&body, value, length);
    script_reply(s, TRL_KIND_ROWS, &body);
    trl_buffer_free(&body);
}

/* Writes a Description of one CHARACTER VARYING column of units `units`. */
static void description_of(script *s, uint8_t units) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 1U);
    trl_put_flag(&body, false);
    trl_put_u8(&body, TRL_TAG_VARYING);
    trl_put_u32(&body, 5U);
    trl_put_u8(&body, units);
    trl_put_flag(&body, false);
    script_reply(s, TRL_KIND_DESCRIPTION, &body);
    trl_buffer_free(&body);
}

/* Writes a Complete whose tag 17 holds a condition cut short. */
static void complete_of_bad_condition(script *s) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    script_put_string(&body, "00000");
    trl_put_flag(&body, false);
    trl_put_u8(&body, TRL_TAIL_CONDITIONS);
    trl_put_u32(&body, 1U);
    static const uint8_t cut[] = {0U, 0U};
    trl_slice bytes = {cut, sizeof cut};
    trl_put_bytes(&body, bytes);
    script_reply(s, TRL_KIND_COMPLETE, &body);
    trl_buffer_free(&body);
}

/* Answers Hello with Ready, and a query with what cannot be decoded. */
static void respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    const broken *what = s->data;
    static const uint8_t truth[] = {TRL_TAG_BOOLEAN, 2U};
    static const uint8_t json[] = {TRL_TAG_JSON, 3U, 0U, 0U, 0U, 0xEEU, 0xEEU, 0xEEU};
    static const uint8_t stored[] = {TRL_TAG_CLOB, 0U, 1U, 0U, 0U, 0U, 'a'};
    if (kind == TRL_KIND_HELLO) {
        static const char *const granted[] = {TRL_CAPABILITY_CONDITIONS};
        script_ready(s, "ANN", granted, 1U);
        return;
    }
    if (*what == BROKEN_CONDITION) {
        complete_of_bad_condition(s);
        return;
    }
    description_of(s, *what == BROKEN_UNITS ? 2U : 0U);
    if (*what == BROKEN_TRUTH) {
        rows_of(s, truth, sizeof truth);
    } else if (*what == BROKEN_JSON) {
        rows_of(s, json, sizeof json);
    } else if (*what == BROKEN_STORED_LOB) {
        rows_of(s, stored, sizeof stored);
    }
    script_complete(s, 0, "00000", false, false);
}

/* Whether a query answered with `what` loses the connection, 08006. */
static bool loses(broken what) {
    script s;
    script_init(&s, respond, &what);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(&s, trust, NULL, &conn);
    trl_result *result = NULL;
    bool lost = trl_query(conn, "Q", &result) == TRL_FAILED && trl_conn_is_lost(conn) &&
                strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0 &&
                trl_failure_sqlcode(trl_conn_failure(conn)) == -919;
    trl_result_free(result);
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    script_free(&s);
    return lost;
}

/**
 * Verifies: [PROTOCOL Values] a truth byte other than 0 or 1, a units
 * byte other than 0 or 1, a damaged JSON document, and a value under tag
 * 18 or 19, a large object's stored form, lose the connection.
 */
static void values_not_decoded_lose_the_connection(trl_test *test) {
    TRL_CHECK(test, loses(BROKEN_TRUTH));
    TRL_CHECK(test, loses(BROKEN_UNITS));
    TRL_CHECK(test, loses(BROKEN_JSON));
    TRL_CHECK(test, loses(BROKEN_STORED_LOB));
}

/**
 * Verifies: [PROTOCOL Complete 'K'] a condition in tag 17 that cannot be
 * read loses the connection.
 */
static void a_bad_condition_loses_the_connection(trl_test *test) {
    TRL_CHECK(test, loses(BROKEN_CONDITION));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(values_not_decoded_lose_the_connection),
        TRL_TEST_CASE(a_bad_condition_loses_the_connection),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
