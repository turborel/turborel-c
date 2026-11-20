/**
 * @file agent_test.c
 * @brief The SQL-agent over scripted servers: its connection statements
 * read, run and refused, and every other statement sent to the current
 * connection.
 */
#include "agent/statement.h"
#include "codec/error.h"
#include "conn/conn.h"
#include "failure.h"
#include "format.h"
#include "harness.h"
#include "script.h"
#include "serving.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The most connections a test's connector makes. */
#define MADE_MOST 6U

/* How the connector answers. */
typedef enum making {
    MAKING_SCRIPTED = 0,
    MAKING_LOST,
    MAKING_UNABLE,
    MAKING_REFUSED,
    MAKING_NO_MEMORY,
    MAKING_NOTHING,
} making;

/* A connector of scripted connections, and what it was asked. */
typedef struct maker {
    making how;
    script scripts[MADE_MOST];
    serving_sequence plans[MADE_MOST];
    size_t made;
    char server[64];
    char user[64];
    bool had_server;
    bool had_user;
} maker;

/* A connection lost before it was made, holding a failure of `sqlstate`. */
static trl_conn *failed_of(const char *sqlstate) {
    trl_failure failure;
    trl_failure_init(&failure);
    trl_failure_set(&failure, -919, sqlstate, "not reached");
    trl_conn *conn = trl_conn_failed_with(&failure);
    trl_failure_clear(&failure);
    return conn;
}

static trl_status make(void *context, const char *server, const char *user, trl_conn **out) {
    maker *m = context;
    m->had_server = server != NULL;
    m->had_user = user != NULL;
    trl_format(m->server, sizeof m->server, "%s", server != NULL ? server : "");
    trl_format(m->user, sizeof m->user, "%s", user != NULL ? user : "");
    switch (m->how) {
    case MAKING_LOST:
        *out = failed_of("08006");
        return TRL_FAILED;
    case MAKING_UNABLE:
        *out = failed_of("08001");
        return TRL_FAILED;
    case MAKING_REFUSED:
        *out = failed_of("28000");
        return TRL_FAILED;
    case MAKING_NO_MEMORY:
        *out = NULL;
        return TRL_NO_MEMORY;
    case MAKING_NOTHING:
        *out = NULL;
        return TRL_OK;
    case MAKING_SCRIPTED:
    default:
        break;
    }
    size_t at = m->made++;
    serving_sequence plan = {{PLAN_DONE, true}, NULL, 0U, 0U};
    m->plans[at] = plan;
    *out = serving_connect_sequence(&m->scripts[at], &m->plans[at]);
    return TRL_OK;
}

/* Frees the scripts a maker made. */
static void maker_free(maker *m) {
    for (size_t i = 0U; i < m->made; ++i) {
        script_free(&m->scripts[i]);
    }
}

/* Reads `sql`; its verb, or -1 if it failed, its failure's SQLCODE in `sqlcode`. */
static int read_as(const char *sql, trl_agent_statement *statement, int32_t *sqlcode, uint64_t *position) {
    trl_failure failure;
    trl_failure_init(&failure);
    trl_agent_statement_free(statement);
    trl_agent_statement_init(statement);
    bool read = trl_agent_statement_read(sql, statement, &failure);
    *sqlcode = failure.sqlcode;
    *position = failure.placed ? failure.position : UINT64_MAX;
    trl_failure_clear(&failure);
    return read ? (int)statement->verb : -1;
}

/* Whether `value` is the text `text`. */
static bool text_is(const trl_agent_text *value, const char *text) {
    return value->kind == TRL_VALUE_TEXT && strcmp((const char *)value->text.data, text) == 0;
}

/**
 * Verifies: [CLIENT The SQL-agent] CONNECT, SET CONNECTION and DISCONNECT
 * are read as Turborel reads them, in any case, a semicolon at the end;
 * a literal's quoted parts joined across newlines, a quote written twice,
 * the introducer of SQL_TEXT taken; a newline LF or CR LF, a simple
 * comment ended by one, a bracketed comment holding others nested in it,
 * as Turborel's lexer reads them; any statement beginning otherwise is
 * another's.
 */
static void connection_statements_are_read(trl_test *test) {
    trl_agent_statement st;
    trl_agent_statement_init(&st);
    int32_t code = 0;
    uint64_t at = 0U;
    TRL_CHECK(test, read_as("SELECT 1", &st, &code, &at) == TRL_AGENT_OTHER);
    TRL_CHECK(test, read_as("SET SCHEMA 'X'", &st, &code, &at) == TRL_AGENT_OTHER);
    TRL_CHECK(test, read_as("/* open", &st, &code, &at) == TRL_AGENT_OTHER);
    TRL_CHECK(test, read_as("/* a /* b */ c */\r\nCONNECT TO 'n'", &st, &code, &at) == TRL_AGENT_CONNECT);
    TRL_CHECK(test, read_as("-- a\rb\nDISCONNECT ALL", &st, &code, &at) == TRL_AGENT_DISCONNECT);
    TRL_CHECK(test, read_as("/* a /* b */ CONNECT TO 'n'", &st, &code, &at) == TRL_AGENT_OTHER);
    TRL_CHECK(test, read_as(" set connection default ;", &st, &code, &at) == TRL_AGENT_SET);
    TRL_CHECK(test, st.object == TRL_AGENT_DEFAULT);
    TRL_CHECK(test, read_as("SET CONNECTION 'c'", &st, &code, &at) == TRL_AGENT_SET && text_is(&st.name, "c"));
    TRL_CHECK(test, read_as("connect to default", &st, &code, &at) == TRL_AGENT_CONNECT_DEFAULT);
    TRL_CHECK(test, read_as("CONNECT TO 'a' AS 'b' USER 'c' -- said\n", &st, &code, &at) == TRL_AGENT_CONNECT);
    TRL_CHECK(test, text_is(&st.server, "a") && text_is(&st.name, "b") && text_is(&st.user, "c"));
    TRL_CHECK(test, read_as("CONNECT TO 'a' USER 'c'", &st, &code, &at) == TRL_AGENT_CONNECT);
    TRL_CHECK(test, st.name.kind == TRL_VALUE_ABSENT && text_is(&st.user, "c"));
    TRL_CHECK(test, read_as("CONNECT TO 'it''s'\n  'here'", &st, &code, &at) == TRL_AGENT_CONNECT);
    TRL_CHECK(test, text_is(&st.server, "it'shere"));
    TRL_CHECK(test,
              read_as("CONNECT TO _sql_text 'x'", &st, &code, &at) == TRL_AGENT_CONNECT && text_is(&st.server, "x"));
    TRL_CHECK(test, read_as("DISCONNECT ALL", &st, &code, &at) == TRL_AGENT_DISCONNECT && st.object == TRL_AGENT_ALL);
    TRL_CHECK(test, read_as("DISCONNECT CURRENT", &st, &code, &at) == TRL_AGENT_DISCONNECT);
    TRL_CHECK(test, st.object == TRL_AGENT_CURRENT);
    TRL_CHECK(test, read_as("DISCONNECT DEFAULT;", &st, &code, &at) == TRL_AGENT_DISCONNECT);
    TRL_CHECK(test, st.object == TRL_AGENT_DEFAULT);
    TRL_CHECK(test, read_as("DISCONNECT 'x'", &st, &code, &at) == TRL_AGENT_DISCONNECT && text_is(&st.name, "x"));
    static const char *const others[] = {
        "CONNECT TO ?",
        "CONNECT TO 12",
        "CONNECT TO -1.5E+3",
        "CONNECT TO .5e2",
        "CONNECT TO N'x'",
        "CONNECT TO X'00'",
        "CONNECT TO B'1'",
        "CONNECT TO DATE '2020-01-01'",
        "CONNECT TO TIME '1:2:3'",
        "CONNECT TO TIMESTAMP '2020-01-01 1:2:3'",
        "CONNECT TO TRUE",
        "CONNECT TO FALSE",
        "CONNECT TO UNKNOWN",
    };
    bool every = true;
    for (size_t i = 0U; i < sizeof others / sizeof others[0]; ++i) {
        every = every && read_as(others[i], &st, &code, &at) == TRL_AGENT_CONNECT && st.server.kind == TRL_VALUE_OTHER;
    }
    TRL_CHECK(test, every);
    trl_agent_statement_free(&st);
}

/**
 * Verifies: [CLIENT The SQL-agent] a statement that begins as a
 * connection statement and is none fails as Turborel fails it, placed at
 * the token, in characters: not of its Format (SQLCODE -102), a literal
 * not ended or holding a newline (-101), an embedded variable or another
 * character set not implemented (-900).
 */
static void malformed_connection_statements_fail(trl_test *test) {
    static const struct {
        const char *sql;
        int32_t sqlcode;
        uint64_t position;
    } malformed[] = {
        {"CONNECT 'a'", -102, 8U},
        {"CONNECT TO", -102, 10U},
        {"CONNECT TO 'é' AS", -102, 17U},
        {"CONNECT TO 'a' 'b'", -102, 15U},
        {"CONNECT TO 'a' /* open", -102, 15U},
        {"CONNECT TO 'a", -101, 11U},
        {"CONNECT TO 'a\nb'", -101, 13U},
        {"CONNECT TO N'a", -101, 12U},
        {"CONNECT TO DATE", -102, 11U},
        {"CONNECT TO SELECT", -102, 11U},
        {"CONNECT TO :x", -900, 11U},
        {"CONNECT TO _latin1'x'", -900, 18U},
        {"CONNECT TO _sql_text x", -102, 21U},
        {"CONNECT TO 'a' AS", -102, 17U},
        {"CONNECT TO 'a' USER", -102, 19U},
        {"SET CONNECTION", -102, 14U},
        {"SET CONNECTION DEFAULT x", -102, 23U},
        {"DISCONNECT", -102, 10U},
        {"DISCONNECT ALL x", -102, 15U},
    };
    trl_agent_statement st;
    trl_agent_statement_init(&st);
    for (size_t i = 0U; i < sizeof malformed / sizeof malformed[0]; ++i) {
        int32_t code = 0;
        uint64_t at = 0U;
        int verb = read_as(malformed[i].sql, &st, &code, &at);
        if (verb != -1 || code != malformed[i].sqlcode || at != malformed[i].position) {
            (void)fprintf(stderr, "%s: %d %d %llu\n", malformed[i].sql, verb, (int)code, (unsigned long long)at);
            TRL_CHECK(test, false);
        }
    }
    trl_agent_statement_free(&st);
}

/* Runs `sql` by `agent`; the SQLSTATE of its failure, or of its completion. */
static const char *state_of(trl_agent *agent, const char *sql) {
    static char state[TRL_SQLSTATE_BYTES];
    trl_result *result = NULL;
    trl_status ran = trl_agent_query(agent, sql, &result);
    const char *told = ran == TRL_OK ? trl_result_sqlstate(result) : trl_failure_sqlstate(trl_agent_failure(agent));
    memcpy(state, told, TRL_SQLSTATE_BYTES);
    trl_result_free(result);
    return state;
}

/* Whether the agent's last failure was of `sqlcode` and said `message`. */
static bool failed_saying(const trl_agent *agent, int32_t sqlcode, const char *message) {
    const trl_failure *failure = trl_agent_failure(agent);
    return trl_failure_sqlcode(failure) == sqlcode && strstr(trl_failure_message(failure), message) != NULL;
}

/**
 * Verifies: [CLIENT The SQL-agent] an agent with no connection refuses
 * every statement but CONNECT and DISCONNECT ALL (SQLSTATE 08003); a name
 * that is no identifier, a user that is no authorization identifier and
 * a name that is no character string literal are refused before any
 * connection is made (2E000, 28000, SQLCODE -103).
 */
static void an_agent_without_connections_refuses(trl_test *test) {
    maker m = {0};
    trl_agent *agent = NULL;
    TRL_CHECK(test, trl_agent_new(make, &m, &agent) == TRL_OK && trl_agent_count(agent) == 0U);
    TRL_CHECK(test, trl_agent_current(agent) == NULL && trl_agent_current_name(agent) == NULL);
    TRL_CHECK(test, strcmp(state_of(agent, "SELECT 1"), "08003") == 0 && failed_saying(agent, -919, "no current"));
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT ALL"), "00000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION DEFAULT"), "08003") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "there is no default connection"));
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT CURRENT"), "08003") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT DEFAULT"), "08003") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT ' x '"), "08003") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "there is no connection X"));
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT '1x'"), "08003") == 0 && failed_saying(agent, -919, "1x"));
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'a b'"), "2E000") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "'a b' is not an <identifier>"));
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'a' USER 'a b'"), "28000") == 0);
    TRL_CHECK(test, failed_saying(agent, -104, "'a b' is not an <authorization identifier>"));
    TRL_CHECK(test, strcmp(trl_failure_rule(trl_agent_failure(agent)), "ISO/IEC 9075:1992 §15.1 GR3") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'a' AS ?"), "2A000") == 0);
    TRL_CHECK(test, failed_saying(agent, -103, "character string literals"));
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO ?"), "2A000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'a' USER 1"), "2A000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION 1"), "2A000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT ?"), "2A000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT 'a'"), "2A000") == 0 && failed_saying(agent, -102, "TO"));
    TRL_CHECK(test, m.made == 0U);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_agent_query(NULL, "SELECT 1", &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_agent_query(agent, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_agent_query(agent, "SELECT 1", NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_agent_query_batches(agent, "SELECT 1", NULL, NULL, &result) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_agent_query_batches(agent, "SELECT 1", NULL, NULL, NULL) == TRL_INVALID_ARGUMENT);
    trl_agent *none = agent;
    TRL_CHECK(test, trl_agent_new(NULL, &m, &none) == TRL_INVALID_ARGUMENT && none == NULL);
    TRL_CHECK(test, trl_agent_terminate(agent) == TRL_OK);
    trl_agent_free(agent);
    TRL_CHECK(test, trl_agent_new(make, &m, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_agent_terminate(NULL) == TRL_INVALID_ARGUMENT && trl_agent_count(NULL) == 0U);
    TRL_CHECK(test, trl_agent_current(NULL) == NULL && trl_agent_current_name(NULL) == NULL);
    TRL_CHECK(test, trl_agent_failure(NULL) == NULL);
    trl_agent_free(NULL);
}

/* Counts the batches given. */
static void counted(void *context, const trl_result *batch) {
    (void)batch;
    ++*(int *)context;
}

/**
 * Verifies: [CLIENT The SQL-agent] CONNECT TO a server makes a connection
 * through the program's connector, named as AS names it or else as the
 * server is, current; TO DEFAULT the default one; a name in use refused
 * (08002); SET CONNECTION chooses one by its name as an identifier, or as
 * written if it is none; every other statement goes to the current
 * connection, its failure the agent's.
 */
static void connections_are_made_and_chosen(trl_test *test) {
    maker m = {0};
    trl_agent *agent = NULL;
    (void)trl_agent_new(make, &m, &agent);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO ' srv '"), "00000") == 0);
    TRL_CHECK(test, strcmp(m.server, "srv") == 0 && !m.had_user && strcmp(trl_agent_current_name(agent), "SRV") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO DEFAULT"), "00000") == 0 && !m.had_server);
    TRL_CHECK(test, trl_agent_current_name(agent) == NULL && trl_agent_count(agent) == 2U);
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO DEFAULT"), "08002") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "the default connection is established"));
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'x' AS 'srv'"), "08002") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "the connection SRV is established"));
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'x' AS ' \"Mixed\" ' USER 'bob'"), "00000") == 0);
    TRL_CHECK(test, strcmp(m.user, "bob") == 0 && strcmp(trl_agent_current_name(agent), "Mixed") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION 'srv'"), "00000") == 0);
    TRL_CHECK(test, strcmp(trl_agent_current_name(agent), "SRV") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION '\"srv\"'"), "08003") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "there is no connection srv"));
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION 'a b'"), "08003") == 0);
    m.plans[0].then.plan = PLAN_ERROR;
    TRL_CHECK(test, strcmp(state_of(agent, "VALUES 1"), "42000") == 0);
    TRL_CHECK(test, failed_saying(agent, -103, "a statement refused"));
    m.plans[0].then.plan = PLAN_ROWS;
    trl_result *result = NULL;
    TRL_CHECK(test, trl_agent_query(agent, "VALUES 1", &result) == TRL_OK && trl_result_row_count(result) == 3U);
    trl_result_free(result);
    TRL_CHECK(test, trl_conn_in_transaction(trl_agent_current(agent)));
    TRL_CHECK(test, strcmp(state_of(agent, "SET CONNECTION DEFAULT"), "0A001") == 0);
    TRL_CHECK(test, failed_saying(agent, -900, "none spans servers"));
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'other'"), "0A001") == 0);
    int batches = 0;
    TRL_CHECK(test, trl_agent_query_batches(agent, "VALUES 1", counted, &batches, &result) == TRL_OK && batches == 2);
    trl_result_free(result);
    TRL_CHECK(test, trl_agent_query_batches(agent, "DISCONNECT '\"Mixed\"'", counted, &batches, &result) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_result_sqlstate(result), "00000") == 0 && !trl_result_has_rows(result));
    trl_result_free(result);
    TRL_CHECK(test, trl_agent_count(agent) == 2U && strcmp(trl_agent_current_name(agent), "SRV") == 0);
    trl_agent_free(agent);
    maker_free(&m);
}

/**
 * Verifies: [CLIENT The SQL-agent] DISCONNECT refuses to end a connection
 * whose transaction is active (SQLSTATE 25000); it ends the one it names,
 * or the current one, or all, none current after if the current one was
 * ended; an error ending one is a warning, 01002, the connection ended
 * still; ending the agent ends every connection, its first failure the
 * agent's.
 */
static void connections_are_ended(trl_test *test) {
    maker m = {0};
    trl_agent *agent = NULL;
    (void)trl_agent_new(make, &m, &agent);
    (void)state_of(agent, "CONNECT TO 'one'");
    (void)state_of(agent, "CONNECT TO 'two'");
    (void)state_of(agent, "CONNECT TO 'three'");
    m.plans[0].then.plan = PLAN_ROWS;
    (void)state_of(agent, "SET CONNECTION 'one'");
    (void)state_of(agent, "VALUES 1");
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT 'one'"), "25000") == 0);
    TRL_CHECK(test, failed_saying(agent, -917, "transaction is active"));
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT ALL"), "25000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT CURRENT"), "25000") == 0);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT 'two'"), "00000") == 0);
    TRL_CHECK(test, trl_agent_count(agent) == 2U && strcmp(trl_agent_current_name(agent), "ONE") == 0);
    m.plans[0].then.plan = PLAN_DONE;
    (void)state_of(agent, "COMMIT");
    TRL_CHECK(test, !trl_conn_in_transaction(trl_agent_current(agent)));
    m.scripts[0].fail_writes = true;
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT CURRENT"), "01002") == 0);
    TRL_CHECK(test, trl_agent_count(agent) == 1U && trl_agent_current(agent) == NULL);
    TRL_CHECK(test, strcmp(state_of(agent, "SELECT 1"), "08003") == 0);
    (void)state_of(agent, "CONNECT TO 'four'");
    m.scripts[3].fail_writes = true;
    TRL_CHECK(test, trl_agent_terminate(agent) == TRL_FAILED && trl_agent_count(agent) == 0U);
    TRL_CHECK(test, trl_failure_sqlcode(trl_agent_failure(agent)) != 0);
    trl_agent_free(agent);
    maker_free(&m);
    maker all = {0};
    (void)trl_agent_new(make, &all, &agent);
    (void)state_of(agent, "CONNECT TO 'one'");
    (void)state_of(agent, "CONNECT TO DEFAULT");
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT DEFAULT"), "00000") == 0 && trl_agent_count(agent) == 1U);
    TRL_CHECK(test, strcmp(state_of(agent, "DISCONNECT ALL"), "00000") == 0 && trl_agent_count(agent) == 0U);
    TRL_CHECK(test, trl_agent_terminate(agent) == TRL_OK);
    trl_agent_free(agent);
    maker_free(&all);
}

/**
 * Verifies: [CLIENT The SQL-agent] a connector that cannot reach the
 * server, or loses it, fails CONNECT with SQLSTATE 08001; one the server
 * refuses with 08004, its message kept; one without memory with it; and
 * an agent made connected holds its connection as the default one,
 * current.
 */
static void connector_failures_are_told(trl_test *test) {
    maker m = {0};
    trl_agent *agent = NULL;
    (void)trl_agent_new(make, &m, &agent);
    m.how = MAKING_LOST;
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'srv'"), "08001") == 0);
    TRL_CHECK(test, failed_saying(agent, -919, "not reached"));
    TRL_CHECK(test, strcmp(trl_failure_rule(trl_agent_failure(agent)), "ISO/IEC 9075:1992 §15.1 GR10, GR11") == 0);
    m.how = MAKING_UNABLE;
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'srv'"), "08001") == 0);
    m.how = MAKING_REFUSED;
    TRL_CHECK(test, strcmp(state_of(agent, "CONNECT TO 'srv' USER 'bob'"), "08004") == 0);
    m.how = MAKING_NO_MEMORY;
    trl_result *result = NULL;
    TRL_CHECK(test, trl_agent_query(agent, "CONNECT TO 'srv'", &result) == TRL_NO_MEMORY);
    m.how = MAKING_NOTHING;
    TRL_CHECK(test, trl_agent_query(agent, "CONNECT TO DEFAULT", &result) == TRL_NO_MEMORY);
    TRL_CHECK(test, trl_agent_count(agent) == 0U);
    trl_agent_free(agent);
    m.how = MAKING_SCRIPTED;
    trl_conn *first = NULL;
    (void)make(&m, NULL, NULL, &first);
    TRL_CHECK(test, trl_agent_connected(make, &m, first, &agent) == TRL_OK && trl_agent_count(agent) == 1U);
    TRL_CHECK(test, trl_agent_current(agent) == first && trl_agent_current_name(agent) == NULL);
    trl_agent_free(agent);
    (void)make(&m, NULL, NULL, &first);
    TRL_CHECK(test, trl_agent_connected(NULL, &m, first, &agent) == TRL_INVALID_ARGUMENT && agent == NULL);
    TRL_CHECK(test, trl_agent_connected(make, &m, NULL, &agent) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_agent_connected(make, &m, NULL, NULL) == TRL_INVALID_ARGUMENT);
    maker_free(&m);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(connection_statements_are_read),
        TRL_TEST_CASE(malformed_connection_statements_fail),
        TRL_TEST_CASE(an_agent_without_connections_refuses),
        TRL_TEST_CASE(connections_are_made_and_chosen),
        TRL_TEST_CASE(connections_are_ended),
        TRL_TEST_CASE(connector_failures_are_told),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
