/**
 * @file import.c
 * @brief Import [CLIENT Importing]: a source's records inserted into a
 * table through one prepared INSERT, its columns the source's headings or
 * else the table's, as a prepared query's description gives them
 * [ISO/IEC 9075:1992 §17.6, §17.9 GR3]; each value cast by the server to
 * its column's type but a string for a character string column, assigned
 * to it as INSERT assigns, so one too long is refused, not cut. A batch of
 * rows is committed at a time; a failure rolls the batch back and stops,
 * or, if asked, a bad row is skipped and reported. Where the server grants
 * `copy`, no row is to be skipped and no column is a large object's, the
 * rows go by bulk copy (`import_copy.c`).
 */
#include "bulk/assign.h"
#include "bulk/imported.h"
#include "bulk/names.h"
#include "codec/buffer.h"
#include "data/params.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The SQLCODE of a statement cancelled, which no skipped row hides. */
#define TRL_SQLCODE_CANCELLED (-920)
/* Room for a message of the input's, and for a number's words. */
#define TRL_MESSAGE_BYTES 512U

void trl_imported_condition(trl_failure *into, const trl_failure *failure) {
    trl_failure_copy(into, failure);
    into->placed = false;
    into->position = 0U;
}

/* Marks the import stopped, at `position` if given. */
static void stopped(trl_imported *imported, const trl_position *position) {
    imported->failed = true;
    imported->placed = position != NULL;
    if (position != NULL) {
        imported->position = *position;
    }
}

void trl_imported_stop_input(trl_imported *imported, const trl_position *position, const char *message) {
    stopped(imported, position);
    imported->sql = false;
    free(imported->message);
    size_t length = strlen(message);
    imported->message = trl_malloc(length + 1U);
    if (imported->message != NULL) {
        memcpy(imported->message, message, length + 1U);
    }
}

void trl_imported_stop_sql(trl_imported *imported, const trl_position *position, const trl_failure *failure) {
    stopped(imported, position);
    imported->sql = true;
    trl_imported_condition(&imported->failure, failure);
}

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run_sql(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    trl_result_free(result);
    return ran == TRL_OK;
}

/* Stops the import, the batch under way rolled back. */
static void stop_input(trl_import_run *run, const trl_position *position, const char *message) {
    if (!trl_conn_is_lost(run->conn) && trl_conn_in_transaction(run->conn)) {
        /* The import has failed already: a rollback that fails too loses
           the connection, which ends the session's transaction. */
        (void)run_sql(run->conn, "ROLLBACK");
    }
    run->imported->rolled_back = run->in_batch;
    trl_imported_stop_input(run->imported, position, message);
}

/* Stops the import for an SQL condition, the batch under way rolled back. */
static void stop_sql(trl_import_run *run, const trl_position *position, const trl_failure *failure) {
    trl_failure kept;
    trl_failure_init(&kept);
    trl_failure_copy(&kept, failure);
    if (!trl_conn_is_lost(run->conn) && trl_conn_in_transaction(run->conn)) {
        (void)run_sql(run->conn, "ROLLBACK");
    }
    run->imported->rolled_back = run->in_batch;
    trl_imported_stop_sql(run->imported, position, &kept);
    trl_failure_clear(&kept);
}

/* Appends `name` as a delimited identifier, null-ended, to the columns. */
static void add_column(trl_import_run *run, const char *name, size_t length) {
    trl_put_delimited(&run->columns, name, length);
    trl_put_u8(&run->columns, 0U);
    ++run->column_count;
}

/* The columns of the table, in order, as a query of them describes them
   [ISO/IEC 9075:1992 §17.9 GR3]. */
static bool described(trl_import_run *run) {
    trl_buffer sql;
    trl_buffer_init(&sql);
    trl_put_text(&sql, "SELECT * FROM ");
    trl_put_raw(&sql, run->table.data, run->table.length);
    trl_put_u8(&sql, 0U);
    trl_statement *query = NULL;
    bool prepared = sql.state == TRL_BUFFER_OK && trl_prepare(run->conn, (const char *)sql.data, &query) == TRL_OK;
    trl_buffer_free(&sql);
    for (size_t i = 0U; prepared && i < trl_statement_column_count(query); ++i) {
        const char *name = trl_statement_column_name(query, i);
        add_column(run, name == NULL ? "" : name, name == NULL ? 0U : strlen(name));
    }
    prepared = prepared && trl_deallocate(run->conn, query) == TRL_OK;
    trl_statement_free(query);
    if (!prepared) {
        stop_sql(run, NULL, trl_conn_failure(run->conn));
    }
    return prepared;
}

/* Stops the import as memory cannot be had; false. */
static bool without_memory(trl_import_run *run) {
    trl_failure no_memory;
    trl_failure_init(&no_memory);
    trl_failure_no_memory(&no_memory);
    trl_imported_stop_sql(run->imported, NULL, &no_memory);
    trl_failure_clear(&no_memory);
    return false;
}

/* The columns the source's headings name, or else the table's. */
static bool columns_of(trl_import_run *run) {
    trl_headings headings = {NULL, 0U};
    trl_source_error error = {{0U, 0U}, ""};
    if (run->source->headings(run->source->context, &headings, &error) == TRL_READ_FAILED) {
        trl_imported_stop_input(run->imported, &error.position, error.message);
        return false;
    }
    if (headings.names == NULL) {
        return described(run);
    }
    for (size_t i = 0U; i < headings.count; ++i) {
        trl_buffer name;
        trl_buffer_init(&name);
        trl_column_of_heading(headings.names[i], &name);
        if (name.state != TRL_BUFFER_OK) {
            trl_buffer_free(&name);
            return without_memory(run);
        }
        add_column(run, name.length == 0U ? "" : (const char *)name.data, name.length);
        trl_buffer_free(&name);
    }
    return true;
}

/* Writes the table's name as SQL, ended by a null not counted, so a copy
   names it too; false, the import stopped, if it is no <table name>. */
static bool table_named(trl_import_run *run) {
    if (!trl_table_sql(run->how->table, &run->table)) {
        char message[TRL_MESSAGE_BYTES];
        trl_format(message, sizeof message,
                   "[ISO/IEC 9075:1992 \xc2\xa7"
                   "5.4] %s is not a <table name>",
                   run->how->table);
        trl_imported_stop_input(run->imported, NULL, message);
        return false;
    }
    trl_put_u8(&run->table, 0U);
    if (run->table.state != TRL_BUFFER_OK) {
        return without_memory(run);
    }
    --run->table.length;
    return true;
}

/* Prepares the INSERT of the source's records into the table. */
static bool prepared(trl_import_run *run) {
    if (trl_conn_in_transaction(run->conn)) {
        trl_imported_stop_input(run->imported, NULL,
                                "[DESIGN] an import begins with no transaction active, as it commits its batches");
        return false;
    }
    if (!table_named(run) || !columns_of(run)) {
        return false;
    }
    trl_buffer sql;
    trl_buffer_init(&sql);
    trl_put_text(&sql, "INSERT INTO ");
    trl_put_raw(&sql, run->table.data, run->table.length);
    trl_put_text(&sql, " (");
    const char *name = (const char *)run->columns.data;
    for (size_t i = 0U; i < run->column_count; ++i) {
        trl_put_text(&sql, i == 0U ? "" : ", ");
        trl_put_text(&sql, name);
        name += strlen(name) + 1U;
    }
    trl_put_text(&sql, ") VALUES (");
    for (size_t i = 0U; i < run->column_count; ++i) {
        trl_put_text(&sql, i == 0U ? "?" : ", ?");
    }
    trl_put_text(&sql, ")");
    trl_put_u8(&sql, 0U);
    bool made = sql.state == TRL_BUFFER_OK && trl_prepare(run->conn, (const char *)sql.data, &run->insert) == TRL_OK;
    trl_buffer_free(&sql);
    if (!made) {
        trl_imported_stop_sql(run->imported, NULL, trl_conn_failure(run->conn));
    }
    return made;
}

bool trl_import_checked(trl_import_run *run, trl_failure *failure, char *message, size_t capacity) {
    size_t wanted = trl_statement_parameter_count(run->insert);
    size_t count = trl_params_count(run->values);
    message[0] = '\0';
    if (count != wanted) {
        trl_format(message, capacity, "[DESIGN] %lu fields, for %lu columns", (unsigned long)count,
                   (unsigned long)wanted);
        return false;
    }
    trl_params_clear(run->sent);
    const trl_value *values = trl_params_values(run->values);
    for (size_t i = 0U; i < count; ++i) {
        trl_status added =
            trl_import_value(run->conn, &values[i], trl_statement_parameter_type(run->insert, i), run->sent, failure);
        if (added == TRL_NO_MEMORY) {
            trl_failure_no_memory(failure);
        }
        if (added != TRL_OK) {
            return false;
        }
    }
    return true;
}

/* Commits the batch under way, if a transaction is active. */
static bool commit(trl_import_run *run) {
    if (trl_conn_in_transaction(run->conn) && !run_sql(run->conn, "COMMIT")) {
        run->imported->rolled_back = run->in_batch;
        trl_imported_stop_sql(run->imported, NULL, trl_conn_failure(run->conn));
        return false;
    }
    run->imported->rows += run->in_batch;
    run->in_batch = 0U;
    return true;
}

/* Skips a row and reports it, if the import skips bad rows; else stops. */
static bool reject(trl_import_run *run, const trl_position *at, const char *message, const trl_failure *failure) {
    if (!run->how->skip) {
        if (failure != NULL) {
            stop_sql(run, at, failure);
        } else {
            stop_input(run, at, message);
        }
        return false;
    }
    ++run->imported->rejected;
    if (run->how->rejected != NULL) {
        trl_failure condition;
        trl_failure_init(&condition);
        trl_rejected row = {*at, {failure != NULL, message, NULL}};
        if (failure != NULL) {
            trl_imported_condition(&condition, failure);
            row.cause.message = trl_failure_message(&condition);
            row.cause.failure = &condition;
        }
        run->how->rejected(run->how->context, &row);
        trl_failure_clear(&condition);
    }
    return true;
}

/* Inserts the record read, committing its batch if it ends one. */
static bool row(trl_import_run *run, const trl_position *at) {
    char message[TRL_MESSAGE_BYTES];
    trl_failure failure;
    trl_failure_init(&failure);
    bool checked = trl_import_checked(run, &failure, message, sizeof message);
    if (!checked && message[0] != '\0') {
        trl_failure_clear(&failure);
        return reject(run, at, message, NULL);
    }
    trl_result *result = NULL;
    bool inserted = checked && trl_execute(run->conn, run->insert, run->sent, &result) == TRL_OK;
    trl_result_free(result);
    if (checked && !inserted) {
        trl_failure_copy(&failure, trl_conn_failure(run->conn));
    }
    bool going = true;
    if (inserted) {
        ++run->in_batch;
        going = run->in_batch != run->how->batch || commit(run);
    } else {
        /* A row is skipped only if it alone was undone: the batch's
           transaction goes on, and the connection. */
        bool alone = run->in_batch == 0U || trl_conn_in_transaction(run->conn);
        if (!alone || trl_failure_ends_connection(&failure) || trl_failure_sqlcode(&failure) == TRL_SQLCODE_CANCELLED) {
            stop_sql(run, at, &failure);
            going = false;
        } else {
            going = reject(run, at, NULL, &failure);
        }
    }
    trl_failure_clear(&failure);
    return going;
}

/* Inserts each of the source's records, then commits the last batch. */
static bool rows(trl_import_run *run) {
    for (;;) {
        trl_params_clear(run->values);
        trl_position at = {0U, 0U};
        trl_source_error error = {{0U, 0U}, ""};
        trl_source_read read = run->source->record(run->source->context, run->values, &at, &error);
        if (read == TRL_READ_END) {
            return commit(run);
        }
        if (read != TRL_READ_OK) {
            stop_input(run, &error.position, error.message);
            return false;
        }
        if (!row(run, &at)) {
            return false;
        }
    }
}

/* Whether the import goes by copy: granted, no bad row skipped, no large
   object column. */
static bool copies(const trl_import_run *run) {
    if (!trl_conn_granted(run->conn, TRL_CAPABILITY_COPY) || run->how->skip) {
        return false;
    }
    for (size_t i = 0U; i < trl_statement_parameter_count(run->insert); ++i) {
        trl_kind kind = trl_type_kind(trl_statement_parameter_type(run->insert, i));
        if (kind == TRL_KIND_CLOB || kind == TRL_KIND_BLOB) {
            return false;
        }
    }
    return true;
}

/* Imports by INSERT, or by copy where it may, the INSERT then let go of. */
static bool imported_by(trl_import_run *run) {
    if (copies(run)) {
        /* Its parameters' types known, the INSERT is of no more use. */
        if (trl_deallocate(run->conn, run->insert) != TRL_OK) {
            trl_imported_stop_sql(run->imported, NULL, trl_conn_failure(run->conn));
            return false;
        }
        return trl_import_copied(run);
    }
    bool done = rows(run);
    if (trl_conn_is_lost(run->conn)) {
        return false;
    }
    /* Let go of even if the import stopped, so the server holds nothing. */
    bool freed = trl_deallocate(run->conn, run->insert) == TRL_OK;
    if (done && !freed) {
        run->imported->rolled_back = 0U;
        trl_imported_stop_sql(run->imported, NULL, trl_conn_failure(run->conn));
    }
    return done && freed;
}

trl_status trl_import_rows(trl_conn *conn, const trl_source *source, const trl_import *how, trl_imported **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (conn == NULL || source == NULL || source->headings == NULL || source->record == NULL || how == NULL ||
        how->table == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_imported *imported = trl_calloc(1U, sizeof *imported);
    if (imported == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&imported->failure);
    trl_import_run run;
    memset(&run, 0, sizeof run);
    run.conn = conn;
    run.source = source;
    run.how = how;
    run.imported = imported;
    trl_buffer_init(&run.table);
    trl_buffer_init(&run.columns);
    bool done = trl_params_new(&run.values) == TRL_OK && trl_params_new(&run.sent) == TRL_OK;
    if (!done) {
        trl_failure no_memory;
        trl_failure_init(&no_memory);
        trl_failure_no_memory(&no_memory);
        trl_imported_stop_sql(imported, NULL, &no_memory);
        trl_failure_clear(&no_memory);
    }
    done = done && prepared(&run) && imported_by(&run);
    trl_statement_free(run.insert);
    trl_params_free(run.values);
    trl_params_free(run.sent);
    trl_buffer_free(&run.table);
    trl_buffer_free(&run.columns);
    *out = imported;
    return done ? TRL_OK : TRL_FAILED;
}

uint64_t trl_imported_rows(const trl_imported *imported) {
    return imported == NULL ? 0U : imported->rows;
}

uint64_t trl_imported_rejected(const trl_imported *imported) {
    return imported == NULL ? 0U : imported->rejected;
}

uint64_t trl_imported_rolled_back(const trl_imported *imported) {
    return imported == NULL ? 0U : imported->rolled_back;
}

bool trl_imported_cause(const trl_imported *imported, trl_cause *cause) {
    if (imported == NULL || cause == NULL || !imported->failed) {
        return false;
    }
    cause->sql = imported->sql;
    cause->failure = imported->sql ? &imported->failure : NULL;
    const char *message = imported->message == NULL ? "" : imported->message;
    cause->message = imported->sql ? trl_failure_message(&imported->failure) : message;
    return true;
}

bool trl_imported_position(const trl_imported *imported, trl_position *position) {
    if (imported == NULL || position == NULL || !imported->failed || !imported->placed) {
        return false;
    }
    *position = imported->position;
    return true;
}

/* "1 row", "2 rows". */
static const char *rows_word(uint64_t n) {
    return n == 1U ? "row" : "rows";
}

/* Appends where the import stopped, "record 3, line 4: ", if a record did. */
static void put_where(trl_buffer *text, const trl_imported *imported) {
    if (!imported->placed) {
        return;
    }
    char part[TRL_MESSAGE_BYTES];
    trl_format(part, sizeof part, "record %llu", (unsigned long long)imported->position.record);
    trl_put_text(text, part);
    if (imported->position.line > 0U) {
        trl_format(part, sizeof part, ", line %llu", (unsigned long long)imported->position.line);
        trl_put_text(text, part);
    }
    trl_put_text(text, ": ");
}

/* Appends why the import stopped: the SQL condition's text, or the input's. */
static void put_cause(trl_buffer *text, const trl_imported *imported) {
    if (!imported->sql) {
        trl_put_text(text, imported->message == NULL ? "" : imported->message);
        return;
    }
    size_t length = trl_failure_text(&imported->failure, NULL, 0U);
    char *line = trl_malloc(length + 1U);
    if (line != NULL) {
        (void)trl_failure_text(&imported->failure, line, length + 1U);
        trl_put_raw(text, (const uint8_t *)line, length);
    }
    free(line);
}

size_t trl_imported_text(const trl_imported *imported, char *out, size_t capacity) {
    if (imported == NULL || !imported->failed) {
        if (out != NULL && capacity > 0U) {
            out[0] = '\0';
        }
        return 0U;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    put_where(&text, imported);
    put_cause(&text, imported);
    char part[TRL_MESSAGE_BYTES];
    trl_format(part, sizeof part, "; %llu %s committed, %llu rolled back", (unsigned long long)imported->rows,
               rows_word(imported->rows), (unsigned long long)imported->rolled_back);
    trl_put_text(&text, part);
    size_t length = text.length;
    if (out != NULL && capacity > 0U) {
        size_t kept = length < capacity - 1U ? length : capacity - 1U;
        if (kept > 0U) {
            memcpy(out, text.data, kept);
        }
        out[kept] = '\0';
    }
    trl_buffer_free(&text);
    return length;
}

void trl_imported_free(trl_imported *imported) {
    if (imported == NULL) {
        return;
    }
    free(imported->message);
    trl_failure_clear(&imported->failure);
    free(imported);
}
