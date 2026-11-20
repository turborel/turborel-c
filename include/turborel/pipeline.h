/**
 * @file pipeline.h
 * @brief Pipelines [CLIENT Pipelines]: a connection's requests queued,
 * written at once, a window at a time, and one outcome read for each, in
 * order, as the server answers each request alone and in turn
 * [PROTOCOL Requests]; so a batch costs one round trip, not one a
 * request.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_PIPELINE_H
#define TRL_PIPELINE_H

#include "turborel/base.h"
#include "turborel/connection.h"
#include "turborel/failure.h"
#include "turborel/statements.h"
#include "turborel/values.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most bytes of requests written before their replies are read. */
#define TRL_PIPELINE_WINDOW 65536

/** @brief What a pipeline does after a request fails. */
typedef enum trl_on_error {
    /** @brief Every request runs; a failure is its request's outcome alone. */
    TRL_ON_ERROR_CONTINUE = 0,
    /** @brief The batch stops at its first failure: the requests after it undone or never sent. */
    TRL_ON_ERROR_SKIP,
} trl_on_error;

/** @brief What one request of a pipeline came to. */
typedef enum trl_piped {
    /** @brief It ran: its result. */
    TRL_PIPED_DONE = 0,
    /** @brief It failed, as the server reported it. */
    TRL_PIPED_FAILED,
    /** @brief Not run, or run and undone, after an earlier request failed under TRL_ON_ERROR_SKIP. */
    TRL_PIPED_SKIPPED,
} trl_piped;

/** @brief Requests queued on a connection, written at once. */
typedef struct trl_pipeline trl_pipeline;

/**
 * @brief Makes an empty pipeline on a connection, TRL_ON_ERROR_CONTINUE.
 *
 * @param conn The connection, which the pipeline uses until freed.
 * @param out Where it goes, the caller's to free.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_pipeline_new(trl_conn *conn, trl_pipeline **out);

/**
 * @brief Queues a statement, run as trl_query() runs it.
 *
 * @param pipeline The pipeline.
 * @param sql The statement, copied.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_pipeline_query(trl_pipeline *pipeline, const char *sql);

/**
 * @brief Queues an execution of a prepared statement, as trl_execute()
 * runs it: all of a query's rows.
 *
 * @param pipeline The pipeline.
 * @param statement The statement, which must last until the pipeline runs.
 * @param params Its parameters, copied; null for none.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pointer; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_pipeline_execute(trl_pipeline *pipeline, const trl_statement *statement,
                                        const trl_params *params);

/**
 * @brief Says what the batch does after a request fails.
 *
 * @param pipeline The pipeline.
 * @param on_error What.
 * @return TRL_OK; TRL_INVALID_ARGUMENT of a null pipeline.
 */
TRL_API trl_status trl_pipeline_on_error(trl_pipeline *pipeline, trl_on_error on_error);

/**
 * @brief How many requests are queued, or, once run, how many outcomes it has.
 *
 * @param pipeline The pipeline; null gives 0.
 * @return How many.
 */
TRL_API size_t trl_pipeline_count(const trl_pipeline *pipeline);

/**
 * @brief Writes the requests queued, a window of at most
 * TRL_PIPELINE_WINDOW bytes at a time, its replies read before the next is
 * written, and reads one outcome for each, in order. Under autocommit
 * each request is its own transaction. Under TRL_ON_ERROR_SKIP a
 * savepoint is set before each request but the first, the requests after
 * a failed one undone to the savepoint after it, or never sent if their
 * window was not yet written, each then TRL_PIPED_SKIPPED; the batch is
 * one transaction, committed as it ends under autocommit where it began
 * none. A pipeline runs once.
 *
 * @param pipeline The pipeline.
 * @return TRL_OK, each outcome then given; TRL_INVALID_ARGUMENT of a null
 * pipeline or one run; TRL_FAILED with the connection's failure, of the
 * connection lost (SQLSTATE 08006), or a skipping batch that cannot be
 * undone; TRL_NO_MEMORY.
 */
TRL_API trl_status trl_pipeline_run(trl_pipeline *pipeline);

/**
 * @brief What a request run came to.
 *
 * @param pipeline The pipeline, run.
 * @param request Which, from 0, in the order queued.
 * @return Its outcome; TRL_PIPED_SKIPPED past the last, or of a pipeline
 * not run.
 */
TRL_API trl_piped trl_pipeline_outcome(const trl_pipeline *pipeline, size_t request);

/**
 * @brief The result of a request that ran.
 *
 * @param pipeline The pipeline, run.
 * @param request Which, from 0.
 * @return Its result, the pipeline's, valid until it is freed; null of
 * one that did not run.
 */
TRL_API const trl_result *trl_pipeline_result(const trl_pipeline *pipeline, size_t request);

/**
 * @brief How a request failed.
 *
 * @param pipeline The pipeline, run.
 * @param request Which, from 0.
 * @return Its failure, the pipeline's; null of one that did not fail.
 */
TRL_API const trl_failure *trl_pipeline_failure(const trl_pipeline *pipeline, size_t request);

/**
 * @brief Frees a pipeline, and its outcomes.
 *
 * @param pipeline It; null does nothing.
 */
TRL_API void trl_pipeline_free(trl_pipeline *pipeline);

#ifdef __cplusplus
}
#endif

#endif
