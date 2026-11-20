/**
 * @file tls_test.c
 * @brief TLS settings and handshakes without a server [CLIENT Connecting]:
 * the authorities of a file, the platform's, a certificate presented,
 * each failing as turborel-client fails, and a handshake with a server
 * that speaks no TLS.
 */
#ifndef __APPLE__
/* POSIX's setenv, which C17 alone does not declare; the feature macro is
   the C library's name, reserved and so spelled. */
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "certs.h"
#include "fake.h"
#include "harness.h"
#include "turborel/turborel.h"

/* The platform's and the TLS library's headers declare more than the
   include checker can map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <arpa/inet.h> // IWYU pragma: keep
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_BYTES 1024U
/* The bytes `tls-exporter` exports. */
#define TRL_EXPORTER_BYTES 32U

/* The directory the test's certificates are made in. */
#define DIR "tls-test"

/* Whether the settings failed with a message beginning `prefix`. */
static bool failed_with(const trl_tls *tls, const char *prefix) {
    return strncmp(trl_failure_message(trl_tls_failure(tls)), prefix, strlen(prefix)) == 0;
}

/**
 * Verifies: [CLIENT Connecting] TLS trusts a PEM file's authorities: one
 * not there cannot be read, one of no certificate is refused; a chain and
 * key presented must be read, and fit (SQLSTATE 08006).
 */
static void files_are_read_or_refused(trl_test *test) {
    trl_tls *tls = NULL;
    TRL_CHECK(test, trl_tls_new(&tls) == TRL_OK);
    TRL_CHECK(test, trl_tls_trust_file(tls, DIR "/missing.pem") == TRL_FAILED);
    TRL_CHECK(test, failed_with(tls, "cannot read " DIR "/missing.pem: "));
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_tls_failure(tls)), "08006") == 0);
    FILE *empty = fopen(DIR "/empty.pem", "w");
    if (empty != NULL) {
        (void)fputs("no certificate here\n", empty);
        (void)fclose(empty);
    }
    TRL_CHECK(test, trl_tls_trust_file(tls, DIR "/empty.pem") == TRL_FAILED);
    TRL_CHECK(test, failed_with(tls, DIR "/empty.pem holds no certificate"));
    TRL_CHECK(test, trl_tls_present(tls, DIR "/missing.pem", DIR "/client.key") == TRL_FAILED);
    TRL_CHECK(test, failed_with(tls, "cannot read " DIR "/missing.pem"));
    TRL_CHECK(test, trl_tls_present(tls, DIR "/client.pem", DIR "/missing.key") == TRL_FAILED);
    TRL_CHECK(test, failed_with(tls, "cannot read " DIR "/missing.key"));
    TRL_CHECK(test, trl_tls_present(tls, DIR "/client.pem", DIR "/server.key") == TRL_FAILED);
    TRL_CHECK(test, trl_tls_present(tls, DIR "/client.pem", DIR "/client.key") == TRL_OK);
    TRL_CHECK(test, trl_tls_trust_file(tls, DIR "/ca.pem") == TRL_OK);
    trl_tls_free(tls);
}

/**
 * Verifies: [CLIENT Connecting] TLS that trusts no authority makes no
 * target; settings a target holds change no more; the platform's
 * authorities are trusted when asked for, those of the files SSL_CERT_FILE
 * and SSL_CERT_DIR name where either is set, as rustls-native-certs reads
 * them: SSL_CERT_DIR a list, a directory that cannot be read passed over,
 * none at all trusted a failure.
 */
static void a_target_needs_an_authority(trl_test *test) {
    trl_tls *tls = NULL;
    (void)trl_tls_new(&tls);
    trl_target *target = NULL;
    TRL_CHECK(test, trl_target_tls("localhost", TRL_DEFAULT_PORT, NULL, tls, &target) == TRL_FAILED);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_target_failure(target)), "no certificate authority is trusted",
                            35U) == 0);
    trl_target_free(target);
    TRL_CHECK(test, trl_tls_trust_platform(tls) == TRL_OK);
    /* The test runs on one thread here. */
    TRL_CHECK(test, setenv("SSL_CERT_FILE", "", 1) == 0); // NOLINT(concurrency-mt-unsafe)
    trl_tls *none = NULL;
    (void)trl_tls_new(&none);
    TRL_CHECK(test, trl_tls_trust_platform(none) == TRL_FAILED && failed_with(none, "cannot trust the platform's"));
    trl_tls_free(none);
    /* A directory within it, a file of no certificate and a dangling link
       are passed over; the certificates its files hold are trusted. */
    (void)mkdir(DIR "/within", 0700);
    TRL_CHECK(test, trl_test_write_file(DIR "/notes.txt", "no certificate here\n"));
    (void)unlink(DIR "/gone.pem");
    TRL_CHECK(test, symlink(DIR "/nothing", DIR "/gone.pem") == 0);
    TRL_CHECK(test, setenv("SSL_CERT_DIR", "nowhere::" DIR, 1) == 0); // NOLINT(concurrency-mt-unsafe)
    TRL_CHECK(test, trl_tls_trust_platform(tls) == TRL_OK);
    TRL_CHECK(test, setenv("SSL_CERT_FILE", DIR "/ca.pem", 1) == 0); // NOLINT(concurrency-mt-unsafe)
    TRL_CHECK(test, setenv("SSL_CERT_DIR", DIR "/within", 1) == 0);  // NOLINT(concurrency-mt-unsafe)
    trl_tls *file = NULL;
    (void)trl_tls_new(&file);
    TRL_CHECK(test, trl_tls_trust_platform(file) == TRL_OK);
    trl_tls_free(file);
    TRL_CHECK(test, unsetenv("SSL_CERT_FILE") == 0 && unsetenv("SSL_CERT_DIR") == 0); // NOLINT(concurrency-mt-unsafe)
    TRL_CHECK(test, trl_target_tls("localhost", TRL_DEFAULT_PORT, NULL, tls, &target) == TRL_OK);
    TRL_CHECK(test, trl_tls_trust_file(tls, DIR "/ca.pem") == TRL_FAILED);
    TRL_CHECK(test, trl_failure_sqlcode(trl_tls_failure(tls)) == -910);
    TRL_CHECK(test, trl_tls_trust_platform(tls) == TRL_FAILED && trl_tls_present(tls, "a", "b") == TRL_FAILED);
    trl_tls_free(tls);
    trl_target_free(target);
}

/**
 * Verifies: [CLIENT Connecting] a TLS handshake with a server that closes
 * at once fails (SQLSTATE 08006); a server's name that is no DNS name
 * nor address is refused.
 */
static void a_handshake_fails_as_it_should(trl_test *test) {
    trl_tls *tls = NULL;
    (void)trl_tls_new(&tls);
    (void)trl_tls_trust_file(tls, DIR "/ca.pem");
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_CLOSE, 16, NULL));
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_target_tls("127.0.0.1", f.port, NULL, tls, &target) == TRL_OK);
    TRL_CHECK(test, trl_connect(target, "ann", trust, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_conn_failure(conn)), "the TLS handshake failed: ", 26U) == 0);
    trl_conn_free(conn);
    trl_target_free(target);
    TRL_CHECK(test, trl_target_tls("127.0.0.1", f.port, "not a name!", tls, &target) == TRL_OK);
    TRL_CHECK(test, trl_connect(target, "ann", trust, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "not a name! is no server's name: invalid dns name") == 0);
    trl_conn_free(conn);
    trl_target_free(target);
    static const char *const named[] = {"Db_1.Example.", "a-b.example", "x"};
    for (size_t i = 0U; i < sizeof named / sizeof named[0]; ++i) {
        (void)trl_target_tls("127.0.0.1", f.port, named[i], tls, &target);
        TRL_CHECK(test, trl_connect(target, "ann", trust, NULL, &conn) == TRL_FAILED);
        TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "invalid dns name") == NULL);
        trl_conn_free(conn);
        trl_target_free(target);
    }
    char long_label[80];
    memset(long_label, 'a', sizeof long_label - 1U);
    long_label[sizeof long_label - 1U] = '\0';
    char long_name[300];
    memset(long_name, 'a', sizeof long_name - 1U);
    for (size_t i = 50U; i < sizeof long_name - 1U; i += 50U) {
        long_name[i] = '.';
    }
    long_name[sizeof long_name - 1U] = '\0';
    const char *const unnamed[] = {"-a.example", "a..example", "a-.example", ".", "", "a.b!c", long_label, long_name};
    for (size_t i = 0U; i < sizeof unnamed / sizeof unnamed[0]; ++i) {
        (void)trl_target_tls("127.0.0.1", f.port, unnamed[i], tls, &target);
        TRL_CHECK(test, trl_connect(target, "ann", trust, NULL, &conn) == TRL_FAILED);
        TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "invalid dns name") != NULL);
        trl_conn_free(conn);
        trl_target_free(target);
    }
    fake_stop(&f);
    trl_mechanism_free(trust);
    trl_tls_free(tls);
}

/* Reads a u32, little-endian. */
static uint32_t u32_at(const uint8_t *at) {
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8U) | ((uint32_t)at[2] << 16U) | ((uint32_t)at[3] << 24U);
}

/* Whether the fake's Hello named `mechanism`, its first bytes beginning `initial`. */
static bool hello_was(const fake *f, const char *mechanism, const char *initial) {
    size_t at = 4U;
    at += 4U + u32_at(f->hello + at);
    uint32_t named = u32_at(f->hello + at);
    bool same = named == strlen(mechanism) && memcmp(f->hello + at + 4U, mechanism, named) == 0;
    at += 4U + named;
    uint32_t length = u32_at(f->hello + at);
    return same && length >= strlen(initial) && memcmp(f->hello + at + 4U, initial, strlen(initial)) == 0;
}

/* Connects over TLS to a fake of TLS 1.2 at most if `tls12`, by `mechanism`. */
static trl_status over_tls(bool tls12, trl_mechanism *mechanism, fake *f, trl_conn **conn) {
    trl_tls *tls = NULL;
    (void)trl_tls_new(&tls);
    (void)trl_tls_trust_file(tls, DIR "/ca.pem");
    trl_target *target = NULL;
    trl_status made = TRL_FAILED;
    if (fake_start_tls(f, FAKE_READY, 1, DIR, tls12) &&
        trl_target_tls("127.0.0.1", f->port, "localhost", tls, &target) == TRL_OK) {
        made = trl_connect(target, "ann", mechanism, NULL, conn);
    }
    trl_target_free(target);
    trl_tls_free(tls);
    return made;
}

/**
 * Verifies: [CLIENT Connecting] over TLS, of 1.3 or 1.2, a session runs:
 * Ping, and Terminate with TLS's closing message.
 */
static void a_session_runs_over_tls(trl_test *test) {
    for (int tls12 = 0; tls12 <= 1; ++tls12) {
        trl_mechanism *trust = NULL;
        (void)trl_mechanism_trust(&trust);
        fake f;
        trl_conn *conn = NULL;
        TRL_CHECK(test, over_tls(tls12 == 1, trust, &f, &conn) == TRL_OK);
        TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK && trl_conn_terminate(conn) == TRL_OK);
        trl_conn_free(conn);
        fake_stop(&f);
        trl_mechanism_free(trust);
    }
}

/**
 * Verifies: [PROTOCOL Mechanisms] over TLS 1.3, SCRAM-SHA-256-PLUS names
 * `tls-exporter` in its GS2 header; over TLS 1.2, `tls-server-end-point`.
 */
static void scram_names_its_binding_over_tls(trl_test *test) {
    static const char *const headers[] = {"p=tls-exporter,,n=,r=", "p=tls-server-end-point,,n=,r="};
    for (int tls12 = 0; tls12 <= 1; ++tls12) {
        trl_mechanism *scram = NULL;
        (void)trl_mechanism_scram("ann's", &scram);
        fake f;
        trl_conn *conn = NULL;
        TRL_CHECK(test, over_tls(tls12 == 1, scram, &f, &conn) == TRL_FAILED);
        TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                               "the server did not prove it knows the user's verifier") == 0);
        trl_conn_free(conn);
        fake_stop(&f);
        TRL_CHECK(test, hello_was(&f, "scram-sha-256-plus", headers[tls12]));
        trl_mechanism_free(scram);
    }
}

/* A TLS stream of the test's own: its socket and the TLS library's connection. */
typedef struct own_stream {
    int fd;
    SSL *ssl;
} own_stream;

static int64_t own_read(void *context, uint8_t *into, size_t capacity) {
    own_stream *own = context;
    int got = SSL_read(own->ssl, into, (int)capacity);
    return got > 0 ? (int64_t)got : 0;
}

static int64_t own_write(void *context, const uint8_t *bytes, size_t length) {
    own_stream *own = context;
    int wrote = SSL_write(own->ssl, bytes, (int)length);
    return wrote > 0 ? (int64_t)wrote : -1;
}

static void own_close(void *context) {
    own_stream *own = context;
    SSL_free(own->ssl);
    (void)close(own->fd);
}

/* Opens a TLS stream of the test's own to the fake, trusting nothing. */
static bool own_open(const fake *f, SSL_CTX *context, own_stream *own) {
    own->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(f->port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    own->ssl = SSL_new(context);
    return own->fd >= 0 && connect(own->fd, (const struct sockaddr *)&address, sizeof address) == 0 &&
           own->ssl != NULL && SSL_set_fd(own->ssl, own->fd) == 1 && SSL_connect(own->ssl) == 1;
}

/**
 * Verifies: [CLIENT Connecting] a TLS stream the program opened, bound by
 * trl_mechanism_bind with the bytes it exported, carries
 * SCRAM-SHA-256-PLUS by `tls-exporter`.
 */
static void a_programs_own_tls_stream_is_bound(trl_test *test) {
    fake f;
    TRL_CHECK(test, fake_start_tls(&f, FAKE_READY, 1, DIR, false));
    SSL_CTX *context = SSL_CTX_new(TLS_client_method());
    own_stream own = {-1, NULL};
    TRL_CHECK(test, context != NULL && own_open(&f, context, &own));
    uint8_t exported[TRL_EXPORTER_BYTES];
    static const char label[] = "EXPORTER-Channel-Binding";
    TRL_CHECK(test, SSL_export_keying_material(own.ssl, exported, sizeof exported, label, sizeof label - 1U,
                                               (const unsigned char *)"", 0U, 1) == 1);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram("ann's", &scram);
    TRL_CHECK(test, trl_mechanism_bind(scram, NULL, 0U, exported, sizeof exported) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_mechanism_name(scram), "scram-sha-256-plus") == 0);
    trl_stream stream = {&own, own_read, own_write, own_close, NULL};
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_connect_stream(&stream, "ann", scram, NULL, &conn) == TRL_FAILED);
    trl_conn_free(conn);
    fake_stop(&f);
    TRL_CHECK(test, hello_was(&f, "scram-sha-256-plus", "p=tls-exporter,,n=,r="));
    trl_mechanism_free(scram);
    SSL_CTX_free(context);
}

/**
 * Verifies: [DESIGN] D9 a null pointer passed in is refused with a status.
 */
static void null_arguments_are_refused(trl_test *test) {
    trl_tls *tls = NULL;
    trl_target *target = NULL;
    TRL_CHECK(test, trl_tls_new(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_tls_trust_file(NULL, "x") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_tls_trust_platform(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_tls_present(NULL, "x", "y") == TRL_INVALID_ARGUMENT);
    (void)trl_tls_new(&tls);
    TRL_CHECK(test, trl_tls_trust_file(tls, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_tls_present(tls, NULL, "y") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_tls_present(tls, "x", NULL) == TRL_INVALID_ARGUMENT);
    trl_tls_free(tls);
    tls = NULL;
    TRL_CHECK(test, trl_tls_failure(NULL) == NULL && trl_target_failure(NULL) == NULL);
    TRL_CHECK(test, trl_target_tls("h", 1U, NULL, NULL, &target) == TRL_INVALID_ARGUMENT && target == NULL);
    TRL_CHECK(test, trl_target_tls("h", 1U, NULL, tls, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_target_tcp("", 1U, &target) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_target_tcp("h", 1U, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_target_local(NULL, &target) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, !trl_target_is_tls(NULL) && !trl_target_is_plain_tcp(NULL) && trl_target_host(NULL) == NULL);
    TRL_CHECK(test, trl_conn_target(NULL) == NULL);
    TRL_CHECK(test, trl_target_local("x.sock", &target) == TRL_OK && trl_target_host(target) == NULL);
    TRL_CHECK(test, !trl_target_is_tls(target) && !trl_target_is_plain_tcp(target));
    trl_target_free(target);
    TRL_CHECK(test, trl_target_tcp("h", 1U, &target) == TRL_OK && strcmp(trl_target_host(target), "h") == 0);
    TRL_CHECK(test, trl_target_is_plain_tcp(target) && !trl_target_is_tls(target));
    trl_target_free(target);
    (void)trl_tls_new(&tls);
    (void)trl_tls_trust_file(tls, DIR "/ca.pem");
    TRL_CHECK(test, trl_target_tls("t", 1U, NULL, tls, &target) == TRL_OK && strcmp(trl_target_host(target), "t") == 0);
    TRL_CHECK(test, trl_target_is_tls(target) && !trl_target_is_plain_tcp(target));
    trl_target_free(target);
    trl_tls_free(tls);
    trl_tls_free(NULL);
    trl_target_free(NULL);
}

int main(int argc, char **argv) {
    /* The directory is made by CMake as the test is set up. */
    if (!certs_make(DIR)) {
        return 1;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(files_are_read_or_refused),        TRL_TEST_CASE(a_target_needs_an_authority),
        TRL_TEST_CASE(a_handshake_fails_as_it_should),   TRL_TEST_CASE(a_session_runs_over_tls),
        TRL_TEST_CASE(scram_names_its_binding_over_tls), TRL_TEST_CASE(a_programs_own_tls_stream_is_bound),
        TRL_TEST_CASE(null_arguments_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
// NOLINTEND(misc-include-cleaner)
