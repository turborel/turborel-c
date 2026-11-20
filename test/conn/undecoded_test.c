/**
 * @file undecoded_test.c
 * @brief Replies that do not decode, against a server of the test's own
 * [PROTOCOL Values and fields]: one whose frame is whole but holds a value
 * the client cannot take fails its request alone, the rest of the
 * request's replies read past, so the next request takes its own reply;
 * a malformed frame loses the connection (SQLCODE -919, SQLSTATE 08006).
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
#include <stdio.h>
#include <string.h>

/* A JSON document's version, and its null and array nodes. */
#define JSON_VERSION 1U
#define JSON_NULL 0U
#define JSON_ARRAY 10U
/* One deeper than a JSON document nests. */
#define TOO_DEEP 129U

/* What the server answers the first statement with. */
typedef enum broken {
    BROKEN_JSON = 0,
    BROKEN_DEEP_JSON,
    BROKEN_TRUTH,
    BROKEN_UNITS,
    BROKEN_CONDITION,
    BROKEN_CONDITION_MORE,
    BROKEN_STORED_LOB,
    BROKEN_FLAG,
    BROKEN_LONGER,
} broken;

/* A server's plan: what breaks, and how many statements it answered. */
typedef struct plan {
    broken what;
    int answered;
} plan;

/* Writes a Rows reply of one row of one value, its bytes raw. */
static void rows_of(script *s, const uint8_t *value, size_t length) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 1U);
    trl_put_u32(&body, 1U);
    trl_put_raw(&body, value, length);
    script_reply(s, TRL_KIND_ROWS, &body);
    trl_buffer_free(&body);
}

/* Writes a Description of one CHARACTER VARYING column of `units`. */
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

/* Writes a Rows reply of a JSON value of arrays nested `depth` deep. */
static void deep_json(script *s, unsigned int depth) {
    trl_buffer node;
    trl_buffer_init(&node);
    trl_put_u8(&node, JSON_NULL);
    for (unsigned int i = 0U; i < depth; ++i) {
        trl_buffer array;
        trl_buffer_init(&array);
        trl_put_u8(&array, JSON_ARRAY);
        trl_put_u32(&array, 1U);
        trl_put_u32(&array, (uint32_t)node.length);
        trl_put_u32(&array, 0U);
        trl_put_raw(&array, node.data, node.length);
        trl_buffer_free(&node);
        node = array;
    }
    trl_buffer value;
    trl_buffer_init(&value);
    trl_put_u8(&value, TRL_TAG_JSON);
    trl_put_u32(&value, (uint32_t)node.length + 1U);
    trl_put_u8(&value, JSON_VERSION);
    trl_put_raw(&value, node.data, node.length);
    rows_of(s, value.data, value.length);
    trl_buffer_free(&value);
    trl_buffer_free(&node);
}

/* Writes a Complete whose one condition is malformed, more following if
   `more`. */
static void complete_of_bad_condition(script *s, bool more) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    script_put_string(&body, "00000");
    trl_put_flag(&body, true);
    if (more) {
        trl_put_u8(&body, TRL_TAIL_MORE);
    }
    trl_put_u8(&body, TRL_TAIL_CONDITIONS);
    trl_put_u32(&body, 1U);
    static const uint8_t cut[] = {0xFFU};
    trl_slice bytes = {cut, sizeof cut};
    trl_put_bytes(&body, bytes);
    script_reply(s, TRL_KIND_COMPLETE, &body);
    trl_buffer_free(&body);
}

/* Writes a Complete whose last byte, its flag, is 2, or one with a byte
   after its end. */
static void malformed_complete(script *s, bool longer) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    script_put_string(&body, "00000");
    trl_put_u8(&body, longer ? 0U : 2U);
    if (longer) {
        trl_put_u8(&body, 0U);
    }
    script_reply(s, TRL_KIND_COMPLETE, &body);
    trl_buffer_free(&body);
}

/* Answers the first statement as the plan says. */
static void answer_broken(script *s, broken what) {
    static const uint8_t json[] = {TRL_TAG_JSON, 2U, 0U, 0U, 0U, JSON_VERSION, 0xEEU};
    static const uint8_t truth[] = {TRL_TAG_BOOLEAN, 2U};
    static const uint8_t good[] = {TRL_TAG_BOOLEAN, 1U};
    static const uint8_t stored[] = {TRL_TAG_CLOB, 0U, 1U, 0U, 0U, 0U, 'a'};
    if (what == BROKEN_FLAG || what == BROKEN_LONGER) {
        malformed_complete(s, what == BROKEN_LONGER);
        return;
    }
    if (what == BROKEN_CONDITION || what == BROKEN_CONDITION_MORE) {
        complete_of_bad_condition(s, what == BROKEN_CONDITION_MORE);
        if (what == BROKEN_CONDITION_MORE) {
            script_complete(s, 0, "00000", false, false);
        }
        return;
    }
    description_of(s, what == BROKEN_UNITS ? 7U : 0U);
    if (what == BROKEN_JSON) {
        rows_of(s, json, sizeof json);
    } else if (what == BROKEN_DEEP_JSON) {
        deep_json(s, TOO_DEEP);
    } else if (what == BROKEN_TRUTH) {
        rows_of(s, truth, sizeof truth);
    } else if (what == BROKEN_STORED_LOB) {
        rows_of(s, stored, sizeof stored);
    }
    rows_of(s, good, sizeof good);
    script_complete(s, 0, "00000", true, false);
}

/* Answers Hello with Ready, the first statement as planned, the next with
   a Complete of no data. */
static void respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    plan *p = s->data;
    if (kind == TRL_KIND_HELLO) {
        static const char *const granted[] = {TRL_CAPABILITY_CONDITIONS};
        script_ready(s, "ANN", granted, 1U);
        return;
    }
    if (p->answered++ == 0) {
        answer_broken(s, p->what);
    } else {
        script_complete(s, 100, "02000", false, false);
    }
}

/* A connection over a script answering as `p` says. */
static trl_conn *connected(script *s, plan *p) {
    script_init(s, respond, p);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    return conn;
}

/* Whether a statement answered with `what` fails alone, with `sqlstate`,
   and the next statement takes its own reply, of no data. */
static bool fails_alone(broken what, const char *sqlstate) {
    plan p = {what, 0};
    script s;
    trl_conn *conn = connected(&s, &p);
    trl_result *result = NULL;
    bool failed = trl_query(conn, "SELECT V FROM T", &result) == TRL_FAILED && !trl_conn_is_lost(conn) &&
                  strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), sqlstate) == 0;
    if (!failed) {
        char line[512];
        (void)trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        (void)fprintf(stderr, "%s\n", line);
    }
    bool own = trl_query(conn, "DELETE FROM T", &result) == TRL_OK && trl_result_sqlcode(result) == 100;
    trl_result_free(result);
    trl_conn_free(conn);
    script_free(&s);
    return failed && own;
}

/* Whether a statement answered with `what` loses the connection, 08006,
   and the next is told so. */
static bool loses(broken what) {
    plan p = {what, 0};
    script s;
    trl_conn *conn = connected(&s, &p);
    trl_result *result = NULL;
    bool lost = trl_query(conn, "DELETE FROM T", &result) == TRL_FAILED && trl_conn_is_lost(conn) &&
                strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0;
    bool told = trl_query(conn, "DELETE FROM T", &result) == TRL_FAILED &&
                strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0;
    trl_conn_free(conn);
    script_free(&s);
    return lost && told;
}

/**
 * Verifies: [PROTOCOL Values and fields] a reply whose frame is whole but
 * holds a value the client cannot take, damaged JSON (SQLSTATE TB916),
 * JSON nested too deep (0A000), a truth byte not 0 or 1, a bad units byte,
 * a value under tag 18 or 19 (RULES.md D8), fails its statement alone, the
 * rest of the statement's replies read past, so the next statement takes
 * its own reply [CLIENT Running statements].
 */
static void a_bad_value_fails_its_statement_alone(trl_test *test) {
    TRL_CHECK(test, fails_alone(BROKEN_JSON, "TB916"));
    TRL_CHECK(test, fails_alone(BROKEN_DEEP_JSON, "0A000"));
    TRL_CHECK(test, fails_alone(BROKEN_TRUTH, "TB916"));
    TRL_CHECK(test, fails_alone(BROKEN_UNITS, "TB916"));
    TRL_CHECK(test, fails_alone(BROKEN_STORED_LOB, "TB916"));
}

/**
 * Verifies: [PROTOCOL Complete 'K'] a malformed condition fails its
 * statement alone, a Complete that says more follow read past to its last;
 * its head still tells whether a transaction is active.
 */
static void a_bad_condition_fails_its_statement_alone(trl_test *test) {
    TRL_CHECK(test, fails_alone(BROKEN_CONDITION, "TB916"));
    TRL_CHECK(test, fails_alone(BROKEN_CONDITION_MORE, "TB916"));
    plan p = {BROKEN_CONDITION, 0};
    script s;
    trl_conn *conn = connected(&s, &p);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "COMMIT", &result) == TRL_FAILED && trl_conn_in_transaction(conn));
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL Values and fields] a malformed frame, a flag neither
 * 0 nor 1 or a byte after its last field, loses the connection, and the
 * next statement is told so.
 */
static void a_malformed_frame_loses_the_connection(trl_test *test) {
    TRL_CHECK(test, loses(BROKEN_FLAG));
    TRL_CHECK(test, loses(BROKEN_LONGER));
}

/* Answers Prepare, then an Execute with a batch holding a bad value and
   Suspended, then a Describe of a cursor with a bad Description, then a
   Complete of no data. */
static void respond_cursor(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    plan *p = s->data;
    static const uint8_t truth[] = {TRL_TAG_BOOLEAN, 3U};
    if (kind == TRL_KIND_HELLO) {
        static const char *const granted[] = {TRL_CAPABILITY_DESCRIBE};
        script_ready(s, "ANN", granted, 1U);
        return;
    }
    int answered = p->answered++;
    if (answered == 0) {
        script_prepared(s, 0U, 0U, 1U);
    } else if (answered == 1) {
        rows_of(s, truth, sizeof truth);
        script_bare(s, TRL_KIND_SUSPENDED);
    } else if (answered == 2) {
        description_of(s, 9U);
    } else {
        script_complete(s, 100, "02000", false, false);
    }
}

/**
 * Verifies: [PROTOCOL Execute 'E'] a cursor's batch holding a bad value is
 * read past up to its Suspended; a Describe of a cursor, answered by a
 * Description alone, reads nothing past it [PROTOCOL Describe 'Y']; the
 * statement after gets its own reply.
 */
static void a_cursor_or_a_describe_reads_only_its_own(trl_test *test) {
    plan p = {BROKEN_TRUTH, 0};
    script s;
    script_init(&s, respond_cursor, &p);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(&s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT V FROM T", &statement) == TRL_OK);
    trl_cursor cursor = {statement, 0U};
    trl_result *result = NULL;
    TRL_CHECK(test, trl_cursor_open(conn, cursor, NULL, 1U, &result) == TRL_FAILED && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_describe_cursor(conn, cursor, &result) == TRL_FAILED && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_query(conn, "DELETE FROM T", &result) == TRL_OK && trl_result_sqlcode(result) == 100);
    trl_result_free(result);
    trl_statement_free(statement);
    trl_conn_free(conn);
    script_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_bad_value_fails_its_statement_alone),
        TRL_TEST_CASE(a_bad_condition_fails_its_statement_alone),
        TRL_TEST_CASE(a_malformed_frame_loses_the_connection),
        TRL_TEST_CASE(a_cursor_or_a_describe_reads_only_its_own),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
