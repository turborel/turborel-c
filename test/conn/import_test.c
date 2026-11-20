/**
 * @file import_test.c
 * @brief Imports against a server of the test's own: batches committed,
 * rows refused and skipped or stopping the import, a commit that fails,
 * the connection lost, the source failing, and imports by copy whose copy
 * fails at a row.
 */
#include "bulk_serving.h"
#include "format.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The most records a test's source gives, and fields of each. */
#define MOST_RECORDS 8U
#define MOST_FIELDS 3U
/* Room for an import's text. */
#define TEXT_BYTES 512U

/* A source of the test's own: records of up to three fields, null where
   a field is; headings or none; failing at a record if told. */
typedef struct records {
    const char *fields[MOST_RECORDS][MOST_FIELDS];
    size_t widths[MOST_RECORDS];
    size_t count;
    size_t at;
    const char *const *headings;
    size_t heading_count;
    bool headings_fail;
    size_t fail_at;
} records;

static trl_source_read headings_of(void *context, trl_headings *out, trl_source_error *error) {
    records *r = context;
    if (r->headings_fail) {
        error->position.record = 0U;
        error->position.line = 1U;
        error->message = "no header";
        return TRL_READ_FAILED;
    }
    out->names = r->headings;
    out->count = r->heading_count;
    return TRL_READ_OK;
}

static trl_source_read record_of(void *context, trl_params *values, trl_position *at, trl_source_error *error) {
    records *r = context;
    if (r->at == r->count) {
        return TRL_READ_END;
    }
    ++r->at;
    at->record = r->at;
    at->line = r->at + 1U;
    if (r->at == r->fail_at) {
        error->position = *at;
        error->message = "the input is cut";
        return TRL_READ_FAILED;
    }
    for (size_t i = 0U; i < r->widths[r->at - 1U]; ++i) {
        const char *field = r->fields[r->at - 1U][i];
        (void)(field == NULL ? trl_params_add_null(values) : trl_params_add_text(values, field, strlen(field)));
    }
    return TRL_READ_OK;
}

/* `count` records of two fields, "a" and "b". */
static records records_of(size_t count) {
    records r;
    memset(&r, 0, sizeof r);
    r.count = count;
    for (size_t i = 0U; i < count; ++i) {
        r.fields[i][0] = "a";
        r.fields[i][1] = "b";
        r.widths[i] = 2U;
    }
    return r;
}

/* What a handler of rows skipped was given. */
typedef struct skipped {
    size_t count;
    uint64_t last_record;
    bool last_sql;
    char last_sqlstate[8];
} skipped;

static void on_rejected(void *context, const trl_rejected *row) {
    skipped *k = context;
    ++k->count;
    k->last_record = row->position.record;
    k->last_sql = row->cause.sql;
    trl_format(k->last_sqlstate, sizeof k->last_sqlstate, "%s",
               row->cause.failure == NULL ? "" : trl_failure_sqlstate(row->cause.failure));
}

/* Imports `r` into T over a server planned as `plan`; the import's text
   into `text`, and whether it succeeded. */
static bool imported(bulk_plan *plan, records *r, trl_import *how, trl_imported **out, char *text) {
    script s;
    trl_conn *conn = bulk_connect(&s, plan);
    trl_source source = {r, headings_of, record_of};
    trl_status status = trl_import_rows(conn, &source, how, out);
    (void)trl_imported_text(*out, text, TEXT_BYTES);
    trl_conn_free(conn);
    script_free(&s);
    return status == TRL_OK;
}

/**
 * Verifies: [CLIENT Importing] an import inserts each record by one
 * prepared INSERT of the table's columns, as a query of them describes
 * them, committing a batch at a time and the last batch at the end.
 */
static void batches_are_committed(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    records r = records_of(5U);
    trl_import how = {"t", 2U, false, NULL, NULL};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, trl_imported_rows(done) == 5U && trl_imported_rejected(done) == 0U);
    TRL_CHECK(test, plan.executes == 5U && plan.commits == 3U && text[0] == '\0');
    trl_cause cause;
    trl_position at;
    TRL_CHECK(test, !trl_imported_cause(done, &cause) && !trl_imported_position(done, &at));
    TRL_CHECK(test, trl_imported_rolled_back(done) == 0U);
    trl_imported_free(done);
    static const char *const named[] = {"x", "\"Y\""};
    r = records_of(1U);
    r.headings = named;
    r.heading_count = 2U;
    memset(&plan, 0, sizeof plan);
    TRL_CHECK(test, imported(&plan, &r, &how, &done, text) && trl_imported_rows(done) == 1U);
    trl_imported_free(done);
}

/**
 * Verifies: [CLIENT Importing] a row the server refuses, the row alone
 * undone, is skipped and given to the handler if asked, the batch going
 * on; else it stops the import, its batch rolled back, saying the record
 * and line, the rows committed and those rolled back.
 */
static void refused_rows_are_skipped_or_stop(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.fail_execute = 2U;
    plan.row_alone = true;
    records r = records_of(4U);
    skipped k;
    memset(&k, 0, sizeof k);
    trl_import how = {"t", 10U, true, on_rejected, &k};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, trl_imported_rows(done) == 3U && trl_imported_rejected(done) == 1U);
    TRL_CHECK(test, k.count == 1U && k.last_record == 2U && k.last_sql && strcmp(k.last_sqlstate, "23505") == 0);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.fail_execute = 3U;
    plan.row_alone = true;
    r = records_of(4U);
    how.skip = false;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, strcmp(text, "record 3, line 4: SQLCODE -803 (SQLSTATE 23505): a row is refused; "
                                 "0 rows committed, 2 rolled back") == 0);
    trl_cause cause;
    trl_position at;
    TRL_CHECK(test, trl_imported_cause(done, &cause) && cause.sql && cause.failure != NULL);
    TRL_CHECK(test, trl_imported_position(done, &at) && at.record == 3U && plan.rollbacks == 1U);
    trl_imported_free(done);
}

/**
 * Verifies: [CLIENT Importing] a record of too many fields, or a string
 * longer than its column, is refused before it is sent: skipped if asked,
 * the latter as an SQL condition (SQLSTATE 22001); a row refused when
 * others of its batch were undone with it stops the import.
 */
static void rows_refused_before_sending(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    records r = records_of(3U);
    r.fields[0][2] = "c";
    r.widths[0] = 3U;
    r.fields[1][0] = "twenty-one characters";
    skipped k;
    memset(&k, 0, sizeof k);
    trl_import how = {"t", 0U, true, on_rejected, &k};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, trl_imported_rows(done) == 1U && trl_imported_rejected(done) == 2U && plan.executes == 1U);
    TRL_CHECK(test, k.last_record == 2U && k.last_sql && strcmp(k.last_sqlstate, "22001") == 0);
    trl_imported_free(done);
    how.skip = false;
    r = records_of(2U);
    r.widths[1] = 1U;
    memset(&plan, 0, sizeof plan);
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test,
              strcmp(text, "record 2, line 3: [DESIGN] 1 fields, for 2 columns; 0 rows committed, 1 rolled back") == 0);
    trl_cause cause;
    TRL_CHECK(test, trl_imported_cause(done, &cause) && !cause.sql && cause.failure == NULL);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.fail_execute = 2U;
    r = records_of(3U);
    how.skip = true;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && trl_imported_rejected(done) == 0U);
    trl_imported_free(done);
}

/**
 * Verifies: [CLIENT Importing] a commit that fails, the connection lost,
 * and a source that fails each stop the import, saying why; the batch
 * under way is rolled back where the session goes on.
 */
static void failures_stop_the_import(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.fail_commit = true;
    records r = records_of(2U);
    trl_import how = {"t", 0U, false, NULL, NULL};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && trl_imported_rolled_back(done) == 2U);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.fail_execute = 1U;
    plan.fail_lost = true;
    r = records_of(2U);
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "08006") != NULL);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    r = records_of(3U);
    r.fail_at = 2U;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, strcmp(text, "record 2, line 3: the input is cut; 0 rows committed, 1 rolled back") == 0);
    TRL_CHECK(test, plan.rollbacks == 1U);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    r = records_of(1U);
    r.headings_fail = true;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, strcmp(text, "record 0, line 1: no header; 0 rows committed, 0 rolled back") == 0);
    trl_imported_free(done);
}

/**
 * Verifies: [CLIENT Importing] an import is refused before any row: a
 * table that is no table name, one whose description fails, an INSERT
 * that cannot be prepared; a Deallocate that fails after the rows fails
 * the import.
 */
static void imports_refused_before_rows(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    records r = records_of(1U);
    trl_import how = {"select", 0U, false, NULL, NULL};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, strstr(text, "select is not a <table name>") != NULL);
    trl_imported_free(done);
    how.table = "t";
    plan.fail_describe = true;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "42000") != NULL);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.fail_insert = true;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "42000") != NULL);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.fail_deallocate = true;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && trl_imported_rows(done) == 0U);
    trl_imported_free(done);
    static const char *const named[] = {"x", "y"};
    r = records_of(1U);
    r.headings = named;
    r.heading_count = 2U;
    plan.executes = 0U;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "26000") != NULL);
    TRL_CHECK(test, trl_imported_rows(done) == 1U && trl_imported_rolled_back(done) == 0U);
    trl_imported_free(done);
}

/**
 * Verifies: [CLIENT Importing] a null connection, source, function of a
 * source, import, table or place for what it did is refused, nothing
 * sent; what an import did, null, or a place for its cause null, gives
 * nothing.
 */
static void import_arguments_are_refused(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    records r = records_of(1U);
    trl_import how = {"t", 0U, false, NULL, NULL};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    trl_source source = {&r, headings_of, record_of};
    TRL_CHECK(test, trl_import_rows(NULL, &source, &how, &done) == TRL_INVALID_ARGUMENT && done == NULL);
    script s;
    trl_conn *conn = bulk_connect(&s, &plan);
    trl_source no_headings = {&r, NULL, record_of};
    trl_source no_records = {&r, headings_of, NULL};
    trl_import no_table = {NULL, 0U, false, NULL, NULL};
    TRL_CHECK(test, trl_import_rows(conn, NULL, &how, &done) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_import_rows(conn, &no_headings, &how, &done) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_import_rows(conn, &no_records, &how, &done) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_import_rows(conn, &source, NULL, &done) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_import_rows(conn, &source, &no_table, &done) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_import_rows(conn, &source, &how, NULL) == TRL_INVALID_ARGUMENT);
    trl_conn_free(conn);
    script_free(&s);
    trl_cause cause;
    trl_position at;
    TRL_CHECK(test, !trl_imported_cause(NULL, &cause) && !trl_imported_position(NULL, &at));
    plan.fail_deallocate = false;
    r = records_of(1U);
    r.fail_at = 1U;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, !trl_imported_cause(done, NULL) && !trl_imported_position(done, NULL));
    trl_imported_free(done);
    TRL_CHECK(test, trl_imported_rows(NULL) == 0U && trl_imported_rejected(NULL) == 0U);
    TRL_CHECK(test, trl_imported_text(NULL, text, sizeof text) == 0U && text[0] == '\0');
    TRL_CHECK(test, trl_imported_rolled_back(NULL) == 0U);
    trl_imported_free(NULL);
}

/**
 * Verifies: [CLIENT Importing] [CLIENT Copying and exporting] with `copy`
 * granted, no row skipped and no large object column, each batch goes by
 * a copy; a copy refused at a row stops the import at that row's record,
 * the rows before it rolled back; one refused naming no row, at none.
 */
static void imports_by_copy(trl_test *test) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    records r = records_of(5U);
    trl_import how = {"t", 2U, false, NULL, NULL};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, trl_imported_rows(done) == 5U && plan.copies == 3U && plan.executes == 0U);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    plan.copy_fail_row = 2U;
    r = records_of(3U);
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text));
    TRL_CHECK(test, strncmp(text, "record 2, line 3: ", 18U) == 0 && trl_imported_rolled_back(done) == 1U);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    static const char *const details[] = {
        NULL, "the table", "row 5 of the copy", "row x of the copy", "row 1 of copy", "row  of the copy",
    };
    for (size_t i = 0U; i < sizeof details / sizeof details[0]; ++i) {
        memset(&plan, 0, sizeof plan);
        plan.copy = true;
        plan.copy_fail = true;
        plan.copy_detail = details[i];
        r = records_of(1U);
        TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strncmp(text, "SQLCODE", 7U) == 0);
        trl_imported_free(done);
    }
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    r = records_of(3U);
    r.fail_at = 3U;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "the input is cut") != NULL);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    r = records_of(2U);
    r.widths[1] = 1U;
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "1 fields, for 2 columns") != NULL);
    trl_imported_free(done);
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    r = records_of(1U);
    r.fields[0][0] = "twenty-one characters";
    TRL_CHECK(test, !imported(&plan, &r, &how, &done, text) && strstr(text, "22001") != NULL);
    trl_imported_free(done);
}

/**
 * Verifies: [CLIENT Importing] [CLIENT Large objects] a field longer than
 * a server sends whole, for a large object column, is sent ahead as a new
 * large object by its locator; a short one whole; such an import goes by
 * INSERT even with `copy` granted.
 */
static void long_fields_go_by_locator(trl_test *test) {
    static char longer[70000];
    memset(longer, 'x', sizeof longer - 1U);
    longer[sizeof longer - 1U] = '\0';
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    plan.lob_columns = true;
    records r = records_of(1U);
    r.fields[0][0] = longer;
    trl_import how = {"t", 0U, false, NULL, NULL};
    trl_imported *done = NULL;
    char text[TEXT_BYTES];
    TRL_CHECK(test, imported(&plan, &r, &how, &done, text) && plan.executes == 1U && plan.copies == 0U);
    trl_imported_free(done);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(batches_are_committed),       TRL_TEST_CASE(refused_rows_are_skipped_or_stop),
        TRL_TEST_CASE(rows_refused_before_sending), TRL_TEST_CASE(failures_stop_the_import),
        TRL_TEST_CASE(imports_refused_before_rows), TRL_TEST_CASE(imports_by_copy),
        TRL_TEST_CASE(long_fields_go_by_locator),   TRL_TEST_CASE(import_arguments_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
