/**
 * @file bulk_serving.c
 * @brief A server of a test's own for copies, imports and exports.
 */
#include "bulk_serving.h"

#include "codec/buffer.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "format.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The length of each CHARACTER VARYING parameter, and of a large object's. */
#define BULK_VARYING 20U
#define BULK_LOB 1000000U
/* The locator of each large object made. */
#define BULK_LOCATOR 7U

/* Whether `body` holds `text`. */
static bool holds(trl_slice body, const char *text) {
    size_t length = strlen(text);
    for (size_t i = 0U; i + length <= body.length; ++i) {
        if (memcmp(body.bytes + i, text, length) == 0) {
            return true;
        }
    }
    return false;
}

void bulk_error(script *s, const char *sqlstate, bool active, const char *detail) {
    trl_buffer body;
    trl_buffer_init(&body);
    bool lost = strncmp(sqlstate, "08", 2U) == 0;
    trl_put_u32(&body, (uint32_t)(lost ? -919 : -803));
    script_put_string(&body, sqlstate);
    script_put_string(&body, lost ? "the server is stopping" : "a row is refused");
    trl_put_flag(&body, false);
    trl_put_flag(&body, false);
    trl_put_flag(&body, active);
    if (detail != NULL) {
        trl_put_u8(&body, TRL_TAIL_SEVERITY + 1U);
        script_put_string(&body, detail);
    }
    script_reply(s, TRL_KIND_ERROR, &body);
    trl_buffer_free(&body);
}

/* A Complete of `rows` rows, the transaction `active` after it. */
static void completed(script *s, bool active, uint64_t rows) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    script_put_string(&body, "00000");
    trl_put_flag(&body, active);
    trl_put_u8(&body, TRL_TAIL_ROWS);
    trl_put_u64(&body, rows);
    script_reply(s, TRL_KIND_COMPLETE, &body);
    trl_buffer_free(&body);
}

/* A Prepared of the INSERT: two parameters, CHARACTER VARYING(20) or
   character large objects, and no column. */
static void insert_prepared(script *s, bool lobs) {
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, 0U);
    trl_put_u32(&body, 2U);
    for (uint32_t i = 0U; i < 2U; ++i) {
        trl_put_flag(&body, false);
        if (lobs) {
            trl_put_u8(&body, TRL_TAG_CLOB);
            trl_put_u64(&body, BULK_LOB);
        } else {
            trl_put_u8(&body, TRL_TAG_VARYING);
            trl_put_u32(&body, BULK_VARYING);
        }
        trl_put_u8(&body, 0U);
    }
    trl_put_u32(&body, 0U);
    script_reply(s, TRL_KIND_PREPARED, &body);
    trl_buffer_free(&body);
}

/* Rows of values CSV must quote, the last's second value null. */
static void quoted_rows(script *s) {
    static const char *const values[] = {
        "a,b", "say \"hi\"", " x", "y\t", "l\nb", "c\rd", "\vz", "z\f", "\xC2\xA0n", "m\xE3\x80\x80", "",
    };
    size_t count = sizeof values / sizeof values[0];
    trl_buffer body;
    trl_buffer_init(&body);
    trl_put_u32(&body, (uint32_t)((count + 1U) / 2U));
    for (size_t i = 0U; i <= count; ++i) {
        if (i % 2U == 0U) {
            trl_put_u32(&body, 2U);
        }
        if (i == count) {
            trl_put_u8(&body, TRL_TAG_NULL);
        } else {
            trl_put_u8(&body, TRL_TAG_CHARACTER);
            script_put_string(&body, values[i]);
        }
    }
    script_reply(s, TRL_KIND_ROWS, &body);
    trl_buffer_free(&body);
}

/* Answers a query: COMMIT, ROLLBACK, or another as the plan says. */
static void query(script *s, bulk_plan *plan, trl_slice body) {
    if (holds(body, "COMMIT")) {
        ++plan->commits;
        if (plan->fail_commit) {
            bulk_error(s, "40002", false, NULL);
        } else {
            completed(s, false, 0U);
        }
        return;
    }
    if (holds(body, "ROLLBACK")) {
        ++plan->rollbacks;
        completed(s, false, 0U);
        return;
    }
    if (plan->query == EXPORT_ERROR) {
        bulk_error(s, "42000", false, NULL);
        return;
    }
    if (plan->query == EXPORT_OUT_OF_TURN) {
        script_bare(s, TRL_KIND_PONG);
        return;
    }
    if (plan->query == EXPORT_NO_QUERY) {
        completed(s, false, 1U);
        return;
    }
    if (plan->query == EXPORT_QUOTED) {
        script_description(s, 2U);
        quoted_rows(s);
        script_complete(s, 0, "00000", false, false);
        return;
    }
    script_description(s, 2U);
    script_rows(s, 2U, 2U);
    if (plan->query == EXPORT_ROWS_THEN_ERROR) {
        bulk_error(s, "22003", false, NULL);
    } else if (plan->query == EXPORT_ROWS_THEN_OUT_OF_TURN) {
        script_bare(s, TRL_KIND_PONG);
    } else {
        script_rows(s, 1U, 2U);
        script_complete(s, 0, "00000", false, false);
    }
}

/* Answers an Execute: the INSERT's row inserted, or refused if the plan
   says this one fails. */
static void execute(script *s, bulk_plan *plan) {
    ++plan->executes;
    if (plan->executes == plan->fail_execute) {
        bulk_error(s, plan->fail_lost ? "08006" : "23505", plan->row_alone, NULL);
    } else {
        completed(s, true, 1U);
    }
}

/* Answers a CopyData, counting its rows, or a CopyEnd, as the plan says. */
static void copy(script *s, bulk_plan *plan, uint8_t kind, trl_slice body) {
    if (kind == TRL_KIND_COPY_DATA) {
        uint32_t rows = 0U;
        if (body.length >= sizeof rows) {
            rows = (uint32_t)body.bytes[0] | ((uint32_t)body.bytes[1] << 8U) | ((uint32_t)body.bytes[2] << 16U) |
                   ((uint32_t)body.bytes[3] << 24U);
        }
        plan->copied += rows;
        return;
    }
    ++plan->copies;
    bool abandoned = body.length > 0U && body.bytes[0] == 1U;
    if (!abandoned && plan->copy_fail_row > 0U) {
        char detail[64];
        trl_format(detail, sizeof detail, "row %u of the copy", plan->copy_fail_row);
        bulk_error(s, "23505", false, detail);
    } else if (!abandoned && plan->copy_fail) {
        bulk_error(s, "42000", false, plan->copy_detail);
    } else {
        completed(s, false, abandoned ? 0U : plan->copied);
    }
    plan->copied = 0U;
}

/* Answers a Prepare: the table's description, or the INSERT. */
static void prepare(script *s, const bulk_plan *plan, trl_slice body) {
    bool describing = holds(body, "SELECT");
    if (describing ? plan->fail_describe : plan->fail_insert) {
        bulk_error(s, "42000", false, NULL);
    } else if (describing) {
        script_prepared(s, 0U, 0U, 2U);
    } else {
        insert_prepared(s, plan->lob_columns);
    }
}

void bulk_respond(script *s, uint8_t kind, trl_slice body) {
    bulk_plan *plan = s->data;
    static const char *const granted[] = {TRL_CAPABILITY_NOTICES, TRL_CAPABILITY_COPY};
    switch (kind) {
    case TRL_KIND_HELLO:
        script_ready(s, "ANN", granted, plan->copy ? 2U : 1U);
        break;
    case TRL_KIND_QUERY:
        query(s, plan, body);
        break;
    case TRL_KIND_PREPARE:
        prepare(s, plan, body);
        break;
    case TRL_KIND_EXECUTE:
        execute(s, plan);
        break;
    case TRL_KIND_DEALLOCATE:
        if (plan->fail_deallocate) {
            bulk_error(s, "26000", false, NULL);
        } else {
            script_complete(s, 0, "00000", false, false);
        }
        break;
    case TRL_KIND_COPY_IN:
        break;
    case TRL_KIND_COPY_DATA:
    case TRL_KIND_COPY_END:
        copy(s, plan, kind, body);
        break;
    case TRL_KIND_LOB_REQUEST:
        if (body.length > 0U && body.bytes[0] == TRL_LOB_CREATE) {
            script_lob_locator(s, BULK_LOCATOR);
        } else {
            script_complete(s, 0, "00000", true, false);
        }
        break;
    default:
        script_complete(s, 0, "00000", false, false);
        break;
    }
}

trl_conn *bulk_connect(script *s, bulk_plan *plan) {
    script_init(s, bulk_respond, plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    return conn;
}
