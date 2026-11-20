/**
 * @file session_test.c
 * @brief The session against a real server (RULES.md R-TEST-8): what a
 * completion tells, notices and conditions, fetch-ends, Reset, ChangeUser,
 * query attributes, several statements as one request, and UUID and XML
 * values. Opt-in: TRL_TEST_SERVER names the server, else skipped.
 */
#include "format.h"
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
#define UUID_TEXT "919108f7-52d1-4320-9bac-f847db4148a8"
#define CONDITIONS_ASKED 2U

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    if (ran != TRL_OK) {
        char line[PATH_BYTES];
        (void)trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        (void)fprintf(stderr, "%s: %s\n", sql, line);
    }
    trl_result_free(result);
    return ran == TRL_OK;
}

/* Whether the result's first value is the text `text`. */
static bool first_text_is(const trl_result *result, const char *text) {
    size_t length = 0U;
    const char *bytes = trl_value_text(trl_result_value(result, 0U, 0U), &length);
    return bytes != NULL && length == strlen(text) && memcmp(bytes, text, length) == 0;
}

/* A trusted connection asking for every capability but those named. */
static trl_conn *asking_all_but(const char *left_out) {
    static const char *const all[] = {
        TRL_CAPABILITY_NOTICES,
        TRL_CAPABILITY_STATUS,
        TRL_CAPABILITY_DIAGNOSTICS,
        TRL_CAPABILITY_DESCRIBE,
        TRL_CAPABILITY_CURSORS,
        TRL_CAPABILITY_PING,
        TRL_CAPABILITY_RESET,
        TRL_CAPABILITY_CHANGE_USER,
        TRL_CAPABILITY_QUERY_ATTRIBUTES,
        TRL_CAPABILITY_MULTI_STATEMENT,
        TRL_CAPABILITY_COMMIT_POSITION,
        TRL_CAPABILITY_ROW_COUNTS,
        TRL_CAPABILITY_GENERATED_VALUES,
        TRL_CAPABILITY_FETCH_ENDS,
        TRL_CAPABILITY_CONDITIONS,
    };
    trl_conn_options *options = NULL;
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_ask_none(options);
    for (size_t i = 0U; i < sizeof all / sizeof all[0]; ++i) {
        if (left_out == NULL || strcmp(all[i], left_out) != 0) {
            (void)trl_conn_options_ask(options, all[i]);
        }
    }
    trl_conn *conn = live_trusted(options);
    trl_conn_options_free(options);
    return conn;
}

/**
 * Verifies: [CLIENT What the server tells] the rows a statement changed,
 * the identity values an INSERT generated in the order of its rows, the
 * server's time, and the last commit's log position
 * [PROTOCOL Complete 'K'].
 */
static void a_completion_tells_its_counts(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, trl_conn_set_autocommit(conn, true) == TRL_OK);
    TRL_CHECK(test, run(conn, "CREATE TABLE GV (ID INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY, N VARCHAR(5))"));
    TRL_CHECK(test, run(conn, "INSERT INTO GV (N) VALUES ('a'), ('b')"));
    uint64_t rows = 0U;
    TRL_CHECK(test, trl_conn_rows_affected(conn, &rows) && rows == 2U);
    TRL_CHECK(test, trl_conn_generated_count(conn) == 2U);
    TRL_CHECK(test,
              strcmp(trl_conn_generated_at(conn, 0U), "1") == 0 && strcmp(trl_conn_generated_at(conn, 1U), "2") == 0);
    TRL_CHECK(test, trl_conn_generated_at(conn, 2U) == NULL);
    trl_timing timing;
    TRL_CHECK(test, trl_conn_server_time(conn, &timing) && timing.total >= timing.queued);
    uint64_t position = 0U;
    TRL_CHECK(test, trl_conn_commit_position(conn, &position));
    TRL_CHECK(test, run(conn, "SELECT N FROM GV") && trl_conn_rows_affected(conn, &rows) && rows == 2U);
    TRL_CHECK(test, trl_conn_generated_count(conn) == 0U);
    trl_conn_free(conn);
}

/* Counts the notices it is given, keeping the last SQLSTATE. */
static void heard(void *context, const trl_failure *notice) {
    char *last = context;
    trl_format(last, 6U, "%s", trl_failure_sqlstate(notice));
}

/**
 * Verifies: [PROTOCOL Errors and notices] a statement's conditions told
 * one way: in its Complete with `conditions`, else as Notices, handed to a
 * handler or kept [CLIENT What the server tells].
 */
static void conditions_and_notices(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test,
              run(conn, "CREATE PROCEDURE WARNS () MODIFIES SQL DATA SIGNAL SQLSTATE '01ABC'") && run(conn, "COMMIT"));
    TRL_CHECK(test, run(conn, "CALL WARNS()") && trl_conn_condition_count(conn) == 1U);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_condition_at(conn, 0U)), "01ABC") == 0);
    TRL_CHECK(test, trl_conn_notice_count(conn) == 0U && trl_conn_condition_at(conn, 1U) == NULL);
    trl_conn_free(conn);
    conn = asking_all_but(TRL_CAPABILITY_CONDITIONS);
    TRL_CHECK(test, run(conn, "CALL WARNS()") && trl_conn_condition_count(conn) == 0U);
    TRL_CHECK(test, trl_conn_notice_count(conn) == 1U);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_notice_at(conn, 0U)), "01ABC") == 0);
    TRL_CHECK(test, trl_failure_sqlcode(trl_conn_notice_at(conn, 0U)) == 0);
    TRL_CHECK(test,
              strcmp(trl_failure_diagnostic(trl_conn_notice_at(conn, 0U), TRL_DIAGNOSTIC_SEVERITY), "WARNING") == 0);
    trl_conn_clear_notices(conn);
    TRL_CHECK(test, trl_conn_notice_count(conn) == 0U);
    char last[6] = "";
    TRL_CHECK(test, trl_conn_set_notice_handler(conn, heard, last) == TRL_OK);
    TRL_CHECK(test, run(conn, "CALL WARNS()") && strcmp(last, "01ABC") == 0 && trl_conn_notice_count(conn) == 0U);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT What the server tells] under autocommit with
 * `fetch-ends`, a cursor's transaction ends as it ends: the fetch of its
 * last row commits, close commits, and rows that fail roll it back
 * [PROTOCOL Conversations].
 */
static void a_cursor_ends_its_transaction(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, trl_conn_granted(conn, TRL_CAPABILITY_FETCH_ENDS));
    TRL_CHECK(test, trl_conn_set_autocommit(conn, true) == TRL_OK);
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT X FROM (VALUES (1), (2), (3)) AS V(X)", &statement) == TRL_OK);
    trl_cursor cursor = {statement, 0U};
    trl_result *batch = NULL;
    TRL_CHECK(test, trl_cursor_open(conn, cursor, NULL, 2U, &batch) == TRL_OK && trl_result_more(batch));
    TRL_CHECK(test, trl_conn_in_transaction(conn));
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_fetch(conn, cursor, 2U, &batch) == TRL_OK && !trl_result_more(batch));
    TRL_CHECK(test, trl_result_row_count(batch) == 1U && !trl_conn_in_transaction(conn));
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_open(conn, cursor, NULL, 1U, &batch) == TRL_OK && trl_conn_in_transaction(conn));
    trl_result_free(batch);
    TRL_CHECK(test, trl_cursor_close(conn, cursor) == TRL_OK && !trl_conn_in_transaction(conn));
    trl_statement *failing = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT 10 / (X - 3) FROM (VALUES (1), (2), (3)) AS V(X)", &failing) == TRL_OK);
    trl_cursor dividing = {failing, 0U};
    batch = NULL;
    TRL_CHECK(test, trl_cursor_open(conn, dividing, NULL, 1U, &batch) == TRL_FAILED && batch == NULL);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "22012") == 0);
    TRL_CHECK(test, !trl_conn_in_transaction(conn) && !trl_conn_is_lost(conn));
    trl_statement_free(failing);
    trl_statement_free(statement);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL Query 'Q'] under autocommit, a result that sent a
 * large object's locator keeps its transaction; one sent whole does not
 * [CLIENT Running statements].
 */
static void a_locator_keeps_its_transaction(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE LK (N INTEGER, BODY CLOB(1M))") && run(conn, "COMMIT"));
    TRL_CHECK(test, trl_conn_set_autocommit(conn, true) == TRL_OK);
    static char big[70000];
    memset(big, 'x', sizeof big);
    trl_statement *insert = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO LK VALUES (?, ?)", &insert) == TRL_OK);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    trl_decimal one;
    memset(&one, 0, sizeof one);
    one.units.low = 1U;
    (void)trl_params_add_decimal(params, one);
    (void)trl_params_add_text(params, big, sizeof big);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, insert, params, &result) == TRL_OK && !trl_conn_in_transaction(conn));
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT N FROM LK", &result) == TRL_OK && !trl_conn_in_transaction(conn));
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM LK", &result) == TRL_OK && trl_conn_in_transaction(conn));
    TRL_CHECK(test, trl_value_kind(trl_result_value(result, 0U, 0U)) == TRL_KIND_LOB_LOCATOR);
    trl_result_free(result);
    trl_params_free(params);
    trl_statement_free(insert);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL Reset 'W'] Reset rolls the transaction back, told by
 * notice 01T01, and its prepared statements go; the session's user stays
 * [CLIENT Controlling the session] [PROTOCOL Conversations].
 */
static void reset_renews_the_session(trl_test *test) {
    trl_conn *conn = asking_all_but(TRL_CAPABILITY_CONDITIONS);
    TRL_CHECK(test, run(conn, "CREATE TABLE RS (ID INTEGER)") && trl_conn_in_transaction(conn));
    trl_statement *statement = NULL;
    TRL_CHECK(test, trl_prepare(conn, "SELECT 1 FROM RS", &statement) == TRL_OK);
    TRL_CHECK(test, trl_conn_reset(conn) == TRL_OK && !trl_conn_in_transaction(conn));
    TRL_CHECK(test, trl_conn_notice_count(conn) == 1U);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_notice_at(conn, 0U)), "01T01") == 0);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, statement, NULL, &result) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_conn_authorization(conn), "ANN") == 0);
    trl_statement_free(statement);
    trl_conn_free(conn);
}

/**
 * Verifies: [PROTOCOL ChangeUser 'U'] ChangeUser takes the same exchange
 * as Hello's, and Ready renews the session as another user's, its
 * transaction ended; refused, the connection fails
 * [CLIENT Controlling the session].
 */
static void change_user_renews_as_another(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE USER BOB PASSWORD 'bob secret'") && run(conn, "COMMIT"));
    TRL_CHECK(test, run(conn, "CREATE TABLE CU (ID INTEGER)") && trl_conn_in_transaction(conn));
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    TRL_CHECK(test, trl_conn_change_user(conn, "bob", trust) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_conn_authorization(conn), "BOB") == 0 && !trl_conn_in_transaction(conn));
    trl_mechanism_free(trust);
    trl_conn_free(conn);
    trl_target *local = NULL;
    (void)trl_target_local(live_server.socket, &local);
    trl_mechanism *password = NULL;
    (void)trl_mechanism_password(SERVER_PASSWORD, &password);
    conn = live_connect(local, password, NULL);
    trl_mechanism *wrong = NULL;
    (void)trl_mechanism_password("not bob's", &wrong);
    TRL_CHECK(test, conn != NULL && trl_conn_change_user(conn, "bob", wrong) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "28000") == 0 && trl_conn_is_lost(conn));
    trl_mechanism_free(wrong);
    trl_mechanism_free(password);
    trl_target_free(local);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Controlling the session] query attributes carried by
 * every later statement until set again; several statements as one
 * request, each outcome and the failure that stopped the rest
 * [PROTOCOL Query 'Q'].
 */
static void attributes_and_several_statements(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    static const char *const names[] = {"k"};
    static const char *const values[] = {"v"};
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, names, values, 1U) == TRL_OK);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "VALUES (QUERY_ATTRIBUTE('k'))", &result) == TRL_OK);
    TRL_CHECK(test, first_text_is(result, "v"));
    trl_result_free(result);
    TRL_CHECK(test, trl_conn_set_query_attributes(conn, NULL, NULL, 0U) == TRL_OK);
    TRL_CHECK(test, trl_query(conn, "VALUES (QUERY_ATTRIBUTE('k'))", &result) == TRL_OK);
    TRL_CHECK(test, trl_value_is_null(trl_result_value(result, 0U, 0U)));
    trl_result_free(result);
    trl_ran *ran = NULL;
    TRL_CHECK(test, trl_query_many(conn, "VALUES (1); CREATE TABLE MS (ID INTEGER); SELECT NOPE; VALUES (2)", &ran) ==
                        TRL_OK);
    TRL_CHECK(test, trl_ran_count(ran) == 2U && trl_result_has_rows(trl_ran_result(ran, 0U)));
    TRL_CHECK(test, !trl_result_has_rows(trl_ran_result(ran, 1U)) && trl_ran_result(ran, 2U) == NULL);
    TRL_CHECK(test, trl_ran_failure(ran) != NULL && !trl_conn_is_lost(conn));
    trl_ran_free(ran);
    TRL_CHECK(test, trl_query_many(conn, "VALUES (1); VALUES (2)", &ran) == TRL_OK && trl_ran_count(ran) == 2U);
    TRL_CHECK(test, trl_ran_failure(ran) == NULL);
    trl_ran_free(ran);
    TRL_CHECK(test, trl_query_many(conn, "", &ran) == TRL_OK && trl_ran_count(ran) == 1U);
    TRL_CHECK(test, !trl_result_has_rows(trl_ran_result(ran, 0U)) && trl_ran_failure(ran) == NULL);
    trl_ran_free(ran);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT UUIDs] a UUID read as its 16 octets and its text, and
 * sent as a parameter made of its text's octets.
 */
static void a_uuid_goes_both_ways(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE TK (ID UUID)"));
    trl_statement *insert = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO TK VALUES (?)", &insert) == TRL_OK);
    uint8_t octets[TRL_UUID_BYTES];
    TRL_CHECK(test, trl_uuid_parse(UUID_TEXT, octets) == TRL_OK);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    (void)trl_params_add_uuid(params, octets);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, insert, params, &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT ID FROM TK", &result) == TRL_OK);
    uint8_t read[TRL_UUID_BYTES];
    char text[TRL_UUID_TEXT_BYTES];
    TRL_CHECK(test, trl_value_uuid(trl_result_value(result, 0U, 0U), read) == TRL_OK);
    TRL_CHECK(test, trl_uuid_text(read, text) == TRL_OK && strcmp(text, UUID_TEXT) == 0);
    trl_result_free(result);
    trl_params_free(params);
    trl_statement_free(insert);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT XML values] an XML value read as its serialization, a
 * parameter made of its text, its nodes parsed from it; a value with no
 * serialization fails the fetch (SQLSTATE 2200W).
 */
static void an_xml_value_goes_both_ways(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE XO (BODY XML)"));
    trl_statement *insert = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO XO VALUES (?)", &insert) == TRL_OK);
    static const char order[] = "<order no=\"18\"><item>jam</item></order>";
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    (void)trl_params_add_xml(params, order, sizeof order - 1U);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, insert, params, &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM XO", &result) == TRL_OK && first_text_is(result, order));
    TRL_CHECK(test, trl_value_kind(trl_result_value(result, 0U, 0U)) == TRL_KIND_XML);
    trl_xml *nodes = NULL;
    TRL_CHECK(test, trl_value_xml(trl_result_value(result, 0U, 0U), &nodes) == TRL_OK);
    const trl_xml_node *element = trl_xml_node_child(trl_xml_root(nodes), 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_name(element), "order") == 0);
    TRL_CHECK(test, strcmp(trl_xml_node_value(trl_xml_node_attribute(element, 0U)), "18") == 0);
    trl_xml_free(nodes);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "VALUES (XMLQUERY('attribute a {1}' RETURNING SEQUENCE BY REF EMPTY ON EMPTY))",
                              &result) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "2200W") == 0);
    trl_params_free(params);
    trl_statement_free(insert);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-session"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-session", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, true, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_completion_tells_its_counts),   TRL_TEST_CASE(conditions_and_notices),
        TRL_TEST_CASE(a_cursor_ends_its_transaction),   TRL_TEST_CASE(reset_renews_the_session),
        TRL_TEST_CASE(change_user_renews_as_another),   TRL_TEST_CASE(attributes_and_several_statements),
        TRL_TEST_CASE(a_uuid_goes_both_ways),           TRL_TEST_CASE(an_xml_value_goes_both_ways),
        TRL_TEST_CASE(a_locator_keeps_its_transaction),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
