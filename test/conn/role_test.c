/**
 * @file role_test.c
 * @brief A server's role read from INFORMATION_SCHEMA.REPLICAS, of a
 * server that does not tell it by SERVER_READ_ONLY, against a server of
 * the test's own: as turborel-client's tests of its role read it.
 */
#include "codec/buffer.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How the server answers the query of its role. */
typedef enum answer {
    ANSWER_NO_TABLE = 0,
    ANSWER_NO_TABLE_OLDER,
    ANSWER_PRIMARY,
    ANSWER_BACKUP,
    ANSWER_NO_ROW,
    ANSWER_LOST,
    ANSWER_TOLD,
} answer;

/* A row of one character string, `text`, then the Complete. */
static void one_row(script *s, const char *text) {
    script_description(s, 1U);
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 1U);
    trl_put_u32(&body, 1U);
    trl_put_u8(&body, TRL_TAG_CHARACTER);
    script_put_string(&body, text);
    script_reply(s, TRL_KIND_ROWS, &body);
    trl_buffer_free(&body);
    script_complete(s, 0, "00000", false, false);
}

static void respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    const answer *plan = s->data;
    if (kind == TRL_KIND_HELLO) {
        if (*plan == ANSWER_TOLD) {
            script_status(s, "SERVER_READ_ONLY", "YES");
        }
        script_ready(s, "ANN", NULL, 0U);
        return;
    }
    switch (*plan) {
    case ANSWER_NO_TABLE:
        script_error(s, -103, "42000", "INFORMATION_SCHEMA.REPLICAS is not defined", NULL);
        break;
    case ANSWER_NO_TABLE_OLDER:
        script_error(s, -103, "2A000", "INFORMATION_SCHEMA.REPLICAS is not defined", NULL);
        break;
    case ANSWER_PRIMARY:
        one_row(s, "PRIMARY ");
        break;
    case ANSWER_BACKUP:
        one_row(s, "BACKUP");
        break;
    case ANSWER_LOST:
        script_error(s, -919, "08006", "the server is stopping", NULL);
        break;
    case ANSWER_NO_ROW:
    case ANSWER_TOLD:
    default:
        script_description(s, 1U);
        script_complete(s, 100, "02000", false, false);
        break;
    }
}

/* The role a server answering as `plan` shows; whether it was read. */
static bool role_read(answer plan, trl_server_role *role) {
    script s;
    script_init(&s, respond, &plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(&s, trust, NULL, &conn);
    bool read = trl_conn_role(conn, role) == TRL_OK && !trl_conn_in_transaction(conn);
    bool queried = s.requests == 2U && s.kinds[1] == TRL_KIND_QUERY;
    trl_conn_free(conn);
    trl_mechanism_free(trust);
    script_free(&s);
    return read && (plan == ANSWER_TOLD ? s.requests == 1U : queried);
}

/**
 * Verifies: [CLIENT Several servers] a server alone, which has no
 * REPLICAS (class 42, or 2A of an older server), takes changes; of a replica, PRIMARY is read-write,
 * another role read-only; no row, as a session not the owner's sees, is
 * unknown; another failure is the query's; a server that tells
 * SERVER_READ_ONLY is not asked.
 */
static void a_servers_role_is_read_from_replicas(trl_test *test) {
    trl_server_role role = TRL_SERVER_UNKNOWN;
    TRL_CHECK(test, role_read(ANSWER_NO_TABLE, &role) && role == TRL_SERVER_READ_WRITE);
    TRL_CHECK(test, role_read(ANSWER_NO_TABLE_OLDER, &role) && role == TRL_SERVER_READ_WRITE);
    TRL_CHECK(test, role_read(ANSWER_PRIMARY, &role) && role == TRL_SERVER_READ_WRITE);
    TRL_CHECK(test, role_read(ANSWER_BACKUP, &role) && role == TRL_SERVER_READ_ONLY);
    TRL_CHECK(test, role_read(ANSWER_NO_ROW, &role) && role == TRL_SERVER_UNKNOWN);
    TRL_CHECK(test, role_read(ANSWER_TOLD, &role) && role == TRL_SERVER_READ_ONLY);
    TRL_CHECK(test, !role_read(ANSWER_LOST, &role));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_servers_role_is_read_from_replicas),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
