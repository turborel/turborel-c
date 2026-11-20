/**
 * @file standard_test.c
 * @brief The standard API's shortest paths against a real server (RULES.md
 * R-TEST-8): a connection made of a URI, a statement run with its
 * parameters as text, and a value read as text by its row and column.
 */
#include "format.h"
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
#define URI_BYTES 256U

/* The URI of the test's server, as ann, by trust. */
static char uri[URI_BYTES];

/**
 * Verifies: [DESIGN] D2 a connection made of a URI; a URI that cannot be
 * read, or a server not reached, a connection holding why; a null
 * pointer refused.
 */
static void a_connection_is_made_of_a_uri(trl_test *test) {
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_connect_uri(uri, &conn) == TRL_OK && strcmp(trl_conn_authorization(conn), "ANN") == 0);
    trl_conn_free(conn);
    TRL_CHECK(test, trl_connect_uri("http://ann@127.0.0.1", &conn) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08001") == 0);
    trl_conn_free(conn);
    TRL_CHECK(test, trl_connect_uri("turborel://ann@127.0.0.1:1/?tls_mode=disable", &conn) == TRL_FAILED);
    TRL_CHECK(test, trl_conn_is_lost(conn));
    trl_conn_free(conn);
    TRL_CHECK(test, trl_connect_uri(NULL, &conn) == TRL_INVALID_ARGUMENT && conn == NULL);
    TRL_CHECK(test, trl_connect_uri(uri, NULL) == TRL_INVALID_ARGUMENT);
}

/**
 * Verifies: [DESIGN] D2 a statement run with its parameters as text, each
 * assigned to its parameter's type by the server, a null pointer the
 * null value; one with none run as by trl_query(); a value's text by its
 * row and column, kept by the result, null of the null value and past
 * the last; a failure the connection's, the statement destroyed still.
 */
static void parameters_go_as_text_and_values_come_as_text(trl_test *test) {
    trl_conn *conn = NULL;
    (void)trl_connect_uri(uri, &conn);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query_params(conn, "CREATE TABLE S (ID INTEGER, NAME VARCHAR(20), D DATE)", 0U, NULL,
                                     &result) == TRL_OK);
    trl_result_free(result);
    const char *const row[] = {"1", "Ann", "2020-01-31"};
    const char *const nulls[] = {"2", NULL, NULL};
    TRL_CHECK(test, trl_query_params(conn, "INSERT INTO S VALUES (?, ?, ?)", 3U, row, &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, trl_query_params(conn, "INSERT INTO S VALUES (?, ?, ?)", 3U, nulls, &result) == TRL_OK);
    trl_result_free(result);
    const char *const below[] = {"3"};
    TRL_CHECK(test, trl_query_params(conn, "SELECT ID, NAME, D FROM S WHERE ID < ? ORDER BY ID", 1U, below, &result) ==
                        TRL_OK);
    TRL_CHECK(test, trl_result_row_count(result) == 2U);
    const char *name = trl_result_text(result, 0U, 1U);
    TRL_CHECK(test, name != NULL && strcmp(name, "Ann") == 0 && trl_result_text(result, 0U, 1U) == name);
    TRL_CHECK(test, strcmp(trl_result_text(result, 0U, 2U), "2020-01-31") == 0);
    TRL_CHECK(test, trl_result_text(result, 1U, 1U) == NULL && trl_result_text(result, 2U, 0U) == NULL);
    TRL_CHECK(test, trl_result_text(result, 0U, 3U) == NULL && trl_result_text(NULL, 0U, 0U) == NULL);
    trl_result_free(result);
    const char *const wrong[] = {"not a number"};
    TRL_CHECK(test, trl_query_params(conn, "SELECT ID FROM S WHERE ID = ?", 1U, wrong, &result) == TRL_FAILED);
    TRL_CHECK(test, result == NULL && trl_failure_sqlcode(trl_conn_failure(conn)) != 0);
    TRL_CHECK(test, trl_query_params(conn, "SELECT FROM", 1U, wrong, &result) == TRL_FAILED);
    TRL_CHECK(test, trl_query_params(conn, "ROLLBACK", 0U, NULL, &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, trl_query_params(NULL, "VALUES 1", 0U, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_query_params(conn, "VALUES ?", 1U, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_query_params(conn, NULL, 0U, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_query_params(conn, "VALUES 1", 0U, NULL, NULL) == TRL_INVALID_ARGUMENT);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-standard"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-standard", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_format(uri, sizeof uri, "turborel://ann@127.0.0.1:%u/?tls_mode=disable", (unsigned int)SERVER_PORT);
    trl_conn *owner = live_trusted(NULL);
    trl_result *result = NULL;
    bool ready = owner != NULL && trl_query(owner, "CREATE SCHEMA AUTHORIZATION ANN", &result) == TRL_OK;
    trl_result_free(result);
    ready = ready && trl_query(owner, "COMMIT", &result) == TRL_OK;
    trl_result_free(result);
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_connection_is_made_of_a_uri),
        TRL_TEST_CASE(parameters_go_as_text_and_values_come_as_text),
    };
    int run_result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return run_result;
}
