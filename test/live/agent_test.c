/**
 * @file agent_test.c
 * @brief The SQL-agent against a real server (RULES.md R-TEST-8), as
 * turborel's command-line client's tests run it: connections made,
 * chosen and ended by CONNECT, SET CONNECTION and DISCONNECT, and each
 * refusal by its SQLSTATE.
 */
#include "codec/error.h"
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
#define HOST_BYTES 256U
#define SQL_BYTES 256U
#define DECIMAL 10

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    trl_result_free(result);
    return ran == TRL_OK;
}

/* Connects to `server`, a host and a port, or for none the test's server,
   as `user`, or ann, by trust. */
static trl_status dial(void *context, const char *server, const char *user, trl_conn **out) {
    (void)context;
    char host[HOST_BYTES] = "127.0.0.1";
    unsigned long port = SERVER_PORT;
    const char *colon = server != NULL ? strrchr(server, ':') : NULL;
    if (server != NULL) {
        size_t length = colon != NULL ? (size_t)(colon - server) : strlen(server);
        length = length < sizeof host - 1U ? length : sizeof host - 1U;
        memcpy(host, server, length);
        host[length] = '\0';
        port = colon != NULL ? strtoul(colon + 1, NULL, DECIMAL) : SERVER_PORT;
    }
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    (void)trl_target_tcp(host, (uint16_t)port, &target);
    (void)trl_mechanism_trust(&trust);
    trl_status made = trl_connect(target, user != NULL ? user : "ann", trust, NULL, out);
    trl_mechanism_free(trust);
    trl_target_free(target);
    return made;
}

/* The one value of the one row `sql` gives, as text, into `out`. */
static bool value_of(trl_agent *agent, const char *sql, char *out, size_t capacity) {
    trl_result *result = NULL;
    bool one = trl_agent_query(agent, sql, &result) == TRL_OK && trl_result_row_count(result) == 1U &&
               trl_value_to_text(trl_result_value(result, 0U, 0U), out, capacity, NULL) == TRL_OK;
    trl_result_free(result);
    return one;
}

/* The SQLSTATE `sql` fails with, or of its completion. */
static const char *state_of(trl_agent *agent, const char *sql) {
    static char state[TRL_SQLSTATE_BYTES];
    trl_result *result = NULL;
    trl_status ran = trl_agent_query(agent, sql, &result);
    const char *told = ran == TRL_OK ? trl_result_sqlstate(result) : trl_failure_sqlstate(trl_agent_failure(agent));
    memcpy(state, told, TRL_SQLSTATE_BYTES);
    trl_result_free(result);
    return state;
}

/**
 * Verifies: [CLIENT The SQL-agent] CONNECT TO an address AS a name makes
 * a connection to that server, as the user named, current; SET
 * CONNECTION makes another current, DEFAULT the one the agent began
 * with; DISCONNECT ends one, and with the current one ended, none is
 * current until one is made or chosen; CONNECT TO DEFAULT makes the
 * default one again.
 */
static void connections_are_made_chosen_and_ended(trl_test *test) {
    trl_agent *agent = NULL;
    TRL_CHECK(test, trl_agent_connected(dial, NULL, live_trusted(NULL), &agent) == TRL_OK);
    char sql[SQL_BYTES];
    char value[SQL_BYTES];
    trl_format(sql, sizeof sql, "CONNECT TO '127.0.0.1:%u' AS 'other' USER 'y'", (unsigned int)SERVER_PORT);
    TRL_CHECK(test, strcmp(state_of(agent, sql), "00000") == 0);
    TRL_CHECK(test, strcmp(trl_agent_current_name(agent), "OTHER") == 0);
    TRL_CHECK(test, value_of(agent, "VALUES CURRENT_USER", value, sizeof value) && strcmp(value, "Y") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "COMMIT"), "00000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION DEFAULT"), "00000") == 0);
    TRL_CHECK(test, value_of(agent, "VALUES CURRENT_USER", value, sizeof value) && strcmp(value, "ANN") == 0);
    TRL_CHECK(test, value_of(agent, "SELECT COUNT(*) FROM T", value, sizeof value) && strcmp(value, "1") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "COMMIT"), "00000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION ' OTHER '"), "00000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT CURRENT"), "00000") == 0);
    TRL_CHECK(test, trl_agent_current(agent) == NULL && trl_agent_count(agent) == 1U);
    TRL_CHECK(test, strcmp(state_of(agent, "SELECT COUNT(*) FROM T"), "08003") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION DEFAULT"), "00000") == 0);
    TRL_CHECK(test, value_of(agent, "SELECT COUNT(*) FROM T", value, sizeof value) && strcmp(value, "1") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "COMMIT"), "00000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT ALL"), "00000") == 0 && trl_agent_count(agent) == 0U);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO DEFAULT"), "00000") == 0);
    TRL_CHECK(test, value_of(agent, "SELECT COUNT(*) FROM T", value, sizeof value) && strcmp(value, "1") == 0);
    TRL_CHECK(test, trl_agent_terminate(agent) == TRL_OK);
    trl_agent_free(agent);
}

/**
 * Verifies: [CLIENT The SQL-agent] each refusal by its SQLSTATE: none
 * such (08003), a name in use (08002), a name or user not an identifier
 * (2E000, 28000), a server not reached (08001); CONNECT or SET
 * CONNECTION in a transaction (0A001), DISCONNECT of a connection whose
 * transaction is active (25000); the connections held go on.
 */
static void connection_statements_are_refused_saying_why(trl_test *test) {
    trl_agent *agent = NULL;
    (void)trl_agent_connected(dial, NULL, live_trusted(NULL), &agent);
    char to[SQL_BYTES];
    trl_format(to, sizeof to, "127.0.0.1:%u", (unsigned int)SERVER_PORT);
    char statements[8][SQL_BYTES];
    trl_format(statements[0], SQL_BYTES, "CONNECT TO '%s'", to);
    trl_format(statements[1], SQL_BYTES, "CONNECT TO '%s' AS '1x'", to);
    trl_format(statements[2], SQL_BYTES, "CONNECT TO '%s' AS 'C' USER 'a b'", to);
    trl_format(statements[3], SQL_BYTES, "CONNECT TO '%s' AS 'C'", to);
    trl_format(statements[4], SQL_BYTES, "CONNECT TO '%s' AS 'c'", to);
    const struct {
        const char *sql;
        const char *state;
    } refusals[] = {
        {"SET CONNECTION 'NOPE'", "08003"},
        {"DISCONNECT 'NOPE'", "08003"},
        {"CONNECT TO DEFAULT", "08002"},
        {statements[0], "2E000"},
        {statements[1], "2E000"},
        {statements[2], "28000"},
        {"CONNECT TO '127.0.0.1:1' AS 'C'", "08001"},
        {"CONNECT TO 'no-such-host.invalid' AS 'C'", "08001"},
        {"SELECT COUNT(*) FROM T", "00000"},
        {statements[3], "0A001"},
        {"SET CONNECTION DEFAULT", "0A001"},
        {"DISCONNECT DEFAULT", "25000"},
        {"DISCONNECT ALL", "25000"},
        {"COMMIT", "00000"},
        {statements[3], "00000"},
        {statements[4], "08002"},
        {"DISCONNECT 'C'", "00000"},
        {"DISCONNECT DEFAULT", "00000"},
        {"DISCONNECT CURRENT", "08003"},
    };
    for (size_t i = 0U; i < sizeof refusals / sizeof refusals[0]; ++i) {
        const char *state = state_of(agent, refusals[i].sql);
        if (strcmp(state, refusals[i].state) != 0) {
            (void)fprintf(stderr, "%s: %s, not %s\n", refusals[i].sql, state, refusals[i].state);
            TRL_CHECK(test, false);
        }
    }
    TRL_CHECK(test, trl_agent_count(agent) == 0U);
    trl_agent_free(agent);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-agent"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    trl_format(dir, sizeof dir, "%s/live-agent", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") &&
                 run(owner, "CREATE TABLE T (K INTEGER)") && run(owner, "INSERT INTO T VALUES (1)") &&
                 run(owner, "GRANT SELECT ON T TO PUBLIC") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(connections_are_made_chosen_and_ended),
        TRL_TEST_CASE(connection_statements_are_refused_saying_why),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
