/**
 * @file faults_test.c
 * @brief Memory not had (RULES.md R-MEM-2, R-ERR-4): each allocation of a
 * scenario made to fail in turn, the call failing as memory not had
 * (TRL_NO_MEMORY, or SQLCODE -918) or as the connection lost, never
 * crashing or leaking, until the scenario runs whole. Built with
 * TRL_FAULTS alone.
 */
#include "codec/buffer.h"
#include "failure.h"
#include "harness.h"
#include "memory.h"
#include "scram_server.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TRL_HAVE_TLS
#include "certs.h"
#include "fake.h"

#include <openssl/crypto.h>
#define FAULTS_TLS 1
/* The directory the scenario's certificates are made in. */
#define TLS_DIR "faults-test"
#endif

/* The most allocations a scenario is failed at. */
#define MOST_FAULTS 4000L

/* A scenario: whether what it came to is what memory not had allows. */
typedef bool (*scenario)(void);

/* Whether a status is success, or failure of memory or the connection. */
static bool allowed(trl_status status, const trl_failure *failure) {
    if (status == TRL_OK || status == TRL_NO_MEMORY) {
        return true;
    }
    int32_t sqlcode = trl_failure_sqlcode(failure);
    return status == TRL_FAILED && (sqlcode == -918 || sqlcode == -919);
}

/* Runs `run` with each allocation failing in turn until it runs whole;
   whether every run came to what is allowed. */
static bool every_fault(scenario run) {
    for (long allowed_count = 0L; allowed_count < MOST_FAULTS; ++allowed_count) {
        trl_faults_arm(allowed_count);
        bool fine = run();
        bool fired = trl_faults_fired();
        trl_faults_arm(-1L);
        if (!fine) {
            return false;
        }
        if (!fired) {
            return true;
        }
    }
    return false;
}

/* Answers a trusted login with Statuses and Ready, and Ping with a Status and Pong. */
static void serving(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    static const char *const granted[] = {TRL_CAPABILITY_STATUS, TRL_CAPABILITY_PING, TRL_CAPABILITY_COMPRESSION};
    if (kind == 'H') {
        script_status(s, "AUTHORIZATION", "ANN");
        script_status(s, "SESSION.TIME_ZONE", "+00:00");
        script_ready(s, "ANN", granted, 3U);
    } else if (kind == 'G') {
        script_status(s, "SESSION.TIME_ZONE", "+01:00");
        script_error(s, -900, "0A000", "not now", "DESIGN");
    } else if (kind == 'X') {
        script_error(s, -320, "40002", "a deferred constraint", NULL);
    }
}

/* A trusted login over a script, Ping failing, and Terminate failing. */
static bool trusted_session(void) {
    script s;
    script_init(&s, serving, NULL);
    trl_mechanism *trust = NULL;
    trl_conn_options *options = NULL;
    bool fine = true;
    if (trl_mechanism_trust(&trust) == TRL_OK && trl_conn_options_new(&options) == TRL_OK &&
        trl_conn_options_attribute(options, "application", "faults") == TRL_OK) {
        trl_conn *conn = NULL;
        trl_status made = script_connect(&s, trust, options, &conn);
        fine = allowed(made, trl_conn_failure(conn));
        if (made == TRL_OK) {
            trl_status pinged = trl_conn_ping(conn);
            fine = fine && pinged == TRL_FAILED;
            trl_status ended = trl_conn_terminate(conn);
            fine = fine && ended == TRL_FAILED;
        }
        trl_conn_free(conn);
    }
    trl_conn_options_free(options);
    trl_mechanism_free(trust);
    script_free(&s);
    return fine;
}

/* A SCRAM login over a script, its keys kept in a cache. */
static bool scram_session(void) {
    scram_server server;
    memset(&server, 0, sizeof server);
    script s;
    script_init(&s, scram_serving, &server);
    trl_key_cache *cache = NULL;
    trl_mechanism *scram = NULL;
    bool fine = true;
    if (trl_key_cache_new(&cache) == TRL_OK && trl_mechanism_scram(PASSWORD, &scram) == TRL_OK &&
        trl_mechanism_scram_key_cache(scram, cache) == TRL_OK) {
        trl_conn *conn = NULL;
        trl_status made = script_connect(&s, scram, NULL, &conn);
        fine = allowed(made, trl_conn_failure(conn));
        trl_conn_free(conn);
    }
    trl_mechanism_free(scram);
    trl_key_cache_free(cache);
    script_free(&s);
    return fine;
}

/* The handles a connection is made of, and a failure copied. */
static bool handles(void) {
    trl_target *target = NULL;
    trl_tls *tls = NULL;
    trl_conn_options *options = NULL;
    trl_mechanism *password = NULL;
    bool fine = trl_target_tcp("localhost", TRL_DEFAULT_PORT, &target) != TRL_INVALID_ARGUMENT;
    trl_target_free(target);
    fine = fine && trl_target_local("/tmp/x.sock", &target) != TRL_INVALID_ARGUMENT;
    trl_target_free(target);
    fine = fine && trl_tls_new(&tls) != TRL_INVALID_ARGUMENT;
    trl_tls_free(tls);
    fine = fine && trl_mechanism_password("p", &password) != TRL_INVALID_ARGUMENT;
    trl_mechanism_free(password);
    if (trl_conn_options_new(&options) == TRL_OK) {
        fine = fine && trl_conn_options_ask(options, "extra") != TRL_INVALID_ARGUMENT;
        fine = fine && trl_conn_options_attribute(options, "a", "b") != TRL_INVALID_ARGUMENT;
    }
    trl_conn_options_free(options);
    trl_failure failure;
    trl_failure copy;
    trl_failure_init(&failure);
    trl_failure_init(&copy);
    trl_failure_broken(&failure, "lost");
    trl_failure_copy(&copy, &failure);
    fine = fine && (trl_failure_sqlcode(&copy) == -919 || trl_failure_sqlcode(&copy) == -918);
    trl_failure_clear(&failure);
    trl_failure_clear(&copy);
    return fine;
}

#ifdef FAULTS_TLS
/* The TLS library's allocations, failing as the fault armed says. */
static void *tls_malloc(size_t size, const char *file, int line) {
    (void)file;
    (void)line;
    return trl_faults_take() ? NULL : malloc(size);
}

static void *tls_realloc(void *block, size_t size, const char *file, int line) {
    (void)file;
    (void)line;
    return trl_faults_take() ? NULL : realloc(block, size);
}

static void tls_free(void *block, const char *file, int line) {
    (void)file;
    (void)line;
    free(block);
}

/* A session over TLS to a fake: login by trust, Ping, Terminate. */
static bool tls_session(void) {
    fake f;
    long held = trl_faults_suspend();
    bool started = fake_start_tls(&f, FAKE_READY, 1, TLS_DIR, false);
    trl_faults_resume(held);
    if (!started) {
        return false;
    }
    trl_tls *tls = NULL;
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    trl_conn *conn = NULL;
    bool fine = true;
    trl_status made = TRL_NO_MEMORY;
    if (trl_tls_new(&tls) == TRL_OK && trl_tls_trust_file(tls, TLS_DIR "/ca.pem") == TRL_OK &&
        trl_target_tls("127.0.0.1", f.port, "localhost", tls, &target) == TRL_OK &&
        trl_mechanism_trust(&trust) == TRL_OK) {
        made = trl_connect(target, "ann", trust, NULL, &conn);
        fine = allowed(made, trl_conn_failure(conn));
    }
    if (made == TRL_OK) {
        trl_status pinged = trl_conn_ping(conn);
        fine = fine && allowed(pinged, trl_conn_failure(conn));
        (void)trl_conn_terminate(conn);
    }
    trl_conn_free(conn);
    held = trl_faults_suspend();
    fake_stop(&f);
    trl_faults_resume(held);
    trl_mechanism_free(trust);
    trl_target_free(target);
    trl_tls_free(tls);
    return fine;
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a session over TLS, the
 * TLS library's among them, failing in turn fails the call as memory not
 * had, or the connection lost, and leaks nothing.
 */
static void a_tls_session_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(tls_session));
}
#endif

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a trusted session, from
 * login to Terminate, failing in turn fails the call as memory not had,
 * or the connection lost, and leaks nothing.
 */
static void a_session_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(trusted_session));
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a SCRAM login with its key
 * cache failing in turn fails the login as memory not had.
 */
static void a_scram_login_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(scram_session));
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of the handles a connection is
 * made of failing in turn gives TRL_NO_MEMORY or a failure of memory.
 */
static void handles_survive_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(handles));
}

int main(int argc, char **argv) {
#ifdef FAULTS_TLS
    /* The TLS library initializes itself once, as it is first used; a fault
       then would leave it unusable for the rest of the run, so a whole
       session runs before any fault is armed. */
    if (CRYPTO_set_mem_functions(tls_malloc, tls_realloc, tls_free) != 1 || !certs_make(TLS_DIR) || !tls_session()) {
        return EXIT_FAILURE;
    }
#endif
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_session_survives_each_fault),
        TRL_TEST_CASE(a_scram_login_survives_each_fault),
        TRL_TEST_CASE(handles_survive_each_fault),
#ifdef FAULTS_TLS
        TRL_TEST_CASE(a_tls_session_survives_each_fault),
#endif
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
