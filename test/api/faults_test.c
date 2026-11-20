/**
 * @file faults_test.c
 * @brief Memory not had (RULES.md R-MEM-2, R-ERR-4): each allocation of a
 * scenario made to fail in turn, the call failing as memory not had
 * (TRL_NO_MEMORY, or SQLCODE -918) or as the connection lost, never
 * crashing or leaking, until the scenario runs whole. Built with
 * TRL_FAULTS alone.
 */
#include "codec/buffer.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "failure.h"
#include "faults.h"
#include "harness.h"
#include "memory.h"
#include "scram_server.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

/* Answers a login granting every capability a statement method needs, and
   each statement with notices, rows and a completion of every tail. */
static void statement_serving(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    static const char *const granted[] = {
        TRL_CAPABILITY_NOTICES,         TRL_CAPABILITY_DESCRIBE,    TRL_CAPABILITY_CURSORS,
        TRL_CAPABILITY_RESET,           TRL_CAPABILITY_CHANGE_USER, TRL_CAPABILITY_QUERY_ATTRIBUTES,
        TRL_CAPABILITY_MULTI_STATEMENT, TRL_CAPABILITY_FETCH_ENDS,  TRL_CAPABILITY_CONDITIONS,
    };
    switch (kind) {
    case 'H':
    case 'U':
        script_status(s, "AUTHORIZATION", "ANN");
        script_ready(s, "ANN", granted, sizeof granted / sizeof granted[0]);
        break;
    case 'Q':
    case 'E':
        script_notice(s, "01000", "a warning");
        script_description(s, 2U);
        script_rows(s, 2U, 2U);
        script_complete_tails(s);
        break;
    case 'P':
        script_prepared(s, 0U, 1U, 2U);
        break;
    case 'Y':
        script_description(s, 2U);
        break;
    default:
        script_complete(s, 0, "00000", false, false);
        break;
    }
}

/* Parameters of every kind, each copied into the list. */
static bool parameters(trl_params *params) {
    trl_decimal decimal;
    memset(&decimal, 0, sizeof decimal);
    trl_datetime datetime;
    memset(&datetime, 0, sizeof datetime);
    trl_interval interval;
    memset(&interval, 0, sizeof interval);
    trl_decfloat decfloat;
    memset(&decfloat, 0, sizeof decfloat);
    static const uint8_t octets[TRL_UUID_BYTES] = {1};
    trl_status added[] = {
        trl_params_add_null(params),
        trl_params_add_text(params, "Ada", 3U),
        trl_params_add_bytes(params, octets, sizeof octets),
        trl_params_add_decimal(params, decimal),
        trl_params_add_double(params, 0.5),
        trl_params_add_boolean(params, true),
        trl_params_add_datetime(params, TRL_KIND_DATE, datetime),
        trl_params_add_interval(params, interval),
        trl_params_add_decfloat(params, decfloat),
        trl_params_add_uuid(params, octets),
        trl_params_add_json(params, "{\"a\": [1, 2.5, \"x\"]}", 20U),
        trl_params_add_xml(params, "<a>b</a>", 8U),
    };
    /* A row of an array of two texts, copied deep. */
    trl_value texts[2];
    memset(texts, 0, sizeof texts);
    for (size_t i = 0U; i < 2U; ++i) {
        texts[i].tag = TRL_TAG_VARYING;
        texts[i].as.bytes.bytes = (const uint8_t *)"text";
        texts[i].as.bytes.length = 4U;
    }
    trl_value array;
    memset(&array, 0, sizeof array);
    array.tag = TRL_TAG_ARRAY;
    array.as.list.items = texts;
    array.as.list.count = 2U;
    trl_value row = array;
    row.tag = TRL_TAG_ROW;
    row.as.list.items = &array;
    row.as.list.count = 1U;
    trl_status copied = trl_params_add_value(params, &row);
    bool fine = copied == TRL_OK || copied == TRL_NO_MEMORY;
    for (size_t i = 0U; i < sizeof added / sizeof added[0]; ++i) {
        fine = fine && (added[i] == TRL_OK || added[i] == TRL_NO_MEMORY);
    }
    return fine;
}

/* The statement methods, each allowed to fail as memory not had. */
static bool statement_calls(trl_conn *conn, trl_params *params) {
    trl_result *result = NULL;
    bool fine = allowed(trl_query(conn, "Q", &result), trl_conn_failure(conn));
    if (result != NULL) {
        fine = fine && (trl_result_row_count(result) == 0U || trl_result_value(result, 0U, 0U) != NULL);
    }
    trl_result_free(result);
    trl_ran *ran = NULL;
    fine = fine && allowed(trl_query_many(conn, "A; B", &ran), trl_conn_failure(conn));
    trl_ran_free(ran);
    trl_statement *statement = NULL;
    fine = fine && allowed(trl_prepare(conn, "S", &statement), trl_conn_failure(conn));
    if (statement != NULL) {
        result = NULL;
        fine = fine && allowed(trl_execute(conn, statement, params, &result), trl_conn_failure(conn));
        trl_result_free(result);
        trl_cursor cursor = {statement, 1U};
        result = NULL;
        fine = fine && allowed(trl_cursor_open(conn, cursor, params, 1U, &result), trl_conn_failure(conn));
        trl_result_free(result);
        result = NULL;
        fine = fine && allowed(trl_describe_cursor(conn, cursor, &result), trl_conn_failure(conn));
        trl_result_free(result);
        trl_statement *described = NULL;
        fine = fine && allowed(trl_describe(conn, statement, &described), trl_conn_failure(conn));
        trl_statement_free(described);
        fine = fine && allowed(trl_deallocate(conn, statement), trl_conn_failure(conn));
    }
    trl_statement_free(statement);
    return fine;
}

/* The session's control: attributes, ChangeUser and Reset. */
static bool control_calls(trl_conn *conn) {
    static const char *const names[] = {"k"};
    static const char *const values[] = {"v"};
    trl_status set = trl_conn_set_query_attributes(conn, names, values, 1U);
    bool fine = set == TRL_OK || set == TRL_NO_MEMORY;
    trl_mechanism *trust = NULL;
    if (trl_mechanism_trust(&trust) == TRL_OK) {
        fine = fine && allowed(trl_conn_change_user(conn, "ann", trust), trl_conn_failure(conn));
    }
    trl_mechanism_free(trust);
    return fine && allowed(trl_conn_reset(conn), trl_conn_failure(conn));
}

/* A session of statements over a script, and an XML value parsed. */
static bool statements_session(void) {
    script s;
    script_init(&s, statement_serving, NULL);
    trl_mechanism *trust = NULL;
    trl_params *params = NULL;
    bool fine = true;
    if (trl_mechanism_trust(&trust) == TRL_OK && trl_params_new(&params) == TRL_OK) {
        fine = parameters(params);
        trl_conn *conn = NULL;
        trl_status made = script_connect(&s, trust, NULL, &conn);
        fine = fine && allowed(made, trl_conn_failure(conn));
        if (made == TRL_OK) {
            fine = fine && statement_calls(conn, params) && control_calls(conn);
        }
        trl_conn_free(conn);
    }
    trl_xml *xml = NULL;
    static const char text[] = "<a xmlns:p='u' p:b='1'>t<!--c--><?p i?></a>";
    trl_status parsed = trl_xml_parse(text, strlen(text), &xml);
    fine = fine && (parsed == TRL_OK || parsed == TRL_NO_MEMORY ||
                    (parsed == TRL_FAILED && trl_failure_sqlcode(trl_xml_failure(xml)) == -918));
    trl_xml_free(xml);
    trl_params_free(params);
    trl_mechanism_free(trust);
    script_free(&s);
    return fine;
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a session of statements,
 * prepared statements, cursors, results kept, parameters of every kind
 * and the session's control failing in turn fails the call as memory not
 * had, or the connection lost, and leaks nothing.
 */
static void statements_survive_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(statements_session));
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
        TRL_TEST_CASE(a_session_survives_each_fault),     TRL_TEST_CASE(a_scram_login_survives_each_fault),
        TRL_TEST_CASE(handles_survive_each_fault),        TRL_TEST_CASE(statements_survive_each_fault),
#ifdef FAULTS_TLS
        TRL_TEST_CASE(a_tls_session_survives_each_fault),
#endif
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
