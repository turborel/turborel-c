/**
 * @file control_test.c
 * @brief The session's control against a server of the test's own, at its
 * edges [PROTOCOL Conversations]: ChangeUser's exchange, Reset's
 * completion, query attributes' bounds, and no request before Ready.
 */
#include "codec/buffer.h"
#include "harness.h"
#include "scram_server.h"
#include "script.h"
#include "serving.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Verifies: [PROTOCOL ChangeUser 'U'] ChangeUser's Ready renews the
 * session as another user's; a reply out of turn loses the connection.
 */
static void change_user_takes_ready(trl_test *test) {
    serving plan = {PLAN_CHALLENGE, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", trust) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_conn_authorization(conn), "BOB") == 0);
    plan.plan = PLAN_PONG;
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", trust) == TRL_FAILED && trl_conn_is_lost(conn));
    trl_mechanism_free(trust);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL ChangeUser 'U'] ChangeUser's exchange by SCRAM:
 * each Challenge answered, the server's signature checked at Ready; one
 * signed wrongly fails; a mechanism that cannot answer fails; an Error is
 * the failure.
 */
static void change_user_answers_challenges(trl_test *test) {
    scram_server server;
    memset(&server, 0, sizeof server);
    script s;
    script_init(&s, scram_serving, &server);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram(PASSWORD, &scram);
    static const char *const granted[] = {TRL_CAPABILITY_CHANGE_USER};
    server.granted = granted;
    server.granted_count = 1U;
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, scram, NULL, &conn) == TRL_OK);
    server.proof_right = false;
    TRL_CHECK(test, trl_conn_change_user(conn, "ann", scram) == TRL_OK && server.proof_right);
    server.ending = ENDING_WRONG_SIGNATURE;
    TRL_CHECK(test, trl_conn_change_user(conn, "ann", scram) == TRL_FAILED);
    trl_conn_free(conn);
    script_free(&s);
    trl_mechanism_free(scram);
    serving plan = {PLAN_ONLY_CHALLENGE, true};
    conn = serving_connect(&s, &plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", trust) == TRL_FAILED);
    plan.plan = PLAN_ERROR;
    trl_conn_free(conn);
    script_free(&s);
    conn = serving_connect(&s, &plan);
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", trust) == TRL_FAILED &&
                        serving_failed_with(conn, "a statement refused"));
    TRL_CHECK(test, trl_conn_change_user(conn, NULL, trust) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", NULL) == TRL_INVALID_ARGUMENT);
    trl_mechanism_free(trust);
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL Reset 'W'] Reset read to its completion; a reply out
 * of turn loses the connection, an Error is the failure.
 */
static void reset_takes_its_completion(trl_test *test) {
    serving plan = {PLAN_DONE, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_OK);
    plan.plan = PLAN_ERROR;
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_FAILED && !trl_conn_is_lost(conn));
    plan.plan = PLAN_PONG;
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_FAILED && serving_failed_with(conn, "the server sent no completion"));
    TRL_CHECK(test, trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL The tail] attributes past their bounds, more
 * than 32, a name empty or longer than 64 bytes, a value longer than 1024,
 * or not UTF-8, refused before sending (SQLCODE -919, SQLSTATE 08006), the
 * connection kept; those within sent with each statement.
 */
static void attributes_past_their_bounds_are_refused(trl_test *test) {
    serving plan = {PLAN_DONE, true};
    script s;
    trl_conn *conn = serving_connect(&s, &plan);
    char long_name[66];
    memset(long_name, 'n', sizeof long_name - 1U);
    long_name[sizeof long_name - 1U] = '\0';
    char long_value[1026];
    memset(long_value, 'v', sizeof long_value - 1U);
    long_value[sizeof long_value - 1U] = '\0';
    const char *names[33];
    const char *values[33];
    for (size_t i = 0U; i < 33U; ++i) {
        names[i] = "k";
        values[i] = "v";
    }
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, names, values, 33U) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0 && !trl_conn_is_lost(conn));
    const char *const bad_names[] = {"", long_name, "k", "\xff", "k"};
    const char *const bad_values[] = {"v", "v", long_value, "v", "\xff"};
    for (size_t i = 0U; i < 5U; ++i) {
        TRL_CHECK(test, trl_conn_set_query_attributes(conn, &bad_names[i], &bad_values[i], 1U) == TRL_FAILED);
    }
    const char *const nulls[] = {NULL};
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, nulls, values, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, names, nulls, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, NULL, values, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, names, values, 32U) == TRL_OK);
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, names, values, 2U) == TRL_OK);
    trl_result *result = NULL;
    size_t before = s.from_client.length;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_OK && s.from_client.length > before + 10U);
    trl_result_free(result);
    trl_conn_free(conn);
    script_free(&s);
}

/* Refuses the login. */
static void refusing(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    (void)kind;
    script_error(s, -919, "28000", "no such user", NULL);
}

/**
 * Verifies: [PROTOCOL Requests] no request but Hello, Answer and Cancel
 * is sent before Ready: a connection whose login failed sends no
 * statement; [PROTOCOL Errors and notices] a replica behind the position
 * asked for fails the statement (SQLCODE -920, SQLSTATE TBR01), the
 * connection kept.
 */
static void requests_wait_for_ready(trl_test *test) {
    script s;
    script_init(&s, refusing, NULL);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_FAILED && conn != NULL);
    size_t sent = s.from_client.length;
    trl_result *result = NULL;
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_FAILED && trl_prepare(conn, "S", &statement) == TRL_FAILED);
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_FAILED && s.from_client.length == sent);
    trl_conn_free(conn);
    script_free(&s);
    trl_mechanism_free(trust);
    serving plan = {PLAN_BEHIND, true};
    conn = serving_connect(&s, &plan);
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_FAILED && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_failure_sqlcode(trl_conn_failure(conn)) == -920);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "TBR01") == 0);
    plan.plan = PLAN_DONE;
    TRL_CHECK(test, trl_query(conn, "Q", &result) == TRL_OK);
    trl_result_free(result);
    trl_conn_free(conn);
    script_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(change_user_takes_ready),    TRL_TEST_CASE(change_user_answers_challenges),
        TRL_TEST_CASE(reset_takes_its_completion), TRL_TEST_CASE(attributes_past_their_bounds_are_refused),
        TRL_TEST_CASE(requests_wait_for_ready),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
