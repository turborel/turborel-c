/**
 * @file simple.c
 * @brief The standard API's shortest paths (RULES.md D2): a connection
 * made of a URI, a statement run with its parameters given as text, and
 * a value read as text by its row and column, the text kept by its
 * result.
 */
#include "codec/arena.h"
#include "conn/conn.h"
#include "failure.h"
#include "memory.h"
#include "stmt/result.h" // NOLINT(misc-include-cleaner): the result's fields, which the checker does not count
#include "stmt/stmt.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

trl_status trl_connect_uri(const char *uri, trl_conn **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (uri == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_settings *settings = NULL;
    trl_status laid = trl_settings_from_uri(uri, &settings);
    if (laid == TRL_FAILED) {
        *out = trl_conn_failed_with(trl_settings_failure(settings));
        trl_settings_free(settings);
        return *out == NULL ? TRL_NO_MEMORY : TRL_FAILED;
    }
    trl_status made = laid == TRL_OK ? trl_settings_connect(settings, out) : laid;
    trl_settings_free(settings);
    return made;
}

/* Parameters of `count` texts, each null for the null value. */
static trl_status texts_of(size_t count, const char *const *values, trl_params **out) {
    trl_status made = trl_params_new(out);
    for (size_t i = 0U; made == TRL_OK && i < count; ++i) {
        made = values[i] == NULL ? trl_params_add_null(*out) : trl_params_add_text(*out, values[i], strlen(values[i]));
    }
    return made;
}

/* Prepares `sql`, executes it with `params` and destroys it, the
   connection telling the execution's completion and failure. */
static trl_status run_once(trl_conn *conn, const char *sql, const trl_params *params, trl_result **out) {
    trl_statement *statement = NULL;
    trl_status ran = trl_prepare(conn, sql, &statement);
    ran = ran == TRL_OK ? trl_execute(conn, statement, params, out) : ran;
    trl_deallocate_after_run(conn, statement);
    trl_statement_free(statement);
    return ran;
}

trl_status trl_query_params(trl_conn *conn, const char *sql, size_t count, const char *const *values,
                            trl_result **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || sql == NULL || out == NULL || (count > 0U && values == NULL)) {
        return TRL_INVALID_ARGUMENT;
    }
    if (count == 0U) {
        return trl_query(conn, sql, out);
    }
    trl_params *params = NULL;
    trl_status made = texts_of(count, values, &params);
    trl_status ran = made == TRL_OK ? run_once(conn, sql, params, out) : made;
    trl_params_free(params);
    if (made == TRL_NO_MEMORY) {
        trl_failure_no_memory(&conn->failure);
    }
    return ran;
}

trl_status trl_query_values(trl_conn *conn, const char *sql, const trl_params *params, trl_result **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || sql == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (trl_params_count(params) == 0U) {
        return trl_query(conn, sql, out);
    }
    return run_once(conn, sql, params, out);
}

/* The texts of a result's values, made once; false if memory cannot be had. */
static bool texts_made(trl_result *result) {
    if (result->texts != NULL) {
        return true;
    }
    size_t cells = result->row_count * result->column_count;
    result->texts = (char **)trl_calloc(cells == 0U ? 1U : cells, sizeof *result->texts);
    return result->texts != NULL;
}

const char *trl_result_text(trl_result *result, size_t row, size_t column) {
    const trl_value *value = trl_result_value(result, row, column);
    if (value == NULL || trl_value_is_null(value) || !texts_made(result)) {
        return NULL;
    }
    char **cell = &result->texts[(row * result->column_count) + column];
    if (*cell != NULL) {
        return *cell;
    }
    size_t length = 0U;
    if (trl_value_to_text(value, NULL, 0U, &length) != TRL_OK) {
        return NULL;
    }
    char *text = trl_arena_alloc(&result->arena, length + 1U, 1U);
    if (text == NULL || trl_value_to_text(value, text, length + 1U, NULL) != TRL_OK) {
        return NULL;
    }
    *cell = text;
    return text;
}
