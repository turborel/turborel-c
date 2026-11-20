/**
 * @file told.c
 * @brief What the server tells a connection
 * [CLIENT What the server tells]: the last completion's rows, identity values, time and
 * conditions, the last commit's position, and notices, handed to a
 * handler or the last 64 kept.
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "codec/replies.h"
#include "codec/tail.h"
#include "conn/conn.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The SQLSTATE class of a warning. */
#define TRL_WARNING_CLASS "01"
#define TRL_SQLSTATE_CLASS_BYTES 2U

/* Frees what the last completion told. */
static void forget_told(trl_told *told) {
    for (size_t i = 0U; i < told->generated_count; ++i) {
        free(told->generated[i]);
    }
    free((void *)told->generated);
    for (size_t i = 0U; i < told->condition_count; ++i) {
        trl_failure_clear(&told->conditions[i]);
    }
    free(told->conditions);
    memset(told, 0, sizeof *told);
}

/* A copy of `text`, null-ended, or null if memory cannot be had. */
static char *copy_of(trl_slice text) {
    char *copy = trl_malloc(text.length + 1U);
    if (copy != NULL) {
        if (text.length > 0U) {
            memcpy(copy, text.bytes, text.length);
        }
        copy[text.length] = '\0';
    }
    return copy;
}

/* Keeps the identity values a completion told; as many as memory allows. */
static void keep_generated(trl_told *told, const trl_strings *generated) {
    if (generated->count == 0U) {
        return;
    }
    told->generated = (char **)trl_calloc(generated->count, sizeof(char *));
    for (uint32_t i = 0U; told->generated != NULL && i < generated->count; ++i) {
        told->generated[i] = copy_of(generated->items[i]);
        if (told->generated[i] == NULL) {
            break;
        }
        told->generated_count = (size_t)i + 1U;
    }
}

/* Keeps the conditions a completion told. */
static void keep_conditions(trl_told *told, const trl_completed *completed) {
    if (completed->condition_count == 0U) {
        return;
    }
    told->conditions = trl_calloc(completed->condition_count, sizeof *told->conditions);
    for (uint32_t i = 0U; told->conditions != NULL && i < completed->condition_count; ++i) {
        trl_failure_init(&told->conditions[i]);
        trl_failure_from_wire(&told->conditions[i], &completed->conditions[i]);
        told->condition_count = (size_t)i + 1U;
    }
}

void trl_conn_completed(trl_conn *conn, const trl_completed *completed) {
    bool warning = completed->sqlstate.length >= TRL_SQLSTATE_CLASS_BYTES &&
                   memcmp(completed->sqlstate.bytes, TRL_WARNING_CLASS, TRL_SQLSTATE_CLASS_BYTES) == 0;
    conn->warning[0] = '\0';
    if (warning) {
        trl_format(conn->warning, sizeof conn->warning, "%.*s", (int)completed->sqlstate.length,
                   (const char *)completed->sqlstate.bytes);
    }
    if (completed->has_position) {
        conn->has_position = true;
        conn->position = completed->position;
    }
    forget_told(&conn->last);
    trl_told *told = &conn->last;
    told->told = true;
    told->has_rows = completed->has_rows;
    told->rows = completed->rows;
    told->has_time = completed->has_time;
    told->time.total = completed->time[TRL_TIME_TOTAL];
    told->time.queued = completed->time[TRL_TIME_QUEUED];
    told->time.locks = completed->time[TRL_TIME_LOCKS];
    keep_generated(told, &completed->generated);
    keep_conditions(told, completed);
}

void trl_conn_notice(trl_conn *conn, const trl_reply *notice) {
    trl_wire_failure wire;
    memset(&wire, 0, sizeof wire);
    wire.sqlstate = notice->as.notice.sqlstate;
    wire.message = notice->as.notice.message;
    wire.diagnostics = notice->as.notice.diagnostics;
    if (conn->handler != NULL) {
        trl_failure told;
        trl_failure_init(&told);
        trl_failure_from_wire(&told, &wire);
        conn->handler(conn->handler_context, &told);
        trl_failure_clear(&told);
        return;
    }
    if (conn->notice_count == TRL_KEPT_NOTICES) {
        trl_failure_clear(&conn->notices[0]);
        memmove(&conn->notices[0], &conn->notices[1], (TRL_KEPT_NOTICES - 1U) * sizeof conn->notices[0]);
        --conn->notice_count;
    }
    trl_failure *kept = &conn->notices[conn->notice_count++];
    trl_failure_init(kept);
    trl_failure_from_wire(kept, &wire);
}

void trl_conn_free_told(trl_conn *conn) {
    forget_told(&conn->last);
    trl_conn_clear_notices(conn);
    for (size_t i = 0U; i < conn->attribute_count; ++i) {
        trl_free_const(conn->attributes[i].name.bytes);
        trl_free_const(conn->attributes[i].value.bytes);
    }
    conn->attribute_count = 0U;
}

bool trl_conn_needs(trl_conn *conn, bool ok, const char *capability) {
    if (ok || trl_conn_granted(conn, capability)) {
        return true;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "the server did not grant %s", capability);
    trl_failure_broken(&conn->failure, message);
    return false;
}

void trl_conn_failed_by(trl_conn *conn, const trl_wire_failure *wire) {
    trl_failure failure;
    trl_failure_init(&failure);
    trl_failure_from_wire(&failure, wire);
    trl_conn_failed(conn, &failure);
    trl_failure_clear(&failure);
}

bool trl_conn_codec_failed(trl_conn *conn, const trl_codec_error *error) {
    trl_failure_from_codec(&conn->failure, error);
    return false;
}

size_t trl_conn_query_attributes(const trl_conn *conn, const trl_attribute **out) {
    *out = conn->attributes;
    return trl_conn_granted(conn, TRL_CAPABILITY_QUERY_ATTRIBUTES) ? conn->attribute_count : 0U;
}

trl_status trl_conn_set_autocommit(trl_conn *conn, bool on) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    conn->autocommit = on;
    return TRL_OK;
}

bool trl_conn_autocommit(const trl_conn *conn) {
    return conn != NULL && conn->autocommit;
}

bool trl_conn_in_transaction(const trl_conn *conn) {
    return conn != NULL && conn->active;
}

const char *trl_conn_warning(const trl_conn *conn) {
    return conn == NULL || conn->warning[0] == '\0' ? NULL : conn->warning;
}

bool trl_conn_rows_affected(const trl_conn *conn, uint64_t *rows) {
    if (conn == NULL || !conn->last.has_rows) {
        return false;
    }
    if (rows != NULL) {
        *rows = conn->last.rows;
    }
    return true;
}

size_t trl_conn_generated_count(const trl_conn *conn) {
    return conn == NULL ? 0U : conn->last.generated_count;
}

const char *trl_conn_generated_at(const trl_conn *conn, size_t index) {
    return conn == NULL || index >= conn->last.generated_count ? NULL : conn->last.generated[index];
}

bool trl_conn_server_time(const trl_conn *conn, trl_timing *timing) {
    if (conn == NULL || !conn->last.has_time) {
        return false;
    }
    if (timing != NULL) {
        *timing = conn->last.time;
    }
    return true;
}

bool trl_conn_commit_position(const trl_conn *conn, uint64_t *position) {
    if (conn == NULL || !conn->has_position) {
        return false;
    }
    if (position != NULL) {
        *position = conn->position;
    }
    return true;
}

size_t trl_conn_condition_count(const trl_conn *conn) {
    return conn == NULL ? 0U : conn->last.condition_count;
}

const trl_failure *trl_conn_condition_at(const trl_conn *conn, size_t index) {
    return conn == NULL || index >= conn->last.condition_count ? NULL : &conn->last.conditions[index];
}

trl_status trl_conn_set_notice_handler(trl_conn *conn, trl_notice_handler handler, void *context) {
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    conn->handler = handler;
    conn->handler_context = context;
    return TRL_OK;
}

size_t trl_conn_notice_count(const trl_conn *conn) {
    return conn == NULL ? 0U : conn->notice_count;
}

const trl_failure *trl_conn_notice_at(const trl_conn *conn, size_t index) {
    return conn == NULL || index >= conn->notice_count ? NULL : &conn->notices[index];
}

void trl_conn_clear_notices(trl_conn *conn) {
    if (conn == NULL) {
        return;
    }
    for (size_t i = 0U; i < conn->notice_count; ++i) {
        trl_failure_clear(&conn->notices[i]);
    }
    conn->notice_count = 0U;
}
