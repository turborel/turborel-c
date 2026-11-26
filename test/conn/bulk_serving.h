/**
 * @file bulk_serving.h
 * @brief A server of a test's own for copies, imports and exports: what
 * it grants, and which of its answers fail, as the test plans.
 */
#ifndef TRL_TEST_BULK_SERVING_H
#define TRL_TEST_BULK_SERVING_H

#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/* How a query that is no COMMIT or ROLLBACK is answered. */
typedef enum bulk_export {
    /* Two columns, two batches of rows, a Complete. */
    EXPORT_ROWS = 0,
    /* An Error. */
    EXPORT_ERROR,
    /* A Pong, out of turn. */
    EXPORT_OUT_OF_TURN,
    /* A Description and a batch, then an Error. */
    EXPORT_ROWS_THEN_ERROR,
    /* A Description and a batch, then a Pong, out of turn. */
    EXPORT_ROWS_THEN_OUT_OF_TURN,
    /* A Complete alone, of a statement that is no query. */
    EXPORT_NO_QUERY,
    /* Two columns, rows of values CSV must quote, and a null, then a Complete. */
    EXPORT_QUOTED,
} bulk_export;

/** @brief What the server grants and does. */
typedef struct bulk_plan {
    /** @brief Whether it grants `copy`. */
    bool copy;
    /** @brief Whether it grants `columns` too, with `copy`. */
    bool columns;
    /** @brief The Execute that fails, from 1; 0 for none. */
    unsigned int fail_execute;
    /** @brief Whether that failure is of class 08. */
    bool fail_lost;
    /** @brief Whether the transaction stays active after it, the row alone undone. */
    bool row_alone;
    /** @brief Whether COMMIT fails. */
    bool fail_commit;
    /** @brief Whether the INSERT's Prepare fails. */
    bool fail_insert;
    /** @brief Whether the table's description, a SELECT's Prepare, fails. */
    bool fail_describe;
    /** @brief Whether Deallocate fails. */
    bool fail_deallocate;
    /** @brief Whether the INSERT's parameters are of a character large object. */
    bool lob_columns;
    /** @brief The row a copy's failure names, from 1; 0 for none. */
    unsigned int copy_fail_row;
    /** @brief Whether a copy fails, its detail `copy_detail`. */
    bool copy_fail;
    /** @brief The detail of a copy's failure; null for none. */
    const char *copy_detail;
    /** @brief How a query is answered. */
    bulk_export query;
    /** @brief The Executes seen. */
    unsigned int executes;
    /** @brief The COMMITs seen. */
    unsigned int commits;
    /** @brief The ROLLBACKs seen. */
    unsigned int rollbacks;
    /** @brief The rows of the copies seen. */
    uint64_t copied;
    /** @brief The copies seen. */
    unsigned int copies;
    /** @brief The CopyData and CopyColumns seen. */
    unsigned int by_row;
    /** @brief The CopyColumns seen. */
    unsigned int by_column;
} bulk_plan;

/** @brief Answers each request as `s->data`, a bulk_plan, says. */
void bulk_respond(script *s, uint8_t kind, trl_slice body);

/** @brief A connection over `s`, logged in by trust, served as `plan` says. */
trl_conn *bulk_connect(script *s, bulk_plan *plan);

/** @brief Writes an Error of class `sqlstate`, the transaction `active` after it, its detail `detail` if not null. */
void bulk_error(script *s, const char *sqlstate, bool active, const char *detail);

#endif
