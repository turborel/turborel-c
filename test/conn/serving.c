/**
 * @file serving.c
 * @brief A scripted server of statements, answering as a plan says.
 */
#include "serving.h"

#include "codec/buffer.h"
#include "codec/kinds.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A query's rows: a Description, two batches, and its Complete. */
static void rows_reply(script *s) {
    script_description(s, 2U);
    script_rows(s, 2U, 2U);
    script_rows(s, 1U, 2U);
    script_complete(s, 0, "00000", true, false);
}

/* Rows broken off by a reply out of turn. */
static void broken_rows(script *s) {
    script_description(s, 1U);
    script_rows(s, 1U, 1U);
    script_bare(s, TRL_KIND_PONG);
}

/* 70 notices, the last 01999, then a Complete. */
static void notices_reply(script *s) {
    for (int i = 0; i < 70; ++i) {
        script_notice(s, i == 69 ? "01999" : "01000", "a warning");
    }
    script_complete(s, 0, "00000", false, false);
}

/* Three statements' outcomes, the third an Error. */
static void many_reply(script *s) {
    script_complete(s, 0, "00000", false, true);
    script_description(s, 1U);
    script_rows(s, 1U, 1U);
    script_complete(s, 0, "00000", false, true);
    script_error(s, -103, "42000", "the third refused", NULL);
}

/* Answers with a failure, if the plan is one; whether it was. */
static bool answer_failure(script *s, plan plan) {
    if (plan == PLAN_ERROR) {
        script_error(s, -103, "42000", "a statement refused", NULL);
    } else if (plan == PLAN_ERROR_08) {
        script_error(s, -919, "08006", "the server is stopping", NULL);
    } else if (plan == PLAN_BEHIND) {
        script_error(s, -920, "TBR01", "the replica has not applied the position asked for", NULL);
    } else if (plan == PLAN_ROWS_THEN_ERROR) {
        script_description(s, 1U);
        script_rows(s, 1U, 1U);
        script_error(s, -802, "22003", "out of range", NULL);
    } else if (plan == PLAN_BROKEN_ROWS) {
        broken_rows(s);
    } else if (plan == PLAN_PONG) {
        script_bare(s, TRL_KIND_PONG);
    } else {
        return false;
    }
    return true;
}

/* Answers a query or a prepared statement, if the plan is one of theirs;
   whether it was. */
static bool answer_statement(script *s, plan plan) {
    if (plan == PLAN_ROWS) {
        rows_reply(s);
    } else if (plan == PLAN_DONE) {
        script_complete(s, 0, "00000", false, false);
    } else if (plan == PLAN_NOTICES) {
        notices_reply(s);
    } else if (plan == PLAN_TAILS) {
        script_complete_tails(s);
    } else if (plan == PLAN_MANY) {
        many_reply(s);
    } else if (plan == PLAN_PREPARED) {
        script_prepared(s, 0U, 1U, 1U);
    } else if (plan == PLAN_PREPARED_BARE) {
        script_prepared(s, 0U, 0U, 0U);
    } else if (plan == PLAN_SUSPEND) {
        script_rows(s, 1U, 1U);
        script_bare(s, TRL_KIND_SUSPENDED);
    } else if (plan == PLAN_DESCRIBED) {
        script_description(s, 1U);
    } else {
        return false;
    }
    return true;
}

/* Answers a statement as the plan says. */
static void answer(script *s, plan plan) {
    if (answer_failure(s, plan) || answer_statement(s, plan)) {
        return;
    }
    if (plan == PLAN_CHALLENGE) {
        script_ready(s, "BOB", NULL, 0U);
    } else if (plan == PLAN_ONLY_CHALLENGE) {
        script_challenge(s, (const uint8_t *)"x", 1U);
    }
}

/* A server of Ready, granting everything if asked, then answering by plan. */
void serving_respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    const serving *serving_plan = s->data;
    static const char *const everything[] = {
        TRL_CAPABILITY_NOTICES,         TRL_CAPABILITY_DESCRIBE,    TRL_CAPABILITY_CURSORS,
        TRL_CAPABILITY_RESET,           TRL_CAPABILITY_CHANGE_USER, TRL_CAPABILITY_QUERY_ATTRIBUTES,
        TRL_CAPABILITY_MULTI_STATEMENT, TRL_CAPABILITY_FETCH_ENDS,  TRL_CAPABILITY_CONDITIONS,
    };
    if (kind == TRL_KIND_HELLO) {
        script_ready(s, "ANN", everything, serving_plan->everything ? 9U : 0U);
        return;
    }
    answer(s, serving_plan->plan);
}

/* A connection over a script serving as `plan` says. */
trl_conn *serving_connect(script *s, serving *plan) {
    script_init(s, serving_respond, plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    return conn;
}

/* Whether the connection failed with `message`. */
bool serving_failed_with(const trl_conn *conn, const char *message) {
    return strcmp(trl_failure_message(trl_conn_failure(conn)), message) == 0;
}
