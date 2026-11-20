/**
 * @file mechanism_test.c
 * @brief The mechanisms [PROTOCOL Mechanisms] [CLIENT Connecting]: their
 * names and first bytes, which are secret, SCRAM bound as its channel
 * allows, and its checks of the server.
 */
#include "auth/crypto.h"
#include "auth/mechanism.h"
#include "auth/scram.h"
#include "codec/buffer.h"
#include "failure.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A certificate signed by ECDSA with SHA-256, as a binding reads it. */
static const uint8_t certificate[] = {
    0x30, 0x14, 0x30, 0x03, 0x02, 0x01, 0x07, 0x30, 0x0a, 0x06, 0x08,
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03, 0x01, 0x00,
};
/* A certificate of Ed25519, which defines no binding. */
static const uint8_t ed25519[] = {
    0x30, 0x0f, 0x30, 0x03, 0x02, 0x01, 0x07, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x01, 0x00,
};

/* The mechanism's first bytes, as text, into `out`. */
static bool initial_is(trl_mechanism *mechanism, const char *expected) {
    trl_buffer out;
    trl_buffer_init(&out);
    trl_failure failure;
    trl_failure_init(&failure);
    bool same = trl_mechanism_initial(mechanism, &out, &failure) && out.length == strlen(expected) &&
                (out.length == 0U || memcmp(out.data, expected, out.length) == 0);
    trl_buffer_free(&out);
    trl_failure_clear(&failure);
    return same;
}

/* Whether the mechanism's first bytes begin with `prefix`. */
static bool initial_begins(trl_mechanism *mechanism, const char *prefix) {
    trl_buffer out;
    trl_buffer_init(&out);
    trl_failure failure;
    trl_failure_init(&failure);
    bool begins = trl_mechanism_initial(mechanism, &out, &failure) && out.length > strlen(prefix) &&
                  memcmp(out.data, prefix, strlen(prefix)) == 0;
    trl_buffer_free(&out);
    trl_failure_clear(&failure);
    return begins;
}

/**
 * Verifies: [PROTOCOL Mechanisms] `trust` and `certificate` send nothing
 * of their own and answer no challenge; `password` sends the password's
 * UTF-8, and is the secret never sent over plain TCP.
 */
static void trust_password_and_certificate(trl_test *test) {
    trl_mechanism *trust = NULL;
    trl_mechanism *password = NULL;
    trl_mechanism *certificate_one = NULL;
    TRL_CHECK(test, trl_mechanism_trust(&trust) == TRL_OK);
    TRL_CHECK(test, trl_mechanism_password("ann's", &password) == TRL_OK);
    TRL_CHECK(test, trl_mechanism_certificate(&certificate_one) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_mechanism_name(trust), "trust") == 0 && initial_is(trust, ""));
    TRL_CHECK(test, strcmp(trl_mechanism_name(password), "password") == 0 && initial_is(password, "ann's"));
    TRL_CHECK(test, strcmp(trl_mechanism_name(certificate_one), "certificate") == 0 && initial_is(certificate_one, ""));
    TRL_CHECK(test, !trl_mechanism_secret(trust) && trl_mechanism_secret(password));
    TRL_CHECK(test, !trl_mechanism_secret(certificate_one));
    trl_failure failure;
    trl_failure_init(&failure);
    trl_buffer out;
    trl_buffer_init(&out);
    trl_slice challenge = {(const uint8_t *)"x", 1U};
    TRL_CHECK(test, !trl_mechanism_answer(trust, challenge, &out, &failure));
    TRL_CHECK(test, strcmp(failure.message, "trust has no answer to a challenge") == 0);
    TRL_CHECK(test, strcmp(failure.sqlstate, "08006") == 0);
    TRL_CHECK(test, trl_mechanism_ready(password, &failure));
    TRL_CHECK(test, trl_mechanism_scram_in_the_clear(password) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_bind(trust, certificate, sizeof certificate, NULL, 0U) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_mechanism_name(trust), "trust") == 0);
    trl_buffer_free(&out);
    trl_failure_clear(&failure);
    trl_mechanism_free(trust);
    trl_mechanism_free(password);
    trl_mechanism_free(certificate_one);
}

/**
 * Verifies: [CLIENT Connecting] SCRAM over TLS is SCRAM-SHA-256-PLUS:
 * bound by the exporter where it is had, else by the server's
 * certificate, else saying it could bind; with binding disabled, or not
 * over TLS, it is SCRAM-SHA-256, unbound.
 */
static void scram_binds_as_its_channel_allows(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_mechanism *scram = NULL;
    TRL_CHECK(test, trl_mechanism_scram("ann's", &scram) == TRL_OK);
    TRL_CHECK(test, trl_mechanism_secret(scram));
    TRL_CHECK(test, strcmp(trl_mechanism_name(scram), "scram-sha-256") == 0 && initial_begins(scram, "n,,n=,r="));
    static const uint8_t exported[TRL_SCRAM_EXPORTER_BYTES] = {0};
    TRL_CHECK(test, trl_mechanism_bind(scram, certificate, sizeof certificate, exported, sizeof exported) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_mechanism_name(scram), "scram-sha-256-plus") == 0);
    TRL_CHECK(test, initial_begins(scram, "p=tls-exporter,,n=,r="));
    TRL_CHECK(test, strcmp(trl_mechanism_name(scram), "scram-sha-256") == 0);
    TRL_CHECK(test, trl_mechanism_bind(scram, certificate, sizeof certificate, NULL, 0U) == TRL_OK);
    TRL_CHECK(test, initial_begins(scram, "p=tls-server-end-point,,n=,r="));
    static const uint8_t too_long[TRL_SCRAM_EXPORTER_BYTES * 3U] = {0};
    TRL_CHECK(test, trl_mechanism_bind(scram, NULL, 0U, too_long, sizeof too_long) == TRL_OK);
    TRL_CHECK(test, initial_begins(scram, "y,,n=,r="));
    TRL_CHECK(test, trl_mechanism_bind(scram, NULL, 0U, exported, 0U) == TRL_OK);
    TRL_CHECK(test, initial_begins(scram, "y,,n=,r="));
    TRL_CHECK(test, trl_mechanism_bind(scram, ed25519, sizeof ed25519, NULL, 0U) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_mechanism_name(scram), "scram-sha-256") == 0 && initial_begins(scram, "y,,n=,r="));
    TRL_CHECK(test, trl_mechanism_scram_channel_binding(scram, TRL_CHANNEL_BINDING_DISABLE) == TRL_OK);
    TRL_CHECK(test, trl_mechanism_bind(scram, certificate, sizeof certificate, exported, sizeof exported) == TRL_OK);
    TRL_CHECK(test, initial_begins(scram, "n,,n=,r="));
    /* A value of none of the three, as a program may pass by mistake. */
    trl_channel_binding unknown = (trl_channel_binding)7; // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
    TRL_CHECK(test, trl_mechanism_scram_channel_binding(scram, unknown) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_scram_in_the_clear(scram) == TRL_OK && !trl_mechanism_secret(scram));
    trl_mechanism_free(scram);
}

/**
 * Verifies: [CLIENT Connecting] SCRAM refuses a connection not bound when
 * binding is required (SQLSTATE 28000), and a Ready before the server
 * proved it knows the user's verifier (SQLSTATE 08006).
 */
static void scram_checks_the_server(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_mechanism *scram = NULL;
    TRL_CHECK(test, trl_mechanism_scram("ann's", &scram) == TRL_OK);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_buffer out;
    trl_buffer_init(&out);
    trl_slice first = {(const uint8_t *)"r=x,s=QQ==,i=1", 14U};
    TRL_CHECK(test, !trl_mechanism_answer(scram, first, &out, &failure));
    TRL_CHECK(test, strcmp(failure.message, "SCRAM was not begun") == 0);
    TRL_CHECK(test, !trl_mechanism_ready(scram, &failure));
    TRL_CHECK(test, strcmp(failure.message, "the server did not prove it knows the user's verifier") == 0);
    TRL_CHECK(test, strcmp(failure.sqlstate, "08006") == 0);
    TRL_CHECK(test, trl_mechanism_scram_channel_binding(scram, TRL_CHANNEL_BINDING_REQUIRE) == TRL_OK);
    TRL_CHECK(test, initial_begins(scram, "n,,"));
    TRL_CHECK(test, !trl_mechanism_answer(scram, first, &out, &failure));
    TRL_CHECK(test, strcmp(failure.sqlstate, "28000") == 0);
    TRL_CHECK(test, strstr(failure.message, "channel binding is required") != NULL);
    trl_mechanism_refused(scram);
    trl_buffer_free(&out);
    trl_failure_clear(&failure);
    trl_mechanism_free(scram);
}

/**
 * Verifies: [DESIGN] D9 a null handle or pointer passed in is refused with
 * a status, never followed.
 */
static void null_arguments_are_refused(trl_test *test) {
    trl_mechanism *mechanism = NULL;
    TRL_CHECK(test, trl_mechanism_trust(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_password(NULL, &mechanism) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_scram(NULL, &mechanism) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_scram_key_cache(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_mechanism_bind(NULL, NULL, 0U, NULL, 0U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, strcmp(trl_mechanism_name(NULL), "") == 0 && trl_mechanism_failure(NULL) == NULL);
    trl_mechanism_free(NULL);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(trust_password_and_certificate),
        TRL_TEST_CASE(scram_binds_as_its_channel_allows),
        TRL_TEST_CASE(scram_checks_the_server),
        TRL_TEST_CASE(null_arguments_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
