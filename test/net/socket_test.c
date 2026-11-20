/**
 * @file socket_test.c
 * @brief Connections over real sockets, to a server of the test's own
 * [CLIENT Connecting] [CLIENT Timeouts]: TCP and Unix sockets opened,
 * what the system says when they cannot be, the connect timeout bounding
 * a login, and cancel over a connection of its own [PROTOCOL Cancel 'Z'].
 */
#include "fake.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SHORT_MS 100U
#define CANCEL_MS 50U
#define PATH_BYTES 104U

/* Connects to `target` as ann by trust, with a connect timeout of `ms`. */
static trl_status trusted(const trl_target *target, uint32_t ms, trl_conn **conn) {
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_timeouts(options, ms, TRL_NO_LIMIT);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_status made = trl_connect(target, "ann", trust, options, conn);
    trl_mechanism_free(trust);
    trl_conn_options_free(options);
    return made;
}

/* Whether the connection failed with a message beginning `prefix`, SQLSTATE 08006. */
static bool failed_with(const trl_conn *conn, const char *prefix) {
    const trl_failure *failure = trl_conn_failure(conn);
    return strncmp(trl_failure_message(failure), prefix, strlen(prefix)) == 0 &&
           strcmp(trl_failure_sqlstate(failure), "08006") == 0;
}

/**
 * Verifies: [CLIENT Connecting] over TCP, a login answered by Ready; Ping,
 * the read timeout set, a canceller sending the key alone over a
 * connection of its own, and Terminate.
 */
static void tcp_connects_and_cancels(trl_test *test) {
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_READY, 2, NULL));
    trl_target *target = NULL;
    TRL_CHECK(test, trl_target_tcp("127.0.0.1", f.port, &target) == TRL_OK);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trusted(target, TRL_CONNECT_WITHIN_MS, &conn) == TRL_OK);
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK);
    TRL_CHECK(test, trl_conn_set_read_timeout(conn, TRL_CONNECT_WITHIN_MS) == TRL_OK);
    TRL_CHECK(test, trl_conn_read_timeout(conn) == TRL_CONNECT_WITHIN_MS);
    trl_canceller *canceller = NULL;
    TRL_CHECK(test, trl_canceller_new(conn, &canceller) == TRL_OK);
    /* The fake takes one connection at a time: the session ends first. */
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_OK);
    TRL_CHECK(test, trl_cancel(canceller) == TRL_OK && trl_canceller_failure(canceller) != NULL);
    trl_conn_free(conn);
    fake_stop(&f);
    TRL_CHECK(test, f.first[0] == 'H' && f.first[1] == 'Z');
    trl_canceller_free(canceller);
    trl_target_free(target);
}

/**
 * Verifies: [CLIENT Timeouts] the connect timeout bounds each step of the
 * login: a server that never answers loses the connection (SQLSTATE
 * 08006), saying it did not answer in time; a cancel gives up after its
 * own limit.
 */
static void a_silent_server_runs_the_timeouts_out(trl_test *test) {
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_SILENT, 2, NULL));
    trl_target *target = NULL;
    (void)trl_target_tcp("localhost", f.port, &target);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trusted(target, SHORT_MS, &conn) == TRL_FAILED && trl_conn_is_lost(conn));
    TRL_CHECK(test, failed_with(conn, "cannot receive: the server did not answer in time"));
    trl_conn_free(conn);
    trl_canceller *canceller = NULL;
    trl_target *again = NULL;
    (void)trl_target_tcp("127.0.0.1", f.port, &again);
    TRL_CHECK(test, trusted(again, SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, trl_canceller_new(conn, &canceller) == TRL_FAILED && canceller == NULL);
    trl_conn_free(conn);
    fake_stop(&f);
    trl_target_free(again);
    trl_target_free(target);
}

/**
 * Verifies: [CLIENT Connecting] what the system says when a connection
 * cannot be made: refused, a host not found, a socket not there or of a
 * path too long (SQLSTATE 08006).
 */
static void the_system_says_why_it_cannot_connect(trl_test *test) {
    trl_target *target = NULL;
    trl_conn *conn = NULL;
    (void)trl_target_tcp("127.0.0.1", fake_closed_port(), &target);
    TRL_CHECK(test, trusted(target, SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "cannot connect: ") &&
                        strstr(trl_failure_message(trl_conn_failure(conn)), "(os error") != NULL);
    trl_conn_free(conn);
    trl_target_free(target);
    (void)trl_target_tcp("no-such-host.invalid", TRL_DEFAULT_PORT, &target);
    TRL_CHECK(test, trusted(target, SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "cannot connect: failed to lookup address information: "));
    trl_conn_free(conn);
    trl_target_free(target);
    (void)trl_target_local("/nonexistent/turborel.sock", &target);
    TRL_CHECK(test, trusted(target, SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "cannot connect: No such file or directory"));
    trl_conn_free(conn);
    trl_target_free(target);
    char path[PATH_BYTES * 2U];
    memset(path, 'x', sizeof path - 1U);
    path[0] = '/';
    path[sizeof path - 1U] = '\0';
    (void)trl_target_local(path, &target);
    TRL_CHECK(test, trusted(target, SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "cannot connect: path must be shorter than SUN_LEN"));
    trl_conn_free(conn);
    trl_target_free(target);
}

/**
 * Verifies: [CLIENT Connecting] over a Unix socket, a login answered;
 * `password` goes over it, a secret refused over plain TCP before any
 * connection is made (SQLSTATE 28000).
 */
static void a_unix_socket_takes_a_secret_tcp_does_not(trl_test *test) {
    char path[PATH_BYTES];
    (void)snprintf(path, sizeof path, "fake-%ld.sock", (long)time(NULL));
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_READY, 1, path));
    trl_target *target = NULL;
    (void)trl_target_local(path, &target);
    trl_mechanism *password = NULL;
    (void)trl_mechanism_password("secret", &password);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_connect(target, "ann", password, NULL, &conn) == TRL_OK);
    trl_conn_free(conn);
    fake_stop(&f);
    trl_target_free(target);
    (void)trl_target_tcp("127.0.0.1", fake_closed_port(), &target);
    TRL_CHECK(test, trl_connect(target, "ann", password, NULL, &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "28000") == 0);
    TRL_CHECK(test, strcmp(trl_failure_message(trl_conn_failure(conn)),
                           "password is sent only over TLS, a Unix socket or a named pipe, never over plain TCP") == 0);
    trl_conn_free(conn);
    trl_mechanism_free(password);
    trl_target_free(target);
}

/**
 * Verifies: [PROTOCOL Cancel 'Z'] a cancel to a server that closes at once
 * is done; to one that never closes, it gives up after its limit; to one
 * not there, it says why.
 */
static void a_cancel_gives_up_in_time(trl_test *test) {
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_READY_HOLDING, 2, NULL));
    trl_target *target = NULL;
    (void)trl_target_tcp("127.0.0.1", f.port, &target);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trusted(target, TRL_CONNECT_WITHIN_MS, &conn) == TRL_OK);
    trl_canceller *canceller = NULL;
    TRL_CHECK(test, trl_canceller_new(conn, &canceller) == TRL_OK);
    trl_conn_free(conn);
    clock_t began = clock();
    TRL_CHECK(test, trl_cancel_within(canceller, CANCEL_MS) == TRL_FAILED);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_canceller_failure(canceller)), "cannot receive: ", 16U) == 0);
    TRL_CHECK(test, (clock() - began) / CLOCKS_PER_SEC < TRL_CANCEL_WITHIN_MS / 1000U);
    fake_stop(&f);
    TRL_CHECK(test, trl_cancel_within(canceller, CANCEL_MS) == TRL_FAILED);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_canceller_failure(canceller)), "cannot connect: ", 16U) == 0);
    trl_canceller_free(canceller);
    TRL_CHECK(test, trl_cancel(NULL) == TRL_INVALID_ARGUMENT && trl_canceller_failure(NULL) == NULL);
    TRL_CHECK(test, trl_canceller_new(NULL, &canceller) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_canceller_new(NULL, NULL) == TRL_INVALID_ARGUMENT);
    trl_canceller_free(NULL);
    trl_target_free(target);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(tcp_connects_and_cancels),
        TRL_TEST_CASE(a_silent_server_runs_the_timeouts_out),
        TRL_TEST_CASE(the_system_says_why_it_cannot_connect),
        TRL_TEST_CASE(a_unix_socket_takes_a_secret_tcp_does_not),
        TRL_TEST_CASE(a_cancel_gives_up_in_time),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
