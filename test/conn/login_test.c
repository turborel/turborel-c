/**
 * @file login_test.c
 * @brief Logging in [PROTOCOL Opening a connection] [CLIENT Connecting],
 * against a server of the test's own: Hello as asked, each Challenge
 * answered, Ready kept, a refusal or a reply out of turn failing, and
 * SCRAM's whole exchange with its key cache.
 */
#include "auth/crypto.h"
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "harness.h"
#include "scram_server.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Answers a trusted login: Statuses, then Ready of `status` and `ping`. */
static void trusting(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    static const char *const granted[] = {TRL_CAPABILITY_STATUS, TRL_CAPABILITY_PING};
    if (kind == TRL_KIND_HELLO) {
        script_status(s, "AUTHORIZATION", "ANN");
        script_status(s, "SESSION.TIME_ZONE", "+00:00");
        script_ready(s, "ANN", granted, 2U);
    }
}

/* A server trusting the user and granting a name the protocol does not
   have, and others twice and out of order. */
static void granting_oddly(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    static const char *const granted[] = {
        TRL_CAPABILITY_PING,
        "teleport",
        TRL_CAPABILITY_STATUS,
        TRL_CAPABILITY_PING,
    };
    if (kind == TRL_KIND_HELLO) {
        script_ready(s, "ANN", granted, 4U);
    }
}

/**
 * Verifies: [PROTOCOL Capabilities] what Ready grants is kept as
 * turborel-client keeps it: each capability of the protocol's once, in
 * its table's order, a name it does not have passed over.
 */
static void granted_capabilities_are_the_known(trl_test *test) {
    script s;
    script_init(&s, granting_oddly, NULL);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_OK);
    TRL_CHECK(test, trl_conn_granted_count(conn) == 2U && !trl_conn_granted(conn, "teleport"));
    TRL_CHECK(test, strcmp(trl_conn_granted_at(conn, 0U), TRL_CAPABILITY_STATUS) == 0);
    TRL_CHECK(test, strcmp(trl_conn_granted_at(conn, 1U), TRL_CAPABILITY_PING) == 0);
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    script_free(&s);
}

/**
 * Verifies: [PROTOCOL Opening a connection] Hello is the first message,
 * of version 1, the user, the mechanism and every capability but
 * compression, by the names [PROTOCOL Capabilities] gives them; each
 * Status before Ready is kept, and Ready gives the authorization
 * identifier, the server's product and what was granted.
 */
static void a_trusted_login_keeps_what_ready_told(trl_test *test) {
    script s;
    script_init(&s, trusting, NULL);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_OK);
    TRL_CHECK(test, s.requests == 1U && s.kinds[0] == TRL_KIND_HELLO);
    static const uint8_t start[] = {
        1, 0, 0, 0, 3,  0, 0, 0, 'a', 'n', 'n', 5, 0,   0,   0,   't', 'r', 'u', 's', 't',
        0, 0, 0, 0, 21, 0, 0, 0, 7,   0,   0,   0, 'n', 'o', 't', 'i', 'c', 'e', 's',
    };
    TRL_CHECK(test,
              s.from_client.length > sizeof start + 5U && memcmp(s.from_client.data + 5U, start, sizeof start) == 0);
    TRL_CHECK(test, strcmp(trl_conn_authorization(conn), "ANN") == 0);
    TRL_CHECK(test, strcmp(trl_conn_server(conn), "turborel 0.0.0 (alpha)") == 0);
    TRL_CHECK(test, trl_conn_granted_count(conn) == 2U && trl_conn_granted(conn, TRL_CAPABILITY_PING));
    TRL_CHECK(test, strcmp(trl_conn_granted_at(conn, 0U), TRL_CAPABILITY_STATUS) == 0);
    TRL_CHECK(test, trl_conn_granted_at(conn, 2U) == NULL && !trl_conn_granted(conn, TRL_CAPABILITY_COPY));
    TRL_CHECK(test, strcmp(trl_conn_setting(conn, "SESSION.TIME_ZONE"), "+00:00") == 0);
    TRL_CHECK(test, trl_conn_setting(conn, "SCHEMA") == NULL && trl_conn_setting_count(conn) == 2U);
    const char *name = NULL;
    const char *value = NULL;
    TRL_CHECK(test, trl_conn_setting_at(conn, 0U, &name, &value) && strcmp(name, "AUTHORIZATION") == 0 &&
                        strcmp(value, "ANN") == 0);
    TRL_CHECK(test, !trl_conn_setting_at(conn, 2U, &name, &value) && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_conn_setting_at(conn, 1U, NULL, NULL));
    trl_conn_free(conn);
    TRL_CHECK(test, s.closed);
    trl_mechanism_free(trust);
    script_free(&s);
}

/* Refuses the login as a wrong password is refused. */
static void refusing(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    if (kind == TRL_KIND_HELLO) {
        script_error(s, -919, "28000", "the user or the password is wrong", "DESIGN");
    }
}

/* Sends Pong where Ready belongs. */
static void out_of_turn(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    if (kind == TRL_KIND_HELLO) {
        script_bare(s, TRL_KIND_PONG);
    }
}

/**
 * Verifies: [PROTOCOL Opening a connection] refused, the server's Error is
 * the failure and the connection is lost; a reply other than Challenge,
 * Ready or Error, or the stream's end, loses it (SQLSTATE 08006).
 */
static void a_refusal_or_a_reply_out_of_turn_fails(trl_test *test) {
    script s;
    script_init(&s, refusing, NULL);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_FAILED);
    const trl_failure *failure = trl_conn_failure(conn);
    TRL_CHECK(test, trl_failure_sqlcode(failure) == -919 && strcmp(trl_failure_sqlstate(failure), "28000") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(failure), "the user or the password is wrong") == 0);
    TRL_CHECK(test, strcmp(trl_failure_rule(failure), "DESIGN") == 0 && trl_failure_ends_connection(failure));
    TRL_CHECK(test, trl_conn_is_lost(conn) && s.closed);
    trl_conn_free(conn);
    script_free(&s);
    script_init(&s, out_of_turn, NULL);
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)), "the server sent no Ready") == 0);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    trl_conn_free(conn);
    script_free(&s);
    script_init(&s, NULL, NULL);
    TRL_CHECK(test, script_connect(&s, trust, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test,
              strcmp(trl_failure_message(trl_conn_failure(conn)), "cannot receive: failed to fill whole buffer") == 0);
    trl_conn_free(conn);
    script_free(&s);
    trl_mechanism_free(trust);
}

/* Connects over a SCRAM server of `s` ending as `end` says, with `cache` if
   given; the caller frees `s` once it has freed the connection. */
static trl_status scram_login(ending end, trl_key_cache *cache, script *s, trl_conn **conn, bool *proof_right) {
    scram_server server;
    memset(&server, 0, sizeof server);
    server.ending = end;
    script_init(s, scram_serving, &server);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram(PASSWORD, &scram);
    if (cache != NULL) {
        (void)trl_mechanism_scram_key_cache(scram, cache);
    }
    trl_status made = script_connect(s, scram, NULL, conn);
    *proof_right = server.proof_right;
    trl_mechanism_free(scram);
    s->data = NULL;
    return made;
}

/**
 * Verifies: [PROTOCOL Mechanisms] SCRAM-SHA-256's four messages after
 * Hello: client-first in Hello, server-first, client-final proved,
 * server-final verified, and an empty Answer; the keys kept in a cache
 * and found by the next login [CLIENT The SCRAM key cache].
 */
static void scram_logs_in_and_keeps_its_keys(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_key_cache *cache = NULL;
    (void)trl_key_cache_new(&cache);
    trl_conn *conn = NULL;
    bool proof_right = false;
    script s;
    TRL_CHECK(test, scram_login(ENDING_PROVED, cache, &s, &conn, &proof_right) == TRL_OK && proof_right);
    trl_conn_free(conn);
    script_free(&s);
    uint64_t hits = 0U;
    uint64_t misses = 0U;
    trl_key_cache_lookups(cache, &hits, &misses);
    TRL_CHECK(test, hits == 0U && misses == 1U && trl_key_cache_size(cache) == 1U);
    TRL_CHECK(test, scram_login(ENDING_PROVED, cache, &s, &conn, &proof_right) == TRL_OK && proof_right);
    trl_conn_free(conn);
    script_free(&s);
    trl_key_cache_lookups(cache, &hits, &misses);
    TRL_CHECK(test, hits == 1U && misses == 1U);
    trl_key_cache_free(cache);
}

/**
 * Verifies: [CLIENT Connecting] SCRAM is refused unless the server proves
 * it knows the user's verifier: a wrong signature (SQLSTATE 28000), or
 * Ready with none (SQLSTATE 08006); the cache drops the entry a failed
 * login used [CLIENT The SCRAM key cache].
 */
static void scram_is_refused_unless_the_server_proves(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_key_cache *cache = NULL;
    (void)trl_key_cache_new(&cache);
    trl_conn *conn = NULL;
    bool proof_right = false;
    script s;
    TRL_CHECK(test, scram_login(ENDING_WRONG_SIGNATURE, cache, &s, &conn, &proof_right) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "28000") == 0);
    TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "did not prove it knows the user") != NULL);
    TRL_CHECK(test, trl_key_cache_size(cache) == 0U);
    trl_conn_free(conn);
    script_free(&s);
    TRL_CHECK(test, scram_login(ENDING_UNPROVED, cache, &s, &conn, &proof_right) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "the server did not prove it knows the user's verifier") == 0);
    trl_conn_free(conn);
    script_free(&s);
    trl_key_cache_free(cache);
}

/**
 * Verifies: [DESIGN] D9 a null pointer passed in is refused with a status,
 * and a connection's accessors take null.
 */
static void null_arguments_are_refused(trl_test *test) {
    trl_conn *conn = NULL;
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    script s;
    script_init(&s, NULL, NULL);
    trl_stream stream = script_stream(&s);
    TRL_CHECK(test, trl_connect_stream(&stream, "ann", trust, NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_connect_stream(NULL, "ann", trust, NULL, &conn) == TRL_INVALID_ARGUMENT && conn == NULL);
    TRL_CHECK(test, trl_connect_stream(&stream, NULL, trust, NULL, &conn) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_connect(NULL, "ann", trust, NULL, &conn) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_connect(NULL, "ann", trust, NULL, NULL) == TRL_INVALID_ARGUMENT);
    trl_target *target = NULL;
    (void)trl_target_tcp("localhost", TRL_DEFAULT_PORT, &target);
    TRL_CHECK(test, trl_connect(target, NULL, trust, NULL, &conn) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_connect(target, "ann", NULL, NULL, &conn) == TRL_INVALID_ARGUMENT);
    trl_target_free(target);
    trl_stream partial = stream;
    partial.read = NULL;
    TRL_CHECK(test, trl_connect_stream(&partial, "ann", trust, NULL, &conn) == TRL_INVALID_ARGUMENT);
    partial = stream;
    partial.write = NULL;
    TRL_CHECK(test, trl_connect_stream(&partial, "ann", trust, NULL, &conn) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_connect_stream(&stream, "ann", NULL, NULL, &conn) == TRL_INVALID_ARGUMENT);
    partial = stream;
    partial.close = NULL;
    TRL_CHECK(test, trl_connect_stream(&partial, "ann", trust, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_conn_server(conn), "") == 0 && strcmp(trl_conn_authorization(conn), "") == 0);
    trl_conn_free(conn);
    TRL_CHECK(test, trl_conn_failure(NULL) == NULL && trl_conn_is_lost(NULL));
    TRL_CHECK(test, strcmp(trl_conn_server(NULL), "") == 0 && strcmp(trl_conn_authorization(NULL), "") == 0);
    TRL_CHECK(test, !trl_conn_granted(NULL, "ping") && trl_conn_granted_count(NULL) == 0U);
    TRL_CHECK(test, trl_conn_granted_at(NULL, 0U) == NULL && trl_conn_setting(NULL, "X") == NULL);
    TRL_CHECK(test, trl_conn_setting_count(NULL) == 0U && !trl_conn_setting_at(NULL, 0U, NULL, NULL));
    TRL_CHECK(test, trl_conn_set_read_timeout(NULL, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_read_timeout(NULL) == TRL_NO_LIMIT);
    TRL_CHECK(test, trl_conn_set_max_body(NULL, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_conn_ping(NULL) == TRL_INVALID_ARGUMENT && trl_conn_terminate(NULL) == TRL_INVALID_ARGUMENT);
    trl_conn_free(NULL);
    script_free(&s);
    trl_mechanism_free(trust);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_trusted_login_keeps_what_ready_told),     TRL_TEST_CASE(granted_capabilities_are_the_known),
        TRL_TEST_CASE(a_refusal_or_a_reply_out_of_turn_fails),    TRL_TEST_CASE(scram_logs_in_and_keeps_its_keys),
        TRL_TEST_CASE(scram_is_refused_unless_the_server_proves), TRL_TEST_CASE(null_arguments_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
