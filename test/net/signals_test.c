/**
 * @file signals_test.c
 * @brief Calls a signal interrupts, to a server of the test's own over a
 * real socket (turborel's RULES.md D1047): a read waiting for a reply, and the wait
 * for the close after Terminate, signalled again and again by a handler
 * installed without SA_RESTART, each made again and so succeeding; and a
 * read timeout, signalled, still bounding the read as a whole
 * [CLIENT Timeouts].
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "fake.h"
#include "harness.h"
#include "signals.h"
#include "turborel/turborel.h"

/* The platform declares its threads in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A read timeout shorter than the slow fake's lateness. */
#define SHORT_MS 100U

/* Connects to the fake `f` as ann by trust, of no read timeout. */
static trl_conn *trusted(const fake *f) {
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    trl_conn *conn = NULL;
    (void)trl_target_tcp("127.0.0.1", f->port, &target);
    (void)trl_mechanism_trust(&trust);
    if (trl_connect(target, "ann", trust, NULL, &conn) != TRL_OK) {
        trl_conn_free(conn);
        conn = NULL;
    }
    trl_mechanism_free(trust);
    trl_target_free(target);
    return conn;
}

/**
 * Verifies: [CLIENT Timeouts] a Ping whose Pong comes late, its read
 * signalled again and again while it waits, is answered, the connection
 * kept; and Terminate, whose wait for the server's close is signalled
 * as it comes late, ends the session as ever (turborel's RULES.md D1047).
 */
static void a_signalled_read_and_terminate_succeed(trl_test *test) {
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_READY_SLOW, 1, NULL));
    trl_conn *conn = trusted(&f);
    TRL_CHECK(test, conn != NULL);
    int before = signals_taken();
    signaller s;
    TRL_CHECK(test, signaller_start(&s, pthread_self()));
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_OK && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_conn_terminate(conn) == TRL_OK);
    signaller_stop(&s);
    TRL_CHECK(test, signals_taken() > before);
    trl_conn_free(conn);
    fake_stop(&f);
}

/**
 * Verifies: [CLIENT Timeouts] a read timeout shorter than the Pong's
 * lateness, its read signalled again and again, still runs out, the
 * time spent before each signal kept rather than begun again, and the
 * Ping fails before its Pong comes (turborel's RULES.md D1047).
 */
static void a_signalled_read_keeps_its_timeout(trl_test *test) {
    fake f;
    TRL_CHECK(test, fake_start(&f, FAKE_READY_SLOW, 1, NULL));
    trl_conn *conn = trusted(&f);
    TRL_CHECK(test, conn != NULL && trl_conn_set_read_timeout(conn, SHORT_MS) == TRL_OK);
    int before = signals_taken();
    signaller s;
    TRL_CHECK(test, signaller_start(&s, pthread_self()));
    TRL_CHECK(test, trl_conn_ping(conn) == TRL_FAILED && trl_conn_is_lost(conn));
    signaller_stop(&s);
    TRL_CHECK(test, signals_taken() > before);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_conn_failure(conn)), "cannot receive", 14U) == 0);
    trl_conn_free(conn);
    fake_stop(&f);
}

int main(int argc, char **argv) {
    if (!signals_install()) {
        return EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_signalled_read_and_terminate_succeed),
        TRL_TEST_CASE(a_signalled_read_keeps_its_timeout),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
// NOLINTEND(misc-include-cleaner)
