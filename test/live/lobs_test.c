/**
 * @file lobs_test.c
 * @brief Large objects against a real server (RULES.md R-TEST-8): one
 * short enough sent whole, a longer one by its locator, read by ranges and
 * as a stream; a locator freed and held; and large objects written a piece
 * at a time and passed as parameters. Opt-in: TRL_TEST_SERVER names the
 * server, else skipped.
 */
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
/* A character large object of more than a chunk, each character "é", two
   octets. */
#define LONG_CHARACTERS ((size_t)200000U)
#define LONG_OCTETS (2U * LONG_CHARACTERS)
/* The UTF-8 octets of "é". */
#define E_ACUTE_LEAD 0xC3U
#define E_ACUTE_TRAIL 0xA9U

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    if (ran != TRL_OK) {
        char line[PATH_BYTES];
        (void)trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        (void)fprintf(stderr, "%s: %s\n", sql, line);
    }
    trl_result_free(result);
    return ran == TRL_OK;
}

/* Inserts a long character large object written by a writer, its writes
   splitting characters, under `id` into LOBS, by `sql`, whose first two
   `?` are the key and the object. */
static bool insert_by(trl_conn *conn, const char *sql, int id) {
    trl_statement *insert = NULL;
    trl_params *params = NULL;
    trl_lob_writer *writer = NULL;
    bool done = trl_prepare(conn, sql, &insert) == TRL_OK && trl_params_new(&params) == TRL_OK &&
                trl_lob_writer_new(conn, true, &writer) == TRL_OK;
    trl_decimal key = {{(uint64_t)id, 0}, 0U};
    done = done && trl_params_add_decimal(params, key) == TRL_OK;
    static uint8_t piece[1002];
    for (size_t i = 0U; i < sizeof piece; ++i) {
        piece[i] = i % 2U == 0U ? E_ACUTE_LEAD : E_ACUTE_TRAIL;
    }
    size_t written = 0U;
    while (done && written < LONG_OCTETS) {
        /* An odd piece splits a character between two writes. */
        size_t n = LONG_OCTETS - written < sizeof piece - 1U ? LONG_OCTETS - written : sizeof piece - 1U;
        const uint8_t *from = written % 2U == 0U ? piece : piece + 1;
        done = trl_lob_write(writer, from, n) == TRL_OK;
        written += n;
    }
    done = done && trl_lob_writer_finish(writer, params) == TRL_OK;
    trl_result *result = NULL;
    done = done && trl_execute(conn, insert, params, &result) == TRL_OK;
    trl_result_free(result);
    if (!done) {
        char line[PATH_BYTES];
        (void)trl_failure_text(trl_conn_failure(conn), line, sizeof line);
        (void)fprintf(stderr, "insert_long: %s\n", line);
    }
    trl_lob_writer_free(writer);
    trl_params_free(params);
    trl_statement_free(insert);
    return done;
}

/* Inserts a long character large object under `id` into LOBS. */
static bool insert_long(trl_conn *conn, int id) {
    return insert_by(conn, "INSERT INTO LOBS VALUES (?, ?)", id);
}

/**
 * Verifies: [CLIENT Large objects] a large object of at most
 * SERVER.LOB_INLINE_BYTES comes whole in its row, read by ranges of
 * octets or characters and as a stream; it has no locator to free
 * (SQLSTATE 22000) [PROTOCOL Values].
 */
static void a_short_lob_comes_whole(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE LOBS (ID INTEGER, BODY CLOB(1M))"));
    TRL_CHECK(test, run(conn, "INSERT INTO LOBS VALUES (1, 'n\xc3\xa9"
                              "e')"));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM LOBS", &result) == TRL_OK);
    const trl_value *body = trl_result_value(result, 0U, 0U);
    trl_lob_info info;
    TRL_CHECK(test, trl_value_lob(body, &info) == TRL_OK && info.character && !info.located);
    TRL_CHECK(test, info.octets == 4U && info.characters == 3U);
    uint8_t out[16];
    size_t length = sizeof out;
    trl_lob_range characters = {1U, 1U, true};
    TRL_CHECK(test, trl_lob_read(conn, body, characters, out, &length) == TRL_OK && length == 2U);
    TRL_CHECK(test, memcmp(out, "\xc3\xa9", 2U) == 0);
    length = sizeof out;
    trl_lob_range octets = {3U, 10U, false};
    TRL_CHECK(test, trl_lob_read(conn, body, octets, out, &length) == TRL_OK && length == 1U && out[0] == 'e');
    trl_lob_reader *reader = NULL;
    TRL_CHECK(test, trl_lob_reader_new(conn, body, &reader) == TRL_OK);
    size_t total = 0U;
    while (reader != NULL && trl_lob_reader_read(reader, out, 3U, &length) == TRL_OK && length > 0U) {
        total += length;
    }
    TRL_CHECK(test, total == 4U);
    trl_lob_reader_free(reader);
    TRL_CHECK(test, trl_lob_free_locator(conn, body) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "22000") == 0);
    trl_result_free(result);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Large objects] a large object written a piece at a
 * time, a character split between writes, passed as a parameter; longer
 * than SERVER.LOB_INLINE_BYTES it comes as its locator and lengths, read
 * by ranges, at most a chunk, and as a stream [PROTOCOL Lob 'L'].
 */
static void a_long_lob_comes_by_locator(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE LOBS (ID INTEGER, BODY CLOB(1M))"));
    TRL_CHECK(test, insert_long(conn, 1));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM LOBS", &result) == TRL_OK);
    const trl_value *body = trl_result_value(result, 0U, 0U);
    trl_lob_info info;
    TRL_CHECK(test, trl_value_lob(body, &info) == TRL_OK && info.located && info.character);
    TRL_CHECK(test, info.octets == LONG_OCTETS && info.characters == LONG_CHARACTERS);
    static uint8_t out[TRL_LOB_CHUNK_BYTES + 8U];
    size_t length = sizeof out;
    trl_lob_range characters = {LONG_CHARACTERS - 3U, 10U, true};
    TRL_CHECK(test, trl_lob_read(conn, body, characters, out, &length) == TRL_OK && length == 6U);
    TRL_CHECK(test, out[0] == E_ACUTE_LEAD && out[5] == E_ACUTE_TRAIL);
    length = sizeof out;
    trl_lob_range octets = {0U, LONG_OCTETS, false};
    TRL_CHECK(test, trl_lob_read(conn, body, octets, out, &length) == TRL_OK && length == TRL_LOB_CHUNK_BYTES);
    trl_lob_reader *reader = NULL;
    TRL_CHECK(test, trl_lob_reader_new(conn, body, &reader) == TRL_OK);
    size_t total = 0U;
    bool every = true;
    while (reader != NULL && trl_lob_reader_read(reader, out, 4097U, &length) == TRL_OK && length > 0U) {
        for (size_t i = 0U; i < length; ++i) {
            every = every && out[i] == ((total + i) % 2U == 0U ? E_ACUTE_LEAD : E_ACUTE_TRAIL);
        }
        total += length;
    }
    TRL_CHECK(test, total == LONG_OCTETS && every);
    trl_lob_reader_free(reader);
    trl_result_free(result);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Large objects] a locator is valid until its
 * transaction ends, which a statement that sent one keeps open under
 * autocommit too; one held stays valid past the commit, until freed; one
 * freed is no longer valid (SQLSTATE 0F001) [PROTOCOL Lob 'L'].
 */
static void locators_are_freed_and_held(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE LOBS (ID INTEGER, BODY CLOB(1M))"));
    TRL_CHECK(test, insert_long(conn, 1) && insert_long(conn, 2) && run(conn, "COMMIT"));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM LOBS ORDER BY ID", &result) == TRL_OK);
    const trl_value *held = trl_result_value(result, 0U, 0U);
    const trl_value *freed = trl_result_value(result, 1U, 0U);
    TRL_CHECK(test, trl_lob_hold(conn, held) == TRL_OK && trl_lob_free_locator(conn, freed) == TRL_OK);
    uint8_t out[16];
    size_t length = sizeof out;
    trl_lob_range first = {0U, 2U, false};
    TRL_CHECK(test, trl_lob_read(conn, freed, first, out, &length) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "0F001") == 0 && !trl_conn_is_lost(conn));
    TRL_CHECK(test, run(conn, "COMMIT"));
    length = sizeof out;
    TRL_CHECK(test, trl_lob_read(conn, held, first, out, &length) == TRL_OK && length == 2U);
    TRL_CHECK(test, trl_lob_free_locator(conn, held) == TRL_OK);
    length = sizeof out;
    TRL_CHECK(test, trl_lob_read(conn, held, first, out, &length) == TRL_FAILED);
    trl_result_free(result);
    TRL_CHECK(test, run(conn, "ROLLBACK") && trl_conn_set_autocommit(conn, true) == TRL_OK);
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM LOBS WHERE ID = 1", &result) == TRL_OK);
    TRL_CHECK(test, trl_conn_in_transaction(conn));
    length = sizeof out;
    TRL_CHECK(test, trl_lob_read(conn, trl_result_value(result, 0U, 0U), first, out, &length) == TRL_OK);
    TRL_CHECK(test, run(conn, "COMMIT") && !trl_conn_in_transaction(conn));
    trl_result_free(result);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Large objects] a binary large object written and read
 * back; [PROTOCOL Lob 'L'] a character one whose octets are not UTF-8
 * refused by the Append that sends them, SQLSTATE 22021, and one that ends
 * within a character refused where it is used.
 */
static void writers_make_binary_and_refuse_bad_text(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE BLOBS (BODY BLOB(1M))"));
    trl_statement *insert = NULL;
    trl_params *params = NULL;
    trl_lob_writer *writer = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO BLOBS VALUES (?)", &insert) == TRL_OK);
    TRL_CHECK(test, trl_params_new(&params) == TRL_OK && trl_lob_writer_new(conn, false, &writer) == TRL_OK);
    static const uint8_t octets[] = {0U, 1U, 255U, 254U};
    TRL_CHECK(test, trl_lob_write(writer, octets, sizeof octets) == TRL_OK);
    TRL_CHECK(test, trl_lob_writer_finish(writer, params) == TRL_OK);
    TRL_CHECK(test, trl_lob_writer_finish(writer, params) == TRL_INVALID_ARGUMENT);
    trl_lob_writer_free(writer);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_execute(conn, insert, params, &result) == TRL_OK);
    trl_result_free(result);
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM BLOBS", &result) == TRL_OK);
    size_t length = 0U;
    const uint8_t *read = trl_value_bytes(trl_result_value(result, 0U, 0U), &length);
    TRL_CHECK(test, read != NULL && length == sizeof octets && memcmp(read, octets, length) == 0);
    trl_result_free(result);
    TRL_CHECK(test, run(conn, "CREATE TABLE CLOBS (BODY CLOB(1M))"));
    trl_statement *text = NULL;
    TRL_CHECK(test, trl_prepare(conn, "INSERT INTO CLOBS VALUES (?)", &text) == TRL_OK);
    TRL_CHECK(test, trl_lob_writer_new(conn, true, &writer) == TRL_OK);
    static const uint8_t bad[] = {'a', 0xFFU};
    TRL_CHECK(test, trl_lob_write(writer, bad, sizeof bad) == TRL_OK);
    trl_params_clear(params);
    TRL_CHECK(test, trl_lob_writer_finish(writer, params) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "22021") == 0);
    TRL_CHECK(test, trl_params_count(params) == 0U && !trl_conn_is_lost(conn));
    trl_lob_writer_free(writer);
    TRL_CHECK(test, trl_lob_writer_new(conn, true, &writer) == TRL_OK);
    static const uint8_t cut[] = {'a', 0xC3U};
    TRL_CHECK(test, trl_lob_write(writer, cut, sizeof cut) == TRL_OK);
    TRL_CHECK(test, trl_lob_writer_finish(writer, params) == TRL_OK);
    TRL_CHECK(test, trl_execute(conn, text, params, &result) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "22021") == 0 && !trl_conn_is_lost(conn));
    trl_lob_writer_free(writer);
    trl_statement_free(text);
    trl_params_free(params);
    trl_statement_free(insert);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Large objects] a large object passed as a `?` of an
 * INSERT of several rows, beside a row of a short literal, kept whole: a
 * `?` takes its object column's type [PROTOCOL Lob 'L'].
 */
static void a_lob_in_rows_of_an_insert(trl_test *test) {
    trl_conn *conn = live_trusted(NULL);
    TRL_CHECK(test, run(conn, "CREATE TABLE MANY (ID INTEGER, BODY CLOB(1M))"));
    TRL_CHECK(test, insert_by(conn, "INSERT INTO MANY VALUES (?, ?), (2, 'short')", 1));
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn, "SELECT BODY FROM MANY ORDER BY ID", &result) == TRL_OK);
    trl_lob_info info;
    TRL_CHECK(test, trl_value_lob(trl_result_value(result, 0U, 0U), &info) == TRL_OK && info.octets == LONG_OCTETS);
    TRL_CHECK(test, trl_value_lob(trl_result_value(result, 1U, 0U), &info) == TRL_OK && info.octets == 5U);
    trl_result_free(result);
    trl_conn_free(conn);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-lobs"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    (void)snprintf(dir, sizeof dir, "%s/live-lobs", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, false, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    trl_conn *owner = live_trusted(NULL);
    bool ready = owner != NULL && run(owner, "CREATE SCHEMA AUTHORIZATION ANN") && run(owner, "COMMIT");
    trl_conn_free(owner);
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_short_lob_comes_whole),     TRL_TEST_CASE(a_long_lob_comes_by_locator),
        TRL_TEST_CASE(locators_are_freed_and_held), TRL_TEST_CASE(writers_make_binary_and_refuse_bad_text),
        TRL_TEST_CASE(a_lob_in_rows_of_an_insert),
    };
    int result = ready ? trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases)) : EXIT_FAILURE;
    server_stop(&live_server);
    return result;
}
