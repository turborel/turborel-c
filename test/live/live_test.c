/**
 * @file live_test.c
 * @brief Connections against a real server (RULES.md R-TEST-8): each
 * transport, mechanism and binding, what is asked for and told, timeouts,
 * cancel and terminate. Opt-in: TRL_TEST_SERVER names the server, else
 * the test is skipped.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/error.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "failure.h"
#include "harness.h"
#include "live.h"
#include "net/tls.h"
#include "net/transport.h"
#include "server.h"
#include "turborel/turborel.h"

/* The platform's and the TLS library's headers declare more than the
   include checker can map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include "format.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* CTest's code of a test skipped. */
#define SKIPPED 77
#define PATH_BYTES 1024U
/* A query that runs for minutes: a cross join of 100,000 rows by 100,000. */
#define SLOW_QUERY                                                                                                     \
    "WITH RECURSIVE T(N) AS (SELECT CAST(1 AS BIGINT) FROM (VALUES (1)) AS V(X) UNION ALL SELECT N + 1 FROM T "        \
    "WHERE N < 100000) SELECT COUNT(*) FROM T A, T B"
/* A query whose rows take more bytes than a frame sent compressed. */
#define LONG_QUERY                                                                                                     \
    "WITH RECURSIVE T(N) AS (SELECT CAST(1 AS BIGINT) FROM (VALUES (1)) AS V(X) UNION ALL SELECT N + 1 FROM T "        \
    "WHERE N < 2000) SELECT N FROM T"
#define LONG_ROWS 2000U
#define CANCEL_AFTER_NS 300000000L
#define READ_TIMEOUT_MS 300U

/* A TLS target of the server, by `tls`. */
static trl_target *tls_target(trl_tls *tls, const char *server_name) {
    trl_target *target = NULL;
    (void)trl_target_tls("127.0.0.1", SERVER_PORT, server_name, tls, &target);
    return target;
}

/**
 * Verifies: [CLIENT Connecting] over TCP, by trust, the connection gives
 * the server's product and the authorization identifier, and was granted
 * what it asked for; every Status told at login is kept, the session's
 * settings among them [PROTOCOL Status 'T'].
 */
static void tcp_trust_tells_the_session(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, conn != NULL);
    if (conn == NULL) {
        return;
    }
    TRL_CHECK(test, strncmp(trl_conn_server(conn), "turborel ", 9U) == 0);
    TRL_CHECK(test, strcmp(trl_conn_authorization(conn), "ANN") == 0);
    TRL_CHECK(test, trl_conn_granted(conn, TRL_CAPABILITY_STATUS) && trl_conn_granted(conn, TRL_CAPABILITY_PING));
    TRL_CHECK(test, !trl_conn_granted(conn, TRL_CAPABILITY_COMPRESSION));
    static const char *const told[] = {
        "AUTHORIZATION", "SCHEMA", "READ_ONLY",         "SERVER_VERSION",        "SERVER_READ_ONLY",
        "CATALOG",       "OWNER",  "SESSION.TIME_ZONE", "TRANSACTION.ISOLATION",
    };
    for (size_t i = 0U; i < sizeof told / sizeof told[0]; ++i) {
        TRL_CHECK(test, trl_conn_setting(conn, told[i]) != NULL);
    }
    TRL_CHECK(test, strcmp(trl_conn_setting(conn, "AUTHORIZATION"), "ANN") == 0);
    TRL_CHECK(test, strcmp(trl_conn_setting(conn, "READ_ONLY"), "NO") == 0);
    TRL_CHECK(test, strcmp(trl_conn_setting(conn, "SERVER_READ_ONLY"), "NO") == 0);
    TRL_CHECK(test, strcmp(trl_conn_setting(conn, "OWNER"), "YES") == 0);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK);
    TRL_CHECK(test, trl_conn_read_timeout(conn) == TRL_NO_LIMIT);
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_OK && trl_conn_is_lost(conn));
    trl_conn_free(conn);
}

/* Sends a Hello of `version`, `user` and `mechanism` over TCP, as no
   client of the driver's would, and reads the server's answer into
   `failure`; whether it was an Error. */
static bool raw_hello(uint32_t version, const char *user, const char *mechanism, trl_failure *failure) {
    trl_transport transport;
    memset(&transport, 0, sizeof transport);
    if (!trl_tcp_open("127.0.0.1", SERVER_PORT, TRL_CONNECT_WITHIN_MS, &transport)) {
        return false;
    }
    trl_hello hello;
    memset(&hello, 0, sizeof hello);
    hello.version = version;
    hello.user.bytes = (const uint8_t *)user;
    hello.user.length = strlen(user);
    hello.mechanism.bytes = (const uint8_t *)mechanism;
    hello.mechanism.length = strlen(mechanism);
    trl_buffer out;
    trl_buffer_init(&out);
    trl_codec_error error = {0, "", ""};
    bool sent = trl_encode_hello(&out, &hello, &error) && trl_transport_write_all(&transport, out.data, out.length);
    trl_buffer_free(&out);
    uint8_t reply[PATH_BYTES];
    size_t had = 0U;
    for (int64_t got = 1; sent && got > 0 && had < sizeof reply; had += (size_t)got) {
        got = transport.ops->read(&transport, reply + had, sizeof reply - had);
        if (got <= 0) {
            break;
        }
    }
    trl_transport_close(&transport);
    trl_arena arena;
    trl_arena_init(&arena);
    trl_reply decoded;
    trl_slice body = {reply + TRL_FRAME_HEADER_BYTES, had > TRL_FRAME_HEADER_BYTES ? had - TRL_FRAME_HEADER_BYTES : 0U};
    bool refused = had > TRL_FRAME_HEADER_BYTES && reply[0] == TRL_KIND_ERROR &&
                   trl_decode_reply(TRL_KIND_ERROR, body, &arena, &decoded, &error);
    if (refused) {
        trl_failure_from_wire(failure, &decoded.as.error);
    }
    trl_arena_free(&arena);
    return refused;
}

/**
 * Verifies: [PROTOCOL Opening a connection] the server refuses a version
 * other than 1 (SQLSTATE 08004), and a user name not an identifier or a
 * mechanism it does not serve before any exchange (SQLSTATE 28000).
 */
static void a_hello_out_of_bounds_is_refused(trl_test *test) {
    trl_failure failure;
    trl_failure_init(&failure);
    TRL_CHECK(test, raw_hello(2U, "ann", "trust", &failure) && strcmp(failure.sqlstate, "08004") == 0);
    TRL_CHECK(test, raw_hello(1U, "a b", "trust", &failure) && strcmp(failure.sqlstate, "28000") == 0);
    TRL_CHECK(test, raw_hello(1U, "ann", "nonesuch", &failure) && strcmp(failure.sqlstate, "28000") == 0);
    trl_failure_clear(&failure);
}

/**
 * Verifies: [PROTOCOL Status 'T'] at login, before Ready, one Status of
 * each name of the page's table, in its order, then the session's own
 * settings.
 */
static void statuses_come_in_the_tables_order(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    static const char *const order[] = {
        "SERVER_VERSION", "AUTHORIZATION", "SCHEMA", "READ_ONLY", "SERVER_READ_ONLY", "CATALOG", "OWNER",
    };
    for (size_t i = 0U; conn != NULL && i < sizeof order / sizeof order[0]; ++i) {
        const char *name = NULL;
        TRL_CHECK(test, trl_conn_setting_at(conn, i, &name, NULL) && strcmp(name, order[i]) == 0);
    }
    TRL_CHECK(test, conn != NULL && trl_conn_setting_count(conn) > sizeof order / sizeof order[0]);
    TRL_CHECK(test, conn != NULL && strcmp(trl_conn_setting(conn, "SERVER_VERSION"), trl_conn_server(conn)) == 0);
    trl_conn_free(conn);
}

/* A stream over TCP that records every byte the server sends. */
typedef struct recorder {
    trl_transport transport;
    trl_buffer heard;
} recorder;

static int64_t recorder_read(void *context, uint8_t *into, size_t capacity) {
    recorder *r = context;
    int64_t got = r->transport.ops->read(&r->transport, into, capacity);
    if (got > 0) {
        trl_put_raw(&r->heard, into, (size_t)got);
    }
    return got;
}

static int64_t recorder_write(void *context, const uint8_t *bytes, size_t length) {
    recorder *r = context;
    return r->transport.ops->write(&r->transport, bytes, length);
}

static void recorder_close(void *context) {
    recorder *r = context;
    trl_transport_close(&r->transport);
}

/* How many frames of `kind` the bytes from `from` hold. */
static size_t frames_of(const trl_buffer *heard, size_t from, uint8_t kind) {
    size_t count = 0U;
    for (size_t at = from; heard->length - at >= TRL_FRAME_HEADER_BYTES;) {
        trl_frame_header header = {0U, 0U};
        (void)trl_frame_header_decode(heard->data + at, TRL_FRAME_HEADER_BYTES, UINT32_MAX, &header);
        count += header.kind == kind ? 1U : 0U;
        at += TRL_FRAME_HEADER_BYTES + header.length;
    }
    return count;
}

/**
 * Verifies: [PROTOCOL Status 'T'] after a statement, a Status only of what
 * it changed, none of the first statement after login that changed
 * nothing, and one of a setting a statement changed; after Reset, one of
 * each setting again [PROTOCOL Reset 'W'].
 */
static void a_statement_tells_only_its_changes(trl_test *test) {
    recorder r;
    memset(&r, 0, sizeof r);
    trl_buffer_init(&r.heard);
    TRL_CHECK(test, trl_tcp_open("127.0.0.1", SERVER_PORT, TRL_CONNECT_WITHIN_MS, &r.transport));
    trl_stream stream = {&r, recorder_read, recorder_write, recorder_close};
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_connect_stream(&stream, "ann", trust, NULL, &conn) == TRL_OK);
    size_t logged_in = r.heard.length;
    size_t told_at_login = frames_of(&r.heard, 0U, TRL_KIND_STATUS);
    TRL_CHECK(test, told_at_login > 7U);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "VALUES (1)", &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, frames_of(&r.heard, logged_in, TRL_KIND_STATUS) == 0U);
    size_t first = r.heard.length;
    TRL_CHECK(test, trl_query(conn, "SET TIME ZONE INTERVAL '+03:00' HOUR TO MINUTE", &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, frames_of(&r.heard, first, TRL_KIND_STATUS) == 1U);
    size_t reset = r.heard.length;
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_OK);
    TRL_CHECK(test, frames_of(&r.heard, reset, TRL_KIND_STATUS) == told_at_login);
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    trl_buffer_free(&r.heard);
}

/**
 * Verifies: [PROTOCOL Status 'T'] after a statement, a Status of each
 * setting it changed, kept at once with no round trip
 * [CLIENT What the server tells].
 */
static void a_statement_tells_what_it_changed(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, conn != NULL);
    if (conn == NULL) {
        return;
    }
    TRL_CHECK(test, live_run(conn, "SET TIME ZONE INTERVAL '+02:00' HOUR TO MINUTE", NULL) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_conn_setting(conn, "SESSION.TIME_ZONE"), "+02:00") == 0);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT What a connection asks for] asking for nothing, the
 * protocol's first form: nothing granted, no Status told; asking for
 * compression too, it is granted, and replies come compressed
 * [PROTOCOL Compressed frames].
 */
static void what_a_connection_asks_for_is_granted(trl_test *test) {
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_ask_none(options);
    trl_conn *conn = live_trusted(options);
    TRL_CHECK(test, conn != NULL && trl_conn_granted_count(conn) == 0U && trl_conn_setting_count(conn) == 0U);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && !trl_conn_is_lost(conn));
    trl_conn_free(conn);
    trl_conn_options_free(options);
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_ask(options, TRL_CAPABILITY_COMPRESSION);
    (void)trl_conn_options_attribute(options, "application", "c-live");
    conn = live_trusted(options);
    TRL_CHECK(test, conn != NULL && trl_conn_granted(conn, TRL_CAPABILITY_COMPRESSION) == trl_compression_built());
    uint64_t rows = 0U;
    TRL_CHECK(test, live_run(conn,
                             "SELECT ATTRIBUTE_VALUE FROM INFORMATION_SCHEMA.CONNECTION_ATTRIBUTES "
                             "WHERE ATTRIBUTE_NAME = 'application' AND ATTRIBUTE_VALUE = 'c-live'",
                             &rows) == TRL_OK);
    TRL_CHECK(test, rows == 1U);
    TRL_CHECK(test, live_run(conn, LONG_QUERY, &rows) == TRL_OK && rows == LONG_ROWS);
    trl_conn_free(conn);
    trl_conn_options_free(options);
}

/**
 * Verifies: [CLIENT Connecting] over a Unix socket, `password` is sent
 * whole, and SCRAM-SHA-256 is taken unbound.
 */
static void a_unix_socket_takes_a_password(trl_test *test) {
    trl_target *target = NULL;
    TRL_CHECK(test, trl_target_local(live_server.socket, &target) == TRL_OK);
    trl_mechanism *password = NULL;
    (void)trl_mechanism_password(SERVER_PASSWORD, &password);
    trl_conn *conn = live_connect(target, password, NULL);
    TRL_CHECK(test, conn != NULL && strcmp(trl_conn_authorization(conn), "ANN") == 0);
    trl_conn_free(conn);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram(SERVER_PASSWORD, &scram);
    conn = live_connect(target, scram, NULL);
    TRL_CHECK(test, conn != NULL);
    trl_conn_free(conn);
    trl_mechanism_free(scram);
    trl_mechanism_free(password);
    trl_target_free(target);
}

/**
 * Verifies: [CLIENT Connecting] over TLS 1.3, SCRAM is SCRAM-SHA-256-PLUS
 * bound by `tls-exporter`, which the server takes only if the binding is
 * right; its keys are kept in a cache, and the next login finds them
 * [CLIENT The SCRAM key cache].
 */
static void tls13_binds_scram_by_the_exporter(trl_test *test) {
    trl_tls *tls = live_tls(false);
    trl_target *target = tls_target(tls, "localhost");
    trl_key_cache *cache = NULL;
    (void)trl_key_cache_new(&cache);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram(SERVER_PASSWORD, &scram);
    (void)trl_mechanism_scram_channel_binding(scram, TRL_CHANNEL_BINDING_REQUIRE);
    (void)trl_mechanism_scram_key_cache(scram, cache);
    for (int login = 0; login < 2; ++login) {
        trl_conn *conn = live_connect(target, scram, NULL);
        TRL_CHECK(test, conn != NULL && strcmp(trl_conn_authorization(conn), "ANN") == 0);
        trl_conn_free(conn);
    }
    uint64_t hits = 0U;
    uint64_t misses = 0U;
    trl_key_cache_lookups(cache, &hits, &misses);
    TRL_CHECK(test, hits == 1U && misses == 1U);
    trl_mechanism_free(scram);
    trl_key_cache_free(cache);
    trl_target_free(target);
    trl_tls_free(tls);
}

/**
 * Verifies: [CLIENT Connecting] over TLS 1.2, SCRAM-SHA-256-PLUS is bound
 * by the certificate the server presented, `tls-server-end-point`; and
 * `password` is taken over TLS.
 */
static void tls12_binds_scram_by_the_certificate(trl_test *test) {
    trl_tls *tls = live_tls(false);
    TRL_CHECK(test, trl_tls_at_most_tls12(tls));
    trl_target *target = tls_target(tls, NULL);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram(SERVER_PASSWORD, &scram);
    (void)trl_mechanism_scram_channel_binding(scram, TRL_CHANNEL_BINDING_REQUIRE);
    trl_conn *conn = live_connect(target, scram, NULL);
    TRL_CHECK(test, conn != NULL);
    trl_conn_free(conn);
    trl_mechanism *password = NULL;
    (void)trl_mechanism_password(SERVER_PASSWORD, &password);
    conn = live_connect(target, password, NULL);
    TRL_CHECK(test, conn != NULL);
    trl_conn_free(conn);
    trl_mechanism_free(password);
    trl_mechanism_free(scram);
    trl_target_free(target);
    trl_tls_free(tls);
}

/**
 * Verifies: [CLIENT Connecting] `certificate`: TLS trusting a PEM file of
 * authorities and presenting a chain and key of two more proves the user
 * its common name names.
 */
static void a_certificate_proves_the_user(trl_test *test) {
    trl_tls *tls = live_tls(true);
    TRL_CHECK(test, tls != NULL);
    trl_target *target = tls_target(tls, "localhost");
    trl_mechanism *certificate = NULL;
    (void)trl_mechanism_certificate(&certificate);
    trl_conn *conn = live_connect(target, certificate, NULL);
    TRL_CHECK(test, conn != NULL && strcmp(trl_conn_authorization(conn), "ANN") == 0);
    trl_conn_free(conn);
    trl_mechanism_free(certificate);
    trl_target_free(target);
    trl_tls_free(tls);
}

/**
 * Verifies: [CLIENT Connecting] a wrong password is refused (SQLSTATE
 * 28000) and its key cache's entry dropped; over plain TCP a password, or
 * SCRAM, is refused before connecting, unless SCRAM is asked for in the
 * clear, which a server of SERVER.SCRAM_WITHOUT_TLS takes.
 */
static void secrets_are_kept_from_plain_tcp(trl_test *test) {
    trl_tls *tls = live_tls(false);
    trl_target *target = tls_target(tls, "localhost");
    trl_key_cache *cache = NULL;
    (void)trl_key_cache_new(&cache);
    trl_mechanism *wrong = NULL;
    (void)trl_mechanism_scram("not ann's", &wrong);
    (void)trl_mechanism_scram_key_cache(wrong, cache);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_connect(target, "ann", wrong, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "28000") == 0);
    TRL_CHECK(test, trl_key_cache_size(cache) == 0U);
    trl_conn_free(conn);
    trl_target *plain = NULL;
    (void)trl_target_tcp("127.0.0.1", SERVER_PORT, &plain);
    trl_mechanism *scram = NULL;
    (void)trl_mechanism_scram(SERVER_PASSWORD, &scram);
    TRL_CHECK(test, trl_connect(plain, "ann", scram, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "28000") == 0);
    TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "never over plain TCP") != NULL);
    trl_conn_free(conn);
    (void)trl_mechanism_scram_in_the_clear(scram);
    conn = live_connect(plain, scram, NULL);
    TRL_CHECK(test, conn != NULL);
    trl_conn_free(conn);
    trl_mechanism_free(scram);
    trl_mechanism_free(wrong);
    trl_key_cache_free(cache);
    trl_target_free(plain);
    trl_target_free(target);
    trl_tls_free(tls);
}

/**
 * Verifies: [CLIENT Connecting] TLS fails its handshake (SQLSTATE 08006)
 * with a server whose certificate no trusted authority signed, or of
 * another name; the platform's authorities are those of SSL_CERT_FILE
 * when it is set.
 */
static void tls_trusts_only_what_it_is_told(trl_test *test) {
    trl_tls *other = NULL;
    (void)trl_tls_new(&other);
    TRL_CHECK(test, trl_tls_trust_file(other, live_path("other-ca.pem")) == TRL_OK);
    trl_target *target = tls_target(other, "localhost");
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_connect(target, "ann", trust, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_conn_failure(conn)), "the TLS handshake failed", 24U) == 0);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    trl_conn_free(conn);
    trl_target_free(target);
    trl_tls *tls = live_tls(false);
    target = tls_target(tls, "turborel.example");
    TRL_CHECK(test, trl_connect(target, "ann", trust, NULL, &conn) == TRL_FAILED);
    trl_conn_free(conn);
    trl_target_free(target);
    trl_tls *platform = NULL;
    (void)trl_tls_new(&platform);
    /* The test runs on one thread here; the canceller's thread is not begun. */
    TRL_CHECK(test, setenv("SSL_CERT_FILE", live_path("ca.pem"), 1) == 0); // NOLINT(concurrency-mt-unsafe)
    TRL_CHECK(test, trl_tls_trust_platform(platform) == TRL_OK);
    TRL_CHECK(test, unsetenv("SSL_CERT_FILE") == 0); // NOLINT(concurrency-mt-unsafe)
    target = tls_target(platform, "localhost");
    conn = live_connect(target, trust, NULL);
    TRL_CHECK(test, conn != NULL);
    trl_conn_free(conn);
    trl_target_free(target);
    trl_mechanism_free(trust);
    trl_tls_free(platform);
    trl_tls_free(tls);
    trl_tls_free(other);
}

/* Cancels after a while, on a thread of its own. */
static void *cancel_later(void *argument) {
    struct timespec wait = {0, CANCEL_AFTER_NS};
    (void)nanosleep(&wait, NULL);
    trl_canceller *canceller = argument;
    return trl_cancel(canceller) == TRL_OK ? argument : NULL;
}

/**
 * Verifies: [PROTOCOL Cancel 'Z'] a statement cancelled from another
 * thread, by the key Ready gave over a connection of its own, fails with
 * SQLCODE -920, SQLSTATE TB920, the connection kept
 * [CLIENT Running statements].
 */
static void a_statement_is_cancelled_from_another_thread(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    trl_canceller *canceller = NULL;
    TRL_CHECK(test, conn != NULL && trl_canceller_new(conn, &canceller) == TRL_OK);
    if (canceller == NULL) {
        trl_conn_free(conn);
        return;
    }
    pthread_t thread;
    TRL_CHECK(test, pthread_create(&thread, NULL, cancel_later, canceller) == 0);
    TRL_CHECK(test, live_run(conn, SLOW_QUERY, NULL) == TRL_FAILED);
    void *cancelled = NULL;
    (void)pthread_join(thread, &cancelled);
    TRL_CHECK(test, cancelled != NULL);
    TRL_CHECK(test, trl_failure_sqlcode(trl_conn_failure(conn)) == -920);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "TB920") == 0);
    TRL_CHECK(test, !trl_conn_is_lost(conn) && trl_conn_ping(conn) == TRL_OK);
    trl_canceller_free(canceller);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Timeouts] a read timeout of the program's, set on a
 * connection, that runs out loses it (SQLSTATE 08006), saying the server
 * did not answer in time.
 */
static void a_read_timeout_that_runs_out_loses_the_connection(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, conn != NULL && trl_conn_set_read_timeout(conn, READ_TIMEOUT_MS) == TRL_OK);
    TRL_CHECK(test, trl_conn_read_timeout(conn) == READ_TIMEOUT_MS);
    TRL_CHECK(test, live_run(conn, SLOW_QUERY, NULL) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "the server did not answer in time") != NULL);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-server", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(tcp_trust_tells_the_session),
        TRL_TEST_CASE(a_statement_tells_what_it_changed),
        TRL_TEST_CASE(a_hello_out_of_bounds_is_refused),
        TRL_TEST_CASE(statuses_come_in_the_tables_order),
        TRL_TEST_CASE(a_statement_tells_only_its_changes),
        TRL_TEST_CASE(what_a_connection_asks_for_is_granted),
        TRL_TEST_CASE(a_unix_socket_takes_a_password),
        TRL_TEST_CASE(tls13_binds_scram_by_the_exporter),
        TRL_TEST_CASE(tls12_binds_scram_by_the_certificate),
        TRL_TEST_CASE(a_certificate_proves_the_user),
        TRL_TEST_CASE(secrets_are_kept_from_plain_tcp),
        TRL_TEST_CASE(tls_trusts_only_what_it_is_told),
        TRL_TEST_CASE(a_statement_is_cancelled_from_another_thread),
        TRL_TEST_CASE(a_read_timeout_that_runs_out_loses_the_connection),
    };
    int result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    server_stop(&live_server);
    return result;
}
// NOLINTEND(misc-include-cleaner)
