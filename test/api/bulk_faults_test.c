/**
 * @file bulk_faults_test.c
 * @brief Memory not had in bulk work (RULES.md R-MEM-2): a copy, an
 * import by INSERT and by copy, an export as CSV and as native frames
 * read back, delimited text read, a large object written, and values and
 * types written as text, each allocation failing in turn, never crashing
 * or leaking. Built with TRL_FAULTS alone.
 */
#include "bulk_serving.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "faults.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Room for an export's bytes. */
#define EXPORT_BYTES 4096U

/* Bytes written, and read back. */
typedef struct bytes_kept {
    uint8_t bytes[EXPORT_BYTES];
    size_t length;
    size_t read_at;
} bytes_kept;

static bool keep(void *context, const uint8_t *bytes, size_t length) {
    bytes_kept *k = context;
    if (k->length + length > sizeof k->bytes) {
        return false;
    }
    memcpy(k->bytes + k->length, bytes, length);
    k->length += length;
    return true;
}

static size_t give_back(void *context, uint8_t *into, size_t capacity) {
    bytes_kept *k = context;
    size_t n = k->length - k->read_at < capacity ? k->length - k->read_at : capacity;
    memcpy(into, k->bytes + k->read_at, n);
    k->read_at += n;
    return n;
}

/* CSV text read whole, a piece at a time. */
typedef struct text_in {
    const char *text;
    size_t at;
} text_in;

static bool read_text(void *context, uint8_t *into, size_t capacity, size_t *length) {
    text_in *in = context;
    size_t left = strlen(in->text) - in->at;
    size_t n = left < capacity ? left : capacity;
    memcpy(into, in->text + in->at, n);
    in->at += n;
    *length = n;
    return true;
}

/* An import of delimited text into T; whether it came to what is allowed. */
static bool import_of(trl_conn *conn, const char *text, bool header, bool skip) {
    text_in in = {text, 0U};
    trl_dialect dialect = trl_dialect_csv();
    dialect.header = header;
    trl_delimited *delimited = NULL;
    trl_status made = trl_delimited_new(read_text, &in, &dialect, &delimited);
    if (made != TRL_OK) {
        return made == TRL_NO_MEMORY;
    }
    trl_source source = trl_delimited_source(delimited);
    trl_import how = {"t", 2U, skip, NULL, NULL};
    trl_imported *done = NULL;
    trl_status status = trl_import_rows(conn, &source, &how, &done);
    char text_out[256];
    (void)trl_imported_text(done, text_out, sizeof text_out);
    trl_imported_free(done);
    trl_delimited_free(delimited);
    return status == TRL_OK || status == TRL_FAILED || status == TRL_NO_MEMORY;
}

/* A copy of two rows; whether it came to what is allowed. */
static bool copied(trl_conn *conn) {
    static const char *const columns[] = {"A", "B"};
    trl_copy_target target = {"T", columns, 2U};
    trl_params *row = NULL;
    if (trl_params_new(&row) != TRL_OK) {
        return true;
    }
    (void)trl_params_add_text(row, "a", 1U);
    (void)trl_params_add_text(row, "b", 1U);
    trl_copy *copy = NULL;
    trl_status status = trl_copy_in(conn, &target, &copy);
    bool fine = allowed(status, trl_conn_failure(conn));
    for (int i = 0; status == TRL_OK && i < 2; ++i) {
        status = trl_copy_send(copy, row);
        fine = fine && allowed(status, trl_conn_failure(conn));
    }
    if (status == TRL_OK) {
        uint64_t rows = 0U;
        fine = fine && allowed(trl_copy_finish(copy, &rows), trl_conn_failure(conn));
    }
    trl_copy_free(copy);
    trl_params_free(row);
    return fine;
}

/* An export as CSV and as native frames, read back. */
static bool exported(trl_conn *conn) {
    bytes_kept csv;
    memset(&csv, 0, sizeof csv);
    trl_export_sink as_csv = {TRL_EXPORT_CSV, keep, &csv};
    bool fine = allowed(trl_export(conn, "Q", &as_csv, NULL), trl_conn_failure(conn));
    bytes_kept native;
    memset(&native, 0, sizeof native);
    trl_export_sink as_native = {TRL_EXPORT_NATIVE, keep, &native};
    trl_status status = trl_export(conn, "Q", &as_native, NULL);
    fine = fine && allowed(status, trl_conn_failure(conn));
    if (status == TRL_OK) {
        trl_result *result = NULL;
        trl_status read = trl_read_native(give_back, &native, &result);
        fine = fine && allowed(read, trl_result_failure(result));
        trl_result_free(result);
    }
    return fine;
}

/* A large object written, a piece at a time, as a parameter. */
static bool written(trl_conn *conn) {
    trl_lob_writer *writer = NULL;
    trl_params *params = NULL;
    if (trl_params_new(&params) != TRL_OK) {
        return true;
    }
    trl_status status = trl_lob_writer_new(conn, true, &writer);
    bool fine = allowed(status, trl_conn_failure(conn));
    static const uint8_t text[] = "text";
    if (status == TRL_OK) {
        fine = fine && allowed(trl_lob_write(writer, text, 4U), trl_conn_failure(conn));
        fine = fine && allowed(trl_lob_writer_finish(writer, params), trl_conn_failure(conn));
    }
    trl_lob_writer_free(writer);
    trl_params_free(params);
    return fine;
}

/* Values of composite kinds, and a type, written as text. */
static bool as_text(void) {
    trl_value parts[2];
    memset(parts, 0, sizeof parts);
    parts[0].tag = TRL_TAG_VARYING;
    parts[0].as.bytes.bytes = (const uint8_t *)"it's";
    parts[0].as.bytes.length = 4U;
    parts[1].tag = TRL_TAG_INTERVAL;
    parts[1].as.interval.qualifier.start = 2U;
    parts[1].as.interval.qualifier.end = 5U;
    trl_value row;
    memset(&row, 0, sizeof row);
    row.tag = TRL_TAG_ROW;
    row.as.list.items = parts;
    row.as.list.count = 2U;
    char text[128];
    trl_status shown = trl_value_to_text(&row, text, sizeof text, NULL);
    trl_data_type type;
    memset(&type, 0, sizeof type);
    type.tag = TRL_TAG_INTERVAL;
    trl_status typed = trl_type_to_text(&type, text, sizeof text, NULL);
    return (shown == TRL_OK || shown == TRL_NO_MEMORY) && (typed == TRL_OK || typed == TRL_NO_MEMORY);
}

/* A session of bulk work over a server granting `copy`. */
static bool bulk_session(void) {
    bulk_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.copy = true;
    script s;
    trl_conn *conn = bulk_connect(&s, &plan);
    bool fine = true;
    if (conn != NULL && !trl_conn_is_lost(conn)) {
        fine = copied(conn) && exported(conn) && written(conn);
        fine = fine && import_of(conn, "a,b\nc,d\ne,f\n", false, false);
        fine = fine && import_of(conn, "X,Y\na,b\n\"c\",d\n", true, true);
    }
    trl_conn_free(conn);
    script_free(&s);
    return fine && as_text();
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a copy, an import by
 * INSERT and by copy, an export as CSV and as native frames read back,
 * a large object written, and values and types written as text failing
 * in turn fails the call as memory not had, or the connection lost, and
 * leaks nothing.
 */
static void bulk_work_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, every_fault(bulk_session));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(bulk_work_survives_each_fault),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
