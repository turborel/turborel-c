/**
 * @file delimited_test.c
 * @brief Delimited text read a record at a time, as turborel-client's
 * own tests of its delimited source read it: CSV and tab-separated text,
 * a header, the text for null, positions, bounds and failures.
 */
#include "codec/buffer.h"
#include "codec/frame.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "data/params.h"
#include "format.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Room for the records read, written out. */
#define SHOWN_BYTES 512U

/* An input: its bytes, how many given at a time, and where it is; or one
   that fails, or one of a byte repeated without end. */
typedef struct input {
    const uint8_t *bytes;
    size_t length;
    size_t piece;
    size_t at;
    bool fails;
    uint8_t endless;
    size_t endless_for;
} input;

static bool read_input(void *context, uint8_t *into, size_t capacity, size_t *length) {
    input *in = context;
    if (in->fails) {
        return false;
    }
    size_t n = in->length - in->at;
    n = n < in->piece ? n : in->piece;
    n = n < capacity ? n : capacity;
    if (in->endless != 0U && n == 0U) {
        n = capacity < in->piece ? capacity : in->piece;
        memset(into, in->endless, n);
        if (in->endless_for > 0U) {
            in->endless_for = in->endless_for > n ? in->endless_for - n : 0U;
            in->endless = in->endless_for == 0U ? (uint8_t)'\n' : in->endless;
        }
        *length = n;
        return true;
    }
    memcpy(into, in->bytes + in->at, n);
    in->at += n;
    *length = n;
    return true;
}

/* An input of `text`, given `piece` bytes at a time. */
static input input_of(const char *text, size_t piece) {
    input in = {(const uint8_t *)text, strlen(text), piece, 0U, false, 0U, 0U};
    return in;
}

/* Writes a record's fields after `out`: each in brackets, a null as ~. */
static void show_record(const trl_params *values, char *out, size_t capacity) {
    const trl_value *all = trl_params_values(values);
    for (size_t i = 0U; i < trl_params_count(values); ++i) {
        size_t used = strlen(out);
        if (all[i].tag == TRL_TAG_NULL) {
            trl_format(out + used, capacity - used, "~");
        } else {
            trl_format(out + used, capacity - used, "[%.*s]", (int)all[i].as.bytes.length,
                       (const char *)all[i].as.bytes.bytes);
        }
    }
}

/* Reads every record of `in` as `dialect` says, written out, records
   apart by /, into `out`; the error's message, position and text, if one
   stopped it, into `error`. Whether all was read. */
static bool read_all(input *in, const trl_dialect *dialect, char *out, trl_source_error *error) {
    out[0] = '\0';
    trl_delimited *delimited = NULL;
    if (trl_delimited_new(read_input, in, dialect, &delimited) != TRL_OK) {
        return false;
    }
    trl_source source = trl_delimited_source(delimited);
    trl_params *values = NULL;
    (void)trl_params_new(&values);
    trl_headings headings = {NULL, 0U};
    bool all = source.headings(source.context, &headings, error) == TRL_READ_OK;
    trl_position at = {0U, 0U};
    trl_source_read read = TRL_READ_OK;
    for (size_t n = 0U; all; ++n) {
        trl_params_clear(values);
        read = source.record(source.context, values, &at, error);
        if (read != TRL_READ_OK) {
            break;
        }
        size_t used = strlen(out);
        trl_format(out + used, SHOWN_BYTES - used, n == 0U ? "" : "/");
        show_record(values, out, SHOWN_BYTES);
    }
    all = all && read == TRL_READ_END;
    /* The message is the source's, which is freed: kept here. */
    static char kept[SHOWN_BYTES];
    trl_format(kept, sizeof kept, "%s", error->message == NULL ? "" : error->message);
    error->message = kept;
    trl_params_free(values);
    trl_delimited_free(delimited);
    return all;
}

/* Whether `text` read as `dialect` says, a piece of 1, 3 and every byte at
   a time, gives `expected`. */
static bool reads_as(const char *text, trl_dialect dialect, const char *expected) {
    static const size_t pieces[] = {1U, 3U, 65536U};
    bool same = true;
    for (size_t i = 0U; i < sizeof pieces / sizeof pieces[0]; ++i) {
        char shown[SHOWN_BYTES];
        trl_source_error error = {{0U, 0U}, NULL};
        input in = input_of(text, pieces[i]);
        bool read = read_all(&in, &dialect, shown, &error);
        if (!read || strcmp(shown, expected) != 0) {
            (void)fprintf(stderr, "%s: %s%s\n", text, shown, read ? "" : " (failed)");
            same = false;
        }
    }
    return same;
}

/* A position: a record and the line it began on. */
static trl_position place(uint64_t record, uint64_t line) {
    trl_position where = {record, line};
    return where;
}

/* Whether `text`, of `length` bytes, read as `dialect` says, fails
   `where`, saying `message`. */
static bool fails_as(const char *text, size_t length, trl_dialect dialect, trl_position where, const char *message) {
    char shown[SHOWN_BYTES];
    trl_source_error error = {{0U, 0U}, NULL};
    input in = {(const uint8_t *)text, length, 2U, 0U, false, 0U, 0U};
    bool read = read_all(&in, &dialect, shown, &error);
    bool same = !read && error.message != NULL && strcmp(error.message, message) == 0 &&
                error.position.record == where.record && error.position.line == where.line;
    if (!same) {
        (void)fprintf(stderr, "%s: %s at %llu, %llu\n", text, error.message == NULL ? "(none)" : error.message,
                      (unsigned long long)error.position.record, (unsigned long long)error.position.line);
    }
    return same;
}

/**
 * Verifies: [CLIENT Importing] CSV's records end at CR LF or LF, the
 * last's optional [RFC 4180 §2 rules 1, 2, 4-7]; fields split at commas,
 * spaces kept; a field in quotes holds commas, line breaks and quotes
 * written twice; an empty field is null, one of empty quotes the empty
 * string; an empty line a record of one null field.
 */
static void csv_records_and_fields(trl_test *test) {
    trl_dialect csv = trl_dialect_csv();
    TRL_CHECK(test, reads_as("a,b\r\nc,d\n", csv, "[a][b]/[c][d]"));
    TRL_CHECK(test, reads_as("a,b\nc,d", csv, "[a][b]/[c][d]"));
    TRL_CHECK(test, reads_as(" a , b ", csv, "[ a ][ b ]"));
    TRL_CHECK(test, reads_as("\"a,b\",\"say \"\"hi\"\"\"", csv, "[a,b][say \"hi\"]"));
    TRL_CHECK(test, reads_as("\"one\r\ntwo\nthree\",x\n", csv, "[one\r\ntwo\nthree][x]"));
    TRL_CHECK(test, reads_as(",\"\",", csv, "~[]~"));
    TRL_CHECK(test, reads_as("a\n\nb\n", csv, "[a]/~/[b]"));
    TRL_CHECK(test, reads_as("\"\"\"\"", csv, "[\"]"));
    TRL_CHECK(test, reads_as("", csv, ""));
    TRL_CHECK(test, reads_as("\xc3\xa9,\xf0\x9f\x98\x80", csv, "[\xc3\xa9][\xf0\x9f\x98\x80]"));
}

/**
 * Verifies: [CLIENT Importing] the text for null is null only not in
 * quotes; an empty field is then the empty string. A byte order mark
 * beginning the input is passed over, and nowhere else.
 */
static void csv_null_text_and_byte_order_mark(trl_test *test) {
    trl_dialect null = trl_dialect_csv();
    null.null = "NULL";
    TRL_CHECK(test, reads_as("NULL,\"NULL\",,null", null, "~[NULL][][null]"));
    TRL_CHECK(test, reads_as("\xef\xbb\xbf"
                             "a,b\n\xef\xbb\xbf"
                             "c",
                             trl_dialect_csv(),
                             "[a][b]/[\xef\xbb\xbf"
                             "c]"));
}

/**
 * Verifies: [CLIENT Importing] a header names the fields, read once;
 * records are numbered from the first after it, and each says the line it
 * began on, past the line breaks of quoted fields; a header missing is
 * refused; none is read unless asked for.
 */
static void csv_header_and_positions(trl_test *test) {
    static const char text[] = "ID,\"FULL NAME\"\n1,\"Ada\nLovelace\"\n2,Grace\n";
    trl_dialect header = trl_dialect_csv();
    header.header = true;
    input in = input_of(text, 5U);
    trl_delimited *delimited = NULL;
    TRL_CHECK(test, trl_delimited_new(read_input, &in, &header, &delimited) == TRL_OK);
    trl_source source = trl_delimited_source(delimited);
    trl_headings headings = {NULL, 0U};
    trl_source_error error = {{0U, 0U}, NULL};
    TRL_CHECK(test, source.headings(source.context, &headings, &error) == TRL_READ_OK && headings.count == 2U);
    TRL_CHECK(test, strcmp(headings.names[0], "ID") == 0 && strcmp(headings.names[1], "FULL NAME") == 0);
    trl_params *values = NULL;
    (void)trl_params_new(&values);
    trl_position at = {0U, 0U};
    TRL_CHECK(test, source.record(source.context, values, &at, &error) == TRL_READ_OK);
    TRL_CHECK(test, at.record == 1U && at.line == 2U);
    trl_params_clear(values);
    TRL_CHECK(test, source.record(source.context, values, &at, &error) == TRL_READ_OK);
    TRL_CHECK(test, at.record == 2U && at.line == 4U);
    TRL_CHECK(test, source.record(source.context, values, &at, &error) == TRL_READ_END);
    TRL_CHECK(test, source.record(source.context, values, &at, &error) == TRL_READ_END);
    trl_params_free(values);
    trl_delimited_free(delimited);
    TRL_CHECK(test, reads_as(text, header, "[1][Ada\nLovelace]/[2][Grace]"));
    TRL_CHECK(test, fails_as("", 0U, header, place(0U, 1U), "[DESIGN] there is no header"));
    char shown[SHOWN_BYTES];
    input plain = input_of("a", 1U);
    trl_dialect csv = trl_dialect_csv();
    TRL_CHECK(test, read_all(&plain, &csv, shown, &error) && strcmp(shown, "[a]") == 0);
}

/**
 * Verifies: [CLIENT Importing] a quote in a field not quoted, anything but
 * the delimiter after a closing quote, a quote left open at the end of
 * the input, and a field not UTF-8 are refused, saying the record, the
 * line it began on, and the field [RFC 4180 §2 rules 5, 7].
 */
static void csv_errors_say_where(trl_test *test) {
    trl_dialect csv = trl_dialect_csv();
    TRL_CHECK(test, fails_as("a\nb,c\"d\n", 8U, csv, place(2U, 2U),
                             "[RFC 4180 \xc2\xa7"
                             "2 rule 5] field 2: a quote in a field not enclosed in quotes"));
    TRL_CHECK(test, fails_as("\"a\"b,c", 6U, csv, place(1U, 1U),
                             "[RFC 4180 \xc2\xa7"
                             "2 rule 5] field 1: a quoted field's closing quote is followed by more"));
    TRL_CHECK(test, fails_as("a\nb\n\"c\nd\ne\n", 11U, csv, place(3U, 3U),
                             "[RFC 4180 \xc2\xa7"
                             "2 rule 5] a quoted field is not closed"));
    TRL_CHECK(test, fails_as("\"c", 2U, csv, place(1U, 1U),
                             "[RFC 4180 \xc2\xa7"
                             "2 rule 5] a quoted field is not closed"));
    TRL_CHECK(test, fails_as("ok,\xff\xfe", 5U, csv, place(1U, 1U), "[DESIGN] field 2 is not UTF-8"));
    TRL_CHECK(test, fails_as("ok\t\xff", 4U, trl_dialect_tsv(), place(1U, 1U), "[DESIGN] field 2 is not UTF-8"));
    input in = input_of("\"a\"b\nc\n", 64U);
    trl_delimited *delimited = NULL;
    TRL_CHECK(test, trl_delimited_new(read_input, &in, &csv, &delimited) == TRL_OK);
    trl_source source = trl_delimited_source(delimited);
    trl_params *values = NULL;
    (void)trl_params_new(&values);
    trl_position at = {0U, 0U};
    trl_source_error error = {{0U, 0U}, NULL};
    TRL_CHECK(test, source.record(source.context, values, &at, &error) == TRL_READ_FAILED);
    TRL_CHECK(test, source.record(source.context, values, &at, &error) == TRL_READ_END);
    trl_params_free(values);
    trl_delimited_free(delimited);
}

/**
 * Verifies: [CLIENT Importing] a record longer than the bound is refused
 * once the bound is read, so an endless input, or a quote never closed,
 * is read in bounded memory; a record of the bound is read, its line
 * break counted. A reader's failure is told.
 */
static void records_are_bounded(trl_test *test) {
    trl_dialect bounded = trl_dialect_csv();
    bounded.max_record = 1024U;
    char shown[SHOWN_BYTES];
    trl_source_error error = {{0U, 0U}, NULL};
    input endless = {(const uint8_t *)"", 0U, 100U, 0U, false, (uint8_t)'a', 0U};
    TRL_CHECK(test, !read_all(&endless, &bounded, shown, &error));
    TRL_CHECK(test, strcmp(error.message, "[DESIGN] the record is longer than 1024 bytes") == 0);
    bounded.max_record = 4096U;
    input open = {(const uint8_t *)"", 0U, 100U, 0U, false, (uint8_t)'"', 10000U};
    TRL_CHECK(test, !read_all(&open, &bounded, shown, &error));
    TRL_CHECK(test, strcmp(error.message, "[DESIGN] the record is longer than 4096 bytes") == 0);
    bounded.max_record = 5U;
    TRL_CHECK(test, reads_as("abcd\n", bounded, "[abcd]"));
    TRL_CHECK(test, fails_as("abcde\n", 6U, bounded, place(1U, 1U), "[DESIGN] the record is longer than 5 bytes"));
    TRL_CHECK(test, trl_dialect_csv().max_record == (size_t)TRL_FRAME_MAX_BODY);
    input failing = {(const uint8_t *)"", 0U, 1U, 0U, true, 0U, 0U};
    trl_dialect csv = trl_dialect_csv();
    TRL_CHECK(test, !read_all(&failing, &csv, shown, &error));
    TRL_CHECK(test, strcmp(error.message, "[DESIGN] cannot read the input") == 0);
}

/**
 * Verifies: [CLIENT Importing] tab-separated text splits at tabs, with no
 * quoting, its records at line breaks; an empty field is null. A
 * delimiter is one ASCII character, not a quote, CR or LF.
 */
static void tab_separated_records_and_delimiters(trl_test *test) {
    trl_dialect tsv = trl_dialect_tsv();
    TRL_CHECK(test, reads_as("a\tb\r\n\"c\t\"d\"\n", tsv, "[a][b]/[\"c][\"d\"]"));
    TRL_CHECK(test, reads_as("a\t\tb,c", tsv, "[a]~[b,c]"));
    tsv.null = "\\N";
    TRL_CHECK(test, reads_as("a\t\\N", tsv, "[a]~"));
    trl_dialect semicolon = trl_dialect_csv();
    semicolon.delimiter = ';';
    TRL_CHECK(test, reads_as("a;\"b;c\";d,e", semicolon, "[a][b;c][d,e]"));
    trl_dialect pipe = trl_dialect_tsv();
    pipe.delimiter = '|';
    TRL_CHECK(test, reads_as("a|b\tc", pipe, "[a][b\tc]"));
    static const char wrong[] = {'"', '\n', '\r', '\0', '\xC3'};
    input in = input_of("", 1U);
    for (size_t i = 0U; i < sizeof wrong; ++i) {
        trl_dialect refused = trl_dialect_csv();
        refused.delimiter = wrong[i];
        trl_delimited *delimited = NULL;
        TRL_CHECK(test, trl_delimited_new(read_input, &in, &refused, &delimited) == TRL_INVALID_ARGUMENT);
    }
    trl_delimited *none = NULL;
    TRL_CHECK(test, trl_delimited_new(NULL, &in, &tsv, &none) == TRL_INVALID_ARGUMENT);
    trl_delimited_free(NULL);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(csv_records_and_fields),   TRL_TEST_CASE(csv_null_text_and_byte_order_mark),
        TRL_TEST_CASE(csv_header_and_positions), TRL_TEST_CASE(csv_errors_say_where),
        TRL_TEST_CASE(records_are_bounded),      TRL_TEST_CASE(tab_separated_records_and_delimiters),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
