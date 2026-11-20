/**
 * @file scram_test.c
 * @brief SCRAM-SHA-256's client exchange [PROTOCOL Mechanisms]: RFC
 * 7677's example, step by step, the channel's GS2 header bound into the
 * proof, and each refusal turborel's own client makes.
 */
#include "auth/crypto.h"
#include "auth/scram.h"
#include "codec/buffer.h"
#include "failure.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* RFC 7677 section 3's example. */
#define USER "user"
#define PASSWORD "pencil"
#define NONCE "rOprNGfwEbeRWgbNEkqO"
#define SERVER_FIRST "r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,s=W22ZaJ0SNY7soEsUEjb6gQ==,i=4096"
#define CLIENT_FINAL                                                                                                   \
    "c=biws,r=rOprNGfwEbeRWgbNEkqO%hvYDpWUa2RaTCAfuxFIlj)hNlF$k0,p=dHzbZapWIk4jUhN+Ute9ytag9zjfMHgsqmmiz7AndVQ="
#define SERVER_FINAL "v=6rriTRBi23WpRR/wtup+mMhUZUn/dB5nLTJRsjl95G4="
#define ITERATIONS 4096U

/* A view of a null-ended string. */
static trl_slice text(const char *string) {
    trl_slice slice = {(const uint8_t *)string, strlen(string)};
    return slice;
}

/* Whether `buffer` holds `expected`, exactly. */
static bool holds(const trl_buffer *buffer, const char *expected) {
    return buffer->length == strlen(expected) && memcmp(buffer->data, expected, buffer->length) == 0;
}

/* Begins an exchange unbound, its client-first sent with the example's nonce. */
static void begin(trl_scram *scram, trl_buffer *first) {
    trl_scram_init(scram, TRL_SCRAM_UNBOUND, NULL, 0U);
    trl_buffer_init(first);
    (void)trl_scram_client_first(scram, USER, NONCE, first);
}

/* Whether reading `server_first` is refused with SQLSTATE 28000 and `why`. */
static bool first_refused(const char *server_first, const char *why) {
    trl_scram scram;
    trl_buffer first;
    begin(&scram, &first);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_scram_first read;
    bool refused = !trl_scram_read_first(&scram, text(server_first), &read, &failure) && failure.sqlcode == -919 &&
                   strcmp(failure.sqlstate, "28000") == 0 && strstr(failure.message, why) != NULL;
    trl_failure_clear(&failure);
    trl_buffer_free(&first);
    trl_scram_free(&scram);
    return refused;
}

/**
 * Verifies: [PROTOCOL Mechanisms] RFC 7677's example: client-first, the
 * server-first read, client-final proved by the password's keys, and
 * server-final's signature verified.
 */
static void the_rfc_example_runs_step_by_step(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_scram scram;
    trl_buffer first;
    begin(&scram, &first);
    TRL_CHECK(test, holds(&first, "n,,n=user,r=" NONCE));
    trl_failure failure;
    trl_failure_init(&failure);
    trl_scram_first read;
    TRL_CHECK(test, trl_scram_read_first(&scram, text(SERVER_FIRST), &read, &failure));
    TRL_CHECK(test, read.iterations == ITERATIONS && read.salt_length == 16U);
    trl_salt salt_of_read = trl_scram_salt(&read);
    trl_scram_keys keys;
    TRL_CHECK(test, trl_scram_derive(PASSWORD, &salt_of_read, &keys));
    trl_buffer final;
    trl_buffer_init(&final);
    TRL_CHECK(test, trl_scram_prove(&scram, &read, &keys, &final));
    TRL_CHECK(test, holds(&final, CLIENT_FINAL));
    TRL_CHECK(test, !trl_scram_verified(&scram));
    TRL_CHECK(test, trl_scram_verify(&scram, text(SERVER_FINAL), &failure));
    TRL_CHECK(test, trl_scram_verified(&scram));
    trl_scram_first_free(&read);
    trl_buffer_free(&final);
    trl_buffer_free(&first);
    trl_scram_free(&scram);
    trl_failure_clear(&failure);
}

/**
 * Verifies: [PROTOCOL Mechanisms] a user's name is a saslname, `=` and `,`
 * escaped, and each binding's GS2 header leads client-first.
 */
static void client_first_names_its_binding(trl_test *test) {
    static const trl_scram_channel channels[] = {
        TRL_SCRAM_UNBOUND,
        TRL_SCRAM_UNOFFERED,
        TRL_SCRAM_END_POINT,
        TRL_SCRAM_EXPORTER,
    };
    static const char *const expected[] = {
        "n,,n=a=3Db=2Cc,r=x",
        "y,,n=a=3Db=2Cc,r=x",
        "p=tls-server-end-point,,n=a=3Db=2Cc,r=x",
        "p=tls-exporter,,n=a=3Db=2Cc,r=x",
    };
    static const uint8_t binding[] = {1, 2, 3};
    for (size_t i = 0U; i < sizeof channels / sizeof channels[0]; ++i) {
        trl_scram scram;
        trl_scram_init(&scram, channels[i], binding, sizeof binding);
        trl_buffer first;
        trl_buffer_init(&first);
        TRL_CHECK(test, trl_scram_client_first(&scram, "a=b,c", "x", &first));
        TRL_CHECK(test, holds(&first, expected[i]));
        TRL_CHECK(test, trl_scram_is_bound(&scram) == (i >= 2U));
        trl_buffer_free(&first);
        trl_scram_free(&scram);
    }
}

/**
 * Verifies: [PROTOCOL Mechanisms] a bound exchange's client-final carries
 * its GS2 header and the binding's data in `c=`, base 64.
 */
static void a_bound_proof_carries_its_channel(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    static const uint8_t exported[] = {'A', 'B', 'C'};
    trl_scram scram;
    trl_scram_init(&scram, TRL_SCRAM_EXPORTER, exported, sizeof exported);
    trl_buffer first;
    trl_buffer_init(&first);
    (void)trl_scram_client_first(&scram, "", NONCE, &first);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_scram_first read;
    TRL_CHECK(test, trl_scram_read_first(&scram, text(SERVER_FIRST), &read, &failure));
    trl_scram_keys keys;
    memset(&keys, 0, sizeof keys);
    trl_buffer final;
    trl_buffer_init(&final);
    TRL_CHECK(test, trl_scram_prove(&scram, &read, &keys, &final));
    /* "p=tls-exporter,,ABC" in base 64. */
    static const char channel[] = "c=cD10bHMtZXhwb3J0ZXIsLEFCQw==,r=";
    TRL_CHECK(test, final.length > sizeof channel && memcmp(final.data, channel, sizeof channel - 1U) == 0);
    trl_scram_first_free(&read);
    trl_buffer_free(&final);
    trl_buffer_free(&first);
    trl_scram_free(&scram);
    trl_failure_clear(&failure);
}

/**
 * Verifies: [PROTOCOL Mechanisms] a server-first missing an attribute,
 * of another, of more than three, of a nonce not extending the client's,
 * of a salt not base 64 or of iterations not a positive u32, is refused
 * (SQLSTATE 28000), as turborel's client refuses it.
 */
static void a_malformed_server_first_is_refused(trl_test *test) {
    TRL_CHECK(test, first_refused("r=" NONCE "x", "the attribute s is missing"));
    TRL_CHECK(test, first_refused("x=" NONCE "x,s=QQ==,i=1", "the attribute r was expected, not x="));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=QQ==,i=1,m=1", "has more than it should"));
    TRL_CHECK(test, first_refused("r=" NONCE ",s=QQ==,i=1", "does not extend the client's"));
    TRL_CHECK(test, first_refused("r=other,s=QQ==,i=1", "does not extend the client's"));
    TRL_CHECK(test, first_refused("r=" NONCE "x y,s=QQ==,i=1", "does not extend the client's"));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=Q,i=1", "the salt is not base 64"));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=QQ==,i=0", "the iterations are no positive number"));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=QQ==,i=4294967296", "the iterations are no positive number"));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=QQ==,i=+", "the iterations are no positive number"));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=QQ==,i=1a", "the iterations are no positive number"));
    TRL_CHECK(test, first_refused("r=" NONCE "\xff,s=QQ==,i=1", "a message is not UTF-8"));
    TRL_CHECK(test, first_refused("r", "the attribute r was expected, not r"));
    TRL_CHECK(test, first_refused("r=" NONCE "\x7f,s=QQ==,i=1", "does not extend the client's"));
    TRL_CHECK(test, first_refused("r=" NONCE "x,s=QQ==,x=1", "the attribute i was expected"));
    trl_scram scram;
    trl_buffer first;
    begin(&scram, &first);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_scram_first read;
    TRL_CHECK(test, trl_scram_read_first(&scram, text("r=" NONCE "x,s=QQ==,i=+4294967295"), &read, &failure));
    TRL_CHECK(test, read.iterations == UINT32_MAX);
    trl_scram_first_free(&read);
    trl_buffer_free(&first);
    trl_scram_free(&scram);
    trl_failure_clear(&failure);
}

/* Whether verifying `server_final`, after the example's proof, is refused
   with `why`. */
static bool final_refused(const char *server_final, const char *why) {
    trl_scram scram;
    trl_buffer first;
    begin(&scram, &first);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_scram_first read;
    trl_scram_keys keys;
    trl_buffer final;
    trl_buffer_init(&final);
    bool read_it = trl_scram_read_first(&scram, text(SERVER_FIRST), &read, &failure);
    trl_salt salt_of_read = trl_scram_salt(&read);
    bool proved =
        read_it && trl_scram_derive(PASSWORD, &salt_of_read, &keys) && trl_scram_prove(&scram, &read, &keys, &final);
    bool refused = proved && !trl_scram_verify(&scram, text(server_final), &failure) &&
                   strcmp(failure.sqlstate, "28000") == 0 && strstr(failure.message, why) != NULL &&
                   !trl_scram_verified(&scram);
    trl_scram_first_free(&read);
    trl_buffer_free(&final);
    trl_buffer_free(&first);
    trl_scram_free(&scram);
    trl_failure_clear(&failure);
    return refused;
}

/**
 * Verifies: [PROTOCOL Mechanisms] the server's signature is checked
 * before the last, empty Answer: a wrong one, one not base 64, an error
 * the server gives, or a message out of turn, is refused.
 */
static void the_servers_signature_is_checked(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    TRL_CHECK(test, final_refused("v=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", "did not prove it knows the user"));
    TRL_CHECK(test, final_refused("v=AAAA", "did not prove it knows the user"));
    TRL_CHECK(test, final_refused("v=A", "the server's signature is not base 64"));
    TRL_CHECK(test, final_refused("e=invalid-proof", "the server refused: invalid-proof"));
    TRL_CHECK(test, final_refused("x=1", "the attribute v was expected"));
    trl_scram scram;
    trl_scram_init(&scram, TRL_SCRAM_UNBOUND, NULL, 0U);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_scram_first read;
    TRL_CHECK(test, !trl_scram_read_first(&scram, text(SERVER_FIRST), &read, &failure));
    TRL_CHECK(test, strstr(failure.message, "the server's first message came out of turn") != NULL);
    TRL_CHECK(test, !trl_scram_verify(&scram, text(SERVER_FINAL), &failure));
    TRL_CHECK(test, strstr(failure.message, "the server's final message came out of turn") != NULL);
    trl_scram_free(&scram);
    trl_failure_clear(&failure);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_rfc_example_runs_step_by_step), TRL_TEST_CASE(client_first_names_its_binding),
        TRL_TEST_CASE(a_bound_proof_carries_its_channel), TRL_TEST_CASE(a_malformed_server_first_is_refused),
        TRL_TEST_CASE(the_servers_signature_is_checked),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
