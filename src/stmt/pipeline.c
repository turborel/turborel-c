/**
 * @file pipeline.c
 * @brief Pipelines [CLIENT Pipelines]: requests queued, written a window
 * at a time, each window at most TRL_PIPELINE_WINDOW bytes, its replies
 * read before the next is written [PROTOCOL Requests]. A window fits the
 * buffers of the two sockets between client and server, so it is written
 * whole even while the server writes the replies of its first requests
 * and reads no more.
 *
 * Each request is independent, so by default a failure is its request's
 * outcome alone and the rest run. TRL_ON_ERROR_SKIP stops the batch at
 * its first failure: a savepoint is set before each request but the
 * first, and the requests after a failed one are undone to the savepoint
 * set after it, or never sent if their window was not yet written. The
 * protocol has no request that skips those after a failure, so the
 * savepoints stay, as turborel-client's do.
 */
#include "stmt/pipeline.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/requests.h"
#include "conn/conn.h"
#include "data/params.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "stmt/stmt.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* What the savepoints of TRL_ON_ERROR_SKIP are named by, then each its
   place; and room for a statement of one. */
#define TRL_SAVEPOINT "TURBOREL_PIPELINE"
#define TRL_SAVEPOINT_BYTES 96U
/* The requests a pipeline first holds room for, and how its room grows. */
#define TRL_PIPELINE_FIRST 8U
#define TRL_PIPELINE_GROWTH 2U
/* What a skipping batch sends of each request but the first: a
   savepoint, and the request. */
#define TRL_SKIP_REQUESTS 2U

/* What a request queued is. */
typedef enum kind {
    KIND_QUERY = 0,
    KIND_EXECUTE,
    KIND_CLOSE,
    KIND_DEALLOCATE,
    KIND_SAVEPOINT,
} kind;

/* A request queued: its kind, its statement's text, or the statement and
   its parameters, or the number of the statement it closes; of a
   savepoint, its place. */
typedef struct queued {
    kind kind;
    char *sql;
    const trl_statement *statement;
    trl_params *params;
    uint32_t number;
    size_t place;
} queued;

/* What a request came to. */
typedef struct outcome {
    trl_piped piped;
    trl_result *result;
    trl_failure failure;
} outcome;

struct trl_pipeline {
    trl_conn *conn;
    queued *queued;
    size_t count;
    size_t capacity;
    trl_on_error on_error;
    bool ran;
    outcome *outcomes;
    size_t outcome_count;
};

trl_status trl_pipeline_new(trl_conn *conn, trl_pipeline **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = trl_calloc(1U, sizeof **out);
    if (*out == NULL) {
        return TRL_NO_MEMORY;
    }
    (*out)->conn = conn;
    return TRL_OK;
}

/* Room for one more request; null if memory cannot be had. */
static queued *more(trl_pipeline *pipeline) {
    if (pipeline->count == pipeline->capacity) {
        size_t capacity = pipeline->capacity == 0U ? TRL_PIPELINE_FIRST : pipeline->capacity * TRL_PIPELINE_GROWTH;
        queued *grown = trl_realloc(pipeline->queued, capacity * sizeof *grown);
        if (grown == NULL) {
            return NULL;
        }
        pipeline->queued = grown;
        pipeline->capacity = capacity;
    }
    queued *q = &pipeline->queued[pipeline->count];
    memset(q, 0, sizeof *q);
    return q;
}

/* Whether a request may be queued: a pipeline not run. */
static bool queueable(const trl_pipeline *pipeline) {
    return pipeline != NULL && !pipeline->ran;
}

trl_status trl_pipeline_query(trl_pipeline *pipeline, const char *sql) {
    if (!queueable(pipeline) || sql == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    queued *q = more(pipeline);
    size_t length = strlen(sql) + 1U;
    char *copy = q == NULL ? NULL : trl_malloc(length);
    if (copy == NULL) {
        return TRL_NO_MEMORY;
    }
    memcpy(copy, sql, length);
    q->kind = KIND_QUERY;
    q->sql = copy;
    ++pipeline->count;
    return TRL_OK;
}

/* A copy of `params`; null if memory cannot be had. */
static trl_params *params_copy(const trl_params *params) {
    trl_params *copy = NULL;
    if (trl_params_new(&copy) != TRL_OK) {
        return NULL;
    }
    const trl_value *values = trl_params_values(params);
    for (size_t i = 0U; params != NULL && i < trl_params_count(params); ++i) {
        if (trl_params_add_value(copy, &values[i]) != TRL_OK) {
            trl_params_free(copy);
            return NULL;
        }
    }
    return copy;
}

trl_status trl_pipeline_execute(trl_pipeline *pipeline, const trl_statement *statement, const trl_params *params) {
    if (!queueable(pipeline) || statement == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    queued *q = more(pipeline);
    trl_params *copy = q == NULL ? NULL : params_copy(params);
    if (copy == NULL) {
        return TRL_NO_MEMORY;
    }
    q->kind = KIND_EXECUTE;
    q->statement = statement;
    q->params = copy;
    ++pipeline->count;
    return TRL_OK;
}

/* Queues a request of `what` of the statement `number`. */
static trl_status numbered(trl_pipeline *pipeline, kind what, uint32_t number) {
    queued *q = more(pipeline);
    if (q == NULL) {
        return TRL_NO_MEMORY;
    }
    q->kind = what;
    q->number = number;
    ++pipeline->count;
    return TRL_OK;
}

trl_status trl_pipeline_release(trl_pipeline *pipeline, uint32_t number) {
    if (!queueable(pipeline)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status queued_one = numbered(pipeline, KIND_CLOSE, number);
    return queued_one == TRL_OK ? numbered(pipeline, KIND_DEALLOCATE, number) : queued_one;
}

trl_status trl_pipeline_on_error(trl_pipeline *pipeline, trl_on_error on_error) {
    if (pipeline == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    pipeline->on_error = on_error;
    return TRL_OK;
}

size_t trl_pipeline_count(const trl_pipeline *pipeline) {
    if (pipeline == NULL) {
        return 0U;
    }
    return pipeline->ran ? pipeline->outcome_count : pipeline->count;
}

/* The statement of the savepoint before the request at `place`, or one of
   its ends: `verb` then its name. */
static void savepoint_sql(char *out, const char *verb, size_t place) {
    trl_format(out, TRL_SAVEPOINT_BYTES, "%s %s_%lu", verb, TRL_SAVEPOINT, (unsigned long)place);
}

/* The requests sent: those queued, and under skip a savepoint before each
   but the first; how many in `count`. */
static queued *sent_of(const trl_pipeline *pipeline, bool skip, size_t *count) {
    *count = skip && pipeline->count > 0U ? (pipeline->count * TRL_SKIP_REQUESTS) - 1U : pipeline->count;
    queued *sent = trl_calloc(*count == 0U ? 1U : *count, sizeof *sent);
    if (sent == NULL) {
        return NULL;
    }
    size_t at = 0U;
    for (size_t n = 0U; n < pipeline->count; ++n) {
        if (skip && n > 0U) {
            sent[at].kind = KIND_SAVEPOINT;
            sent[at].place = n;
            ++at;
        }
        sent[at++] = pipeline->queued[n];
    }
    return sent;
}

/* Appends the request `q` to `to`, its transaction ended with it if
   `ends`. */
static bool encoded(const trl_conn *conn, const queued *q, bool ends, trl_encoding to) {
    char sql[TRL_SAVEPOINT_BYTES];
    switch (q->kind) {
    case KIND_QUERY:
        return trl_query_encode(conn, q->sql, ends, true, to);
    case KIND_EXECUTE:
        return trl_execute_encode(conn, q->statement, q->params, ends, to);
    case KIND_CLOSE:
        return trl_encode_close(to.out, q->number, 0U, to.error);
    case KIND_DEALLOCATE:
        return trl_encode_deallocate(to.out, q->number, to.error);
    case KIND_SAVEPOINT:
    default:
        savepoint_sql(sql, "SAVEPOINT", q->place);
        return trl_query_encode(conn, sql, false, false, to);
    }
}

/* Reads the reply of `q` into `o`; false if the connection is lost. */
static bool read_reply(trl_conn *conn, const queued *q, outcome *o) {
    trl_status read = TRL_OK;
    if (q->kind == KIND_EXECUTE) {
        read = trl_execute_read(conn, q->statement, &o->result);
    } else if (q->kind == KIND_CLOSE || q->kind == KIND_DEALLOCATE) {
        read = trl_conn_completion(conn) ? TRL_OK : TRL_FAILED;
    } else {
        read = trl_query_read(conn, &o->result);
    }
    if (read == TRL_OK) {
        o->piped = TRL_PIPED_DONE;
        return true;
    }
    o->piped = TRL_PIPED_FAILED;
    trl_failure_copy(&o->failure, &conn->failure);
    return !conn->lost;
}

/* A run under way: the pipeline, the requests sent, their outcomes, and
   whether each request's transaction ends with it. */
typedef struct running {
    trl_pipeline *pipeline;
    queued *sent;
    outcome *outcomes;
    size_t count;
    bool ends;
} running;

/* Writes the window of requests `from` to `to`, and reads their replies;
   false if the connection is lost, or a request cannot be written. */
static bool window_run(running *r, size_t from, size_t to) {
    trl_conn *conn = r->pipeline->conn;
    trl_codec_error error = {0, "", ""};
    trl_encoding into = {&conn->out, &error};
    for (size_t i = from; i < to; ++i) {
        if (!encoded(conn, &r->sent[i], r->ends, into)) {
            trl_buffer_free(&conn->out);
            return trl_conn_codec_failed(conn, &error);
        }
    }
    if (!trl_conn_send(conn)) {
        return false;
    }
    for (size_t i = from; i < to; ++i) {
        if (!read_reply(conn, &r->sent[i], &r->outcomes[i])) {
            return false;
        }
    }
    return true;
}

/* Whether a request failed among the first `count`. */
static bool any_failed(const outcome *outcomes, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        if (outcomes[i].piped == TRL_PIPED_FAILED) {
            return true;
        }
    }
    return false;
}

/* The bytes request `q` takes written, into `bytes`; false, the
   connection's failure set, if it cannot be written. */
static bool length_of(trl_conn *conn, const queued *q, bool ends, size_t *bytes) {
    trl_buffer one;
    trl_buffer_init(&one);
    trl_codec_error error = {0, "", ""};
    trl_encoding to = {&one, &error};
    bool written = encoded(conn, q, ends, to);
    bool held = one.state == TRL_BUFFER_OK;
    *bytes = one.length;
    trl_buffer_free(&one);
    if (!written) {
        return trl_conn_codec_failed(conn, &error);
    }
    if (!held) {
        trl_failure_no_memory(&conn->failure);
    }
    return held;
}

/* Runs the window of requests `from` to `to`; under skip, after a
   failure, marks them skipped, never sent. */
static bool window_done(running *r, size_t from, size_t to, bool skip) {
    if (skip && any_failed(r->outcomes, from)) {
        for (size_t k = from; k < to; ++k) {
            r->outcomes[k].piped = TRL_PIPED_SKIPPED;
        }
        return true;
    }
    return window_run(r, from, to);
}

/* Writes the requests a window at a time, of at most TRL_PIPELINE_WINDOW
   bytes, a request longer than that alone in its own, and reads their
   outcomes; under skip, a window after a failure is never sent. */
static bool windows_run(running *r, bool skip) {
    size_t from = 0U;
    size_t filled = 0U;
    for (size_t i = 0U; i <= r->count; ++i) {
        size_t bytes = 0U;
        if (i < r->count && !length_of(r->pipeline->conn, &r->sent[i], r->ends, &bytes)) {
            return false;
        }
        bool full = i == r->count || filled + bytes > TRL_PIPELINE_WINDOW;
        if (full && i > from) {
            if (!window_done(r, from, i, skip)) {
                return false;
            }
            from = i;
            filled = 0U;
        }
        filled += bytes;
    }
    return true;
}

/* Runs `sql` within the transaction, whatever autocommit says: a statement
   of the batch's own, which must not end it. */
static bool within(trl_conn *conn, const char *sql) {
    trl_codec_error error = {0, "", ""};
    trl_encoding to = {&conn->out, &error};
    if (!trl_query_encode(conn, sql, false, false, to)) {
        trl_buffer_free(&conn->out);
        return trl_conn_codec_failed(conn, &error);
    }
    trl_result *result = NULL;
    bool ran = trl_conn_send(conn) && trl_query_read(conn, &result) == TRL_OK;
    trl_result_free(result);
    return ran;
}

/* Ends a skipping batch: the requests after the first that failed undone,
   to the savepoint set after it, each marked skipped; the savepoints
   released; and, under autocommit of a batch that began no transaction,
   its work committed. */
static bool settle(running *r, bool began) {
    trl_conn *conn = r->pipeline->conn;
    char sql[TRL_SAVEPOINT_BYTES];
    size_t failed = r->count;
    for (size_t i = 0U; i < r->count && failed == r->count; ++i) {
        failed = r->outcomes[i].piped == TRL_PIPED_FAILED ? i : failed;
    }
    if (failed + 1U < r->count && r->outcomes[failed + 1U].piped == TRL_PIPED_DONE) {
        savepoint_sql(sql, "ROLLBACK TO SAVEPOINT", r->sent[failed + 1U].place);
        if (!within(conn, sql)) {
            return false;
        }
    }
    for (size_t i = failed + 1U; i < r->count; ++i) {
        trl_result_free(r->outcomes[i].result);
        r->outcomes[i].result = NULL;
        r->outcomes[i].piped = TRL_PIPED_SKIPPED;
    }
    if (r->count > 1U && r->outcomes[1].piped == TRL_PIPED_DONE) {
        savepoint_sql(sql, "RELEASE SAVEPOINT", r->sent[1].place);
        if (!within(conn, sql)) {
            return false;
        }
    }
    return began || !conn->autocommit || within(conn, "COMMIT");
}

/* Frees what each outcome holds, and the list. */
static void outcomes_free(outcome *outcomes, size_t count) {
    for (size_t i = 0U; outcomes != NULL && i < count; ++i) {
        trl_result_free(outcomes[i].result);
        trl_failure_clear(&outcomes[i].failure);
    }
    free(outcomes);
}

/* Keeps the outcomes of the requests queued, the savepoints' dropped. */
static void shown(trl_pipeline *pipeline, running *r) {
    size_t at = 0U;
    for (size_t i = 0U; i < r->count; ++i) {
        if (r->sent[i].kind == KIND_SAVEPOINT) {
            trl_result_free(r->outcomes[i].result);
            trl_failure_clear(&r->outcomes[i].failure);
            continue;
        }
        r->outcomes[at++] = r->outcomes[i];
    }
    pipeline->outcomes = r->outcomes;
    pipeline->outcome_count = at;
}

trl_status trl_pipeline_run(trl_pipeline *pipeline) {
    if (pipeline == NULL || pipeline->ran) {
        return TRL_INVALID_ARGUMENT;
    }
    pipeline->ran = true;
    trl_conn *conn = pipeline->conn;
    bool skip = pipeline->on_error == TRL_ON_ERROR_SKIP;
    bool began = trl_conn_in_transaction(conn);
    running r = {pipeline, NULL, NULL, 0U, conn->autocommit && !skip};
    r.sent = sent_of(pipeline, skip, &r.count);
    r.outcomes = r.sent == NULL ? NULL : trl_calloc(r.count == 0U ? 1U : r.count, sizeof *r.outcomes);
    if (r.outcomes == NULL) {
        free(r.sent);
        trl_failure_no_memory(&conn->failure);
        return TRL_NO_MEMORY;
    }
    for (size_t i = 0U; i < r.count; ++i) {
        trl_failure_init(&r.outcomes[i].failure);
    }
    bool ran = windows_run(&r, skip) && (!skip || settle(&r, began));
    if (!ran) {
        outcomes_free(r.outcomes, r.count);
        free(r.sent);
        return TRL_FAILED;
    }
    shown(pipeline, &r);
    free(r.sent);
    return TRL_OK;
}

trl_piped trl_pipeline_outcome(const trl_pipeline *pipeline, size_t request) {
    if (pipeline == NULL || request >= pipeline->outcome_count) {
        return TRL_PIPED_SKIPPED;
    }
    return pipeline->outcomes[request].piped;
}

const trl_result *trl_pipeline_result(const trl_pipeline *pipeline, size_t request) {
    if (pipeline == NULL || request >= pipeline->outcome_count) {
        return NULL;
    }
    return pipeline->outcomes[request].result;
}

const trl_failure *trl_pipeline_failure(const trl_pipeline *pipeline, size_t request) {
    if (trl_pipeline_outcome(pipeline, request) != TRL_PIPED_FAILED) {
        return NULL;
    }
    return &pipeline->outcomes[request].failure;
}

void trl_pipeline_free(trl_pipeline *pipeline) {
    if (pipeline == NULL) {
        return;
    }
    for (size_t i = 0U; i < pipeline->count; ++i) {
        free(pipeline->queued[i].sql);
        trl_params_free(pipeline->queued[i].params);
    }
    free(pipeline->queued);
    outcomes_free(pipeline->outcomes, pipeline->outcome_count);
    free(pipeline);
}
