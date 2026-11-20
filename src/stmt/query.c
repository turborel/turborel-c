/**
 * @file query.c
 * @brief Statements run by Query [PROTOCOL Query 'Q']
 * [CLIENT Running statements]: one, its rows all at once or a batch at a time, or several
 * as one request [CLIENT Controlling the session].
 */
#include "codec/arena.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "conn/conn.h"
#include "failure.h"
#include "memory.h"
#include "stmt/result.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A batch of rows given to a handler as they come. */
typedef struct batches {
    trl_batch_handler handler;
    void *context;
} batches;

/* Sends a Query of `sql`, several statements if `multi`. */
static bool send_query(trl_conn *conn, const char *sql, bool multi) {
    trl_query_request query;
    trl_query_of(conn, sql, multi, &query);
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_query(&conn->out, &query, &error)) {
        return trl_conn_codec_failed(conn, &error);
    }
    return trl_conn_send(conn);
}

/* Gives a batch of rows to the handler, the query's columns its own. */
static bool give_batch(trl_conn *conn, const trl_result *result, trl_result *batch, const trl_reply *reply,
                       const batches *to) {
    batch->has_rows = true;
    batch->columns = result->columns;
    batch->column_count = result->column_count;
    batch->names = result->names;
    if (!trl_result_add_rows(batch, reply->as.rows.rows, reply->as.rows.count)) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    to->handler(to->context, batch);
    return true;
}

/* Reads the reply after a batch: true to read on, false to stop, `done`
   whether the rows ended well. */
static bool take_reply(trl_conn *conn, trl_result *result, const batches *to, bool *done) {
    trl_result *batch = trl_result_make();
    if (batch == NULL) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    conn->keep = &batch->arena;
    trl_reply reply;
    bool received = trl_conn_receive(conn, &reply);
    conn->keep = &result->arena;
    bool more = false;
    if (!received) {
        more = false;
    } else if (reply.kind == TRL_KIND_ROWS) {
        more = give_batch(conn, result, batch, &reply, to);
    } else if (reply.kind == TRL_KIND_COMPLETE) {
        trl_result_complete(result, &reply.as.complete);
        *done = true;
    } else if (reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
    } else {
        trl_conn_lose_by(conn, "the server broke off its rows");
    }
    trl_result_free(batch);
    return more;
}

/* Reads a query's rows a batch at a time, each given to the handler in a
   batch of its own. */
static bool read_batches(trl_conn *conn, trl_result *result, const batches *to) {
    bool done = false;
    while (take_reply(conn, result, to, &done)) {
    }
    return done;
}

/* Reads a statement's outcome into `result`: a Complete, or a Description
   and its rows; `more` gets whether another statement's replies follow. */
static bool read_outcome(trl_conn *conn, trl_result *result, const batches *to, bool *more) {
    trl_reply reply;
    if (!trl_conn_receive(conn, &reply)) {
        return false;
    }
    if (reply.kind == TRL_KIND_COMPLETE) {
        trl_result_complete(result, &reply.as.complete);
        *more = result->followed;
        return true;
    }
    if (reply.kind == TRL_KIND_ERROR) {
        trl_conn_failed_by(conn, &reply.as.error);
        return false;
    }
    if (reply.kind != TRL_KIND_DESCRIPTION) {
        trl_conn_lose_by(conn, "the server sent no result");
        return false;
    }
    if (!trl_result_take_columns(result, reply.as.description.columns, reply.as.description.count)) {
        trl_failure_no_memory(&conn->failure);
        return false;
    }
    bool read = to != NULL ? read_batches(conn, result, to) : trl_read_rows(conn, result);
    *more = read && result->followed;
    return read;
}

/* Runs one statement by Query into a result of its own. */
static trl_status run(trl_conn *conn, const char *sql, const batches *to, trl_result **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (conn == NULL || sql == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_result *result = trl_result_make();
    if (result == NULL) {
        return TRL_NO_MEMORY;
    }
    bool more = false;
    bool ran = send_query(conn, sql, false);
    if (ran) {
        conn->keep = &result->arena;
        ran = read_outcome(conn, result, to, &more);
        conn->keep = NULL;
    }
    if (!ran) {
        trl_result_free(result);
        return TRL_FAILED;
    }
    *out = result;
    return TRL_OK;
}

trl_status trl_query(trl_conn *conn, const char *sql, trl_result **out) {
    return run(conn, sql, NULL, out);
}

trl_status trl_query_batches(trl_conn *conn, const char *sql, trl_batch_handler handler, void *context,
                             trl_result **out) {
    if (handler == NULL) {
        if (out != NULL) {
            *out = NULL;
        }
        return TRL_INVALID_ARGUMENT;
    }
    batches to = {handler, context};
    return run(conn, sql, &to, out);
}

struct trl_ran {
    trl_result **results;
    size_t count;
    bool failed;
    trl_failure failure;
};

/* Adds a result to what several statements came to. */
static bool add_result(trl_ran *ran, trl_result *result) {
    trl_result **grown = (trl_result **)trl_realloc((void *)ran->results, (ran->count + 1U) * sizeof(trl_result *));
    if (grown == NULL) {
        return false;
    }
    ran->results = grown;
    ran->results[ran->count++] = result;
    return true;
}

/* Reads each statement's outcome until the last, or the first failure. */
static bool read_many(trl_conn *conn, trl_ran *ran) {
    for (bool more = true; more;) {
        trl_result *result = trl_result_make();
        if (result == NULL) {
            trl_failure_no_memory(&conn->failure);
            return false;
        }
        conn->keep = &result->arena;
        bool read = read_outcome(conn, result, NULL, &more);
        conn->keep = NULL;
        if (!read) {
            trl_result_free(result);
            if (conn->lost) {
                return false;
            }
            ran->failed = true;
            trl_failure_copy(&ran->failure, &conn->failure);
            return true;
        }
        if (!add_result(ran, result)) {
            trl_result_free(result);
            trl_failure_no_memory(&conn->failure);
            return false;
        }
    }
    return true;
}

trl_status trl_query_many(trl_conn *conn, const char *sql, trl_ran **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (conn == NULL || sql == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (!trl_conn_needs(conn, false, TRL_CAPABILITY_MULTI_STATEMENT)) {
        return TRL_FAILED;
    }
    trl_ran *ran = trl_calloc(1U, sizeof *ran);
    if (ran == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&ran->failure);
    if (!send_query(conn, sql, true) || !read_many(conn, ran)) {
        trl_ran_free(ran);
        return TRL_FAILED;
    }
    *out = ran;
    return TRL_OK;
}

size_t trl_ran_count(const trl_ran *ran) {
    return ran == NULL ? 0U : ran->count;
}

const trl_result *trl_ran_result(const trl_ran *ran, size_t index) {
    return ran == NULL || index >= ran->count ? NULL : ran->results[index];
}

const trl_failure *trl_ran_failure(const trl_ran *ran) {
    return ran == NULL || !ran->failed ? NULL : &ran->failure;
}

void trl_ran_free(trl_ran *ran) {
    if (ran == NULL) {
        return;
    }
    for (size_t i = 0U; i < ran->count; ++i) {
        trl_result_free(ran->results[i]);
    }
    free((void *)ran->results);
    trl_failure_clear(&ran->failure);
    free(ran);
}
