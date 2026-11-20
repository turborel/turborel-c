/**
 * @file serving.h
 * @brief A scripted server of statements: Ready granting every capability
 * a statement method needs, or none, then each request answered as a plan
 * says, for the tests of statements and of the session's control.
 */
#ifndef TRL_TEST_CONN_SERVING_H
#define TRL_TEST_CONN_SERVING_H

#include "codec/buffer.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief What the server does with each request after Ready. */
typedef enum reply_plan {
    PLAN_ROWS = 0,
    PLAN_DONE,
    PLAN_PONG,
    PLAN_BROKEN_ROWS,
    PLAN_ERROR,
    PLAN_ERROR_08,
    PLAN_NOTICES,
    PLAN_TAILS,
    PLAN_MANY,
    PLAN_PREPARED,
    PLAN_SUSPEND,
    PLAN_DESCRIBED,
    PLAN_CHALLENGE,
    PLAN_ONLY_CHALLENGE,
    PLAN_PREPARED_BARE,
    PLAN_ROWS_THEN_ERROR,
    PLAN_BEHIND,
} reply_plan;

/** @brief A server's plan, and whether it grants everything. */
typedef struct serving {
    reply_plan plan;
    bool everything;
} serving;

/** @brief Answers Hello with Ready, granting everything if the plan says, then each request by the plan. */
void serving_respond(script *s, uint8_t kind, trl_slice body);

/** @brief A connection over `s`, logged in by trust, served as `plan` says. */
trl_conn *serving_connect(script *s, serving *plan);

/** @brief Whether the connection's failure says `message`. */
bool serving_failed_with(const trl_conn *conn, const char *message);

#endif
