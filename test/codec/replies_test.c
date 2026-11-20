/**
 * @file replies_test.c
 * @brief Every reply decoded, and malformed ones failing the connection
 * [PROTOCOL Replies].
 */
#include "codec/buffer.h"
#include "codec/int128.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "fixture.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdint.h>
#include <string.h>

/* Appends a failure's fields, SQLCODE to active, with no diagnostics. */
static void put_failure(trl_buffer *b, int32_t sqlcode, const char *sqlstate, const char *message) {
    trl_put_u32(b, (uint32_t)sqlcode);
    trl_put_bytes(b, text_of(sqlstate));
    trl_put_bytes(b, text_of(message));
    trl_put_u8(b, 1U);
    trl_put_bytes(b, text_of("X3.135-1986 §5.7 SR3b"));
    trl_put_u8(b, 1U);
    trl_put_u64(b, 7U);
    trl_put_u8(b, 0U);
}

/**
 * Verifies: [PROTOCOL A worked example] the Rows reply of the exact number
 * 1, units 1 and scale 0, is read from the bytes the spec prints.
 */
static void the_worked_rows(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t body[] = {
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    trl_reply reply;
    TRL_CHECK(test, sizeof body == 29U && decode_bytes(&f, 'B', body, sizeof body, &reply));
    TRL_CHECK(test, reply.as.rows.count == 1U && reply.as.rows.rows[0].count == 1U);
    const trl_value *one = &reply.as.rows.rows[0].values[0];
    trl_int128 units_one = {1U, 0};
    TRL_CHECK(test, one->tag == TRL_TAG_EXACT && trl_int128_equal(one->as.exact.units, units_one));
    TRL_CHECK(test, one->as.exact.scale == 0U);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL A worked example] the Complete with an empty tail is
 * its first form, and with row-counts it ends with tag 6 and the row
 * count, then tag 18 and the server's three times.
 */
static void the_worked_complete(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t body[] = {0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00};
    trl_reply reply;
    TRL_CHECK(test, sizeof body == 14U && decode_bytes(&f, 'K', body, sizeof body, &reply));
    TRL_CHECK(test, reply.as.complete.sqlcode == 0 && slice_is(reply.as.complete.sqlstate, "00000"));
    TRL_CHECK(test, !reply.as.complete.active && !reply.as.complete.has_rows && !reply.as.complete.has_time);
    trl_put_raw(&f.buffer, body, sizeof body);
    const uint8_t rows[] = {0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    trl_put_raw(&f.buffer, rows, sizeof rows);
    trl_put_u8(&f.buffer, 18U);
    trl_put_u64(&f.buffer, 180U);
    trl_put_u64(&f.buffer, 4U);
    trl_put_u64(&f.buffer, 0U);
    TRL_CHECK(test, decode_buffer(&f, 'K', &reply) && reply.as.complete.has_rows && reply.as.complete.rows == 1U);
    TRL_CHECK(test, reply.as.complete.has_time && reply.as.complete.time[TRL_TIME_TOTAL] == 180U);
    TRL_CHECK(test, reply.as.complete.time[TRL_TIME_QUEUED] == 4U && reply.as.complete.time[TRL_TIME_LOCKS] == 0U);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Complete 'K'] a Complete's tail is tags 4 position, 5
 * more, 6 rows, 16 generated and 17 conditions, each condition a failure as
 * bytes whose tail ends where they do.
 */
static void complete_tail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u32(&f.buffer, 100U);
    trl_put_bytes(&f.buffer, text_of("02000"));
    trl_put_u8(&f.buffer, 1U);
    trl_put_u8(&f.buffer, 4U);
    trl_put_u64(&f.buffer, 4711U);
    trl_put_u8(&f.buffer, 5U);
    trl_put_u8(&f.buffer, 16U);
    trl_put_u32(&f.buffer, 2U);
    trl_put_bytes(&f.buffer, text_of("3"));
    trl_put_bytes(&f.buffer, text_of("4"));
    trl_put_u8(&f.buffer, 17U);
    trl_put_u32(&f.buffer, 1U);
    trl_buffer condition;
    trl_buffer_init(&condition);
    put_failure(&condition, 0, "01004", "string data, right truncation");
    trl_put_u8(&condition, 7U);
    trl_put_bytes(&condition, text_of("WARNING"));
    trl_put_bytes(&f.buffer, bytes_of(condition.data, condition.length));
    trl_buffer_free(&condition);
    trl_reply reply;
    TRL_CHECK(test, decode_buffer(&f, 'K', &reply));
    const trl_completed *c = &reply.as.complete;
    TRL_CHECK(test, c->sqlcode == 100 && c->active && c->has_position && c->position == 4711U && c->more);
    TRL_CHECK(test, c->generated.count == 2U && slice_is(c->generated.items[1], "4"));
    TRL_CHECK(test, c->condition_count == 1U && slice_is(c->conditions[0].sqlstate, "01004"));
    TRL_CHECK(test, c->conditions[0].diagnostics.told[0] && slice_is(c->conditions[0].diagnostics.text[0], "WARNING"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Error 'E'] an Error is a failure: SQLCODE i32 as
 * u32, SQLSTATE, message, the rule cited, the position, the active flag,
 * then diagnostics, tags 7 to 15, in order.
 */
static void error_carries_its_failure(trl_test *test) {
    fixture f;
    fixture_init(&f);
    put_failure(&f.buffer, -103, "2A000", "column K is ambiguous");
    trl_put_u8(&f.buffer, 7U);
    trl_put_bytes(&f.buffer, text_of("ERROR"));
    trl_put_u8(&f.buffer, 12U);
    trl_put_bytes(&f.buffer, text_of("STAFF"));
    trl_put_u8(&f.buffer, 15U);
    trl_put_bytes(&f.buffer, text_of("STAFF_PK"));
    trl_reply reply;
    TRL_CHECK(test, decode_buffer(&f, 'E', &reply));
    const trl_wire_failure *e = &reply.as.error;
    TRL_CHECK(test,
              e->sqlcode == -103 && slice_is(e->sqlstate, "2A000") && slice_is(e->message, "column K is ambiguous"));
    TRL_CHECK(test, e->cited && slice_is(e->rule, "X3.135-1986 §5.7 SR3b") && e->placed && e->position == 7U);
    TRL_CHECK(test, !e->active && e->diagnostics.told[0] && e->diagnostics.told[5] && e->diagnostics.told[8]);
    TRL_CHECK(test, !e->diagnostics.told[1] && slice_is(e->diagnostics.text[8], "STAFF_PK"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Ready 'R'] Ready is authorization, a 16-byte key,
 * the capabilities and the server; [PROTOCOL Challenge 'C'] Challenge is
 * bytes, read as the server's because of the direction it travels.
 */
static void ready_and_challenge(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_bytes(&f.buffer, text_of("ANN"));
    uint8_t key[16];
    memset(key, 9, sizeof key);
    trl_put_raw(&f.buffer, key, sizeof key);
    trl_put_u32(&f.buffer, 1U);
    trl_put_bytes(&f.buffer, text_of("ping"));
    trl_put_bytes(&f.buffer, text_of("turborel 0.0.0 (alpha)"));
    trl_reply reply;
    TRL_CHECK(test, decode_buffer(&f, 'R', &reply) && slice_is(reply.as.ready.authorization, "ANN"));
    TRL_CHECK(test, reply.as.ready.key.length == 16U && reply.as.ready.capabilities.count == 1U);
    TRL_CHECK(test, slice_is(reply.as.ready.server, "turborel 0.0.0 (alpha)"));
    const uint8_t short_key[] = {0, 0, 0, 0, 1};
    TRL_CHECK(test, !decode_bytes(&f, 'R', short_key, sizeof short_key, &reply) && failed_connection(&f));
    fixture_reset(&f);
    trl_put_bytes(&f.buffer, text_of("r=abc"));
    TRL_CHECK(test, decode_buffer(&f, 'C', &reply) && reply.kind == TRL_KIND_CHALLENGE);
    TRL_CHECK(test, slice_is(reply.as.challenge, "r=abc"));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Prepared 'P'] Prepared is the statement, its
 * parameters, each varying flag and type, and its columns; [PROTOCOL
 * Description 'D'] Description its columns, each named flag, name, type
 * and not null flag.
 */
static void prepared_and_description(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u32(&f.buffer, 1U);
    trl_put_u32(&f.buffer, 1U);
    trl_put_u8(&f.buffer, 1U);
    trl_put_u8(&f.buffer, TRL_TAG_VARYING);
    trl_put_u32(&f.buffer, 32767U);
    trl_put_u8(&f.buffer, 0U);
    trl_put_u32(&f.buffer, 1U);
    trl_put_u8(&f.buffer, 1U);
    trl_put_bytes(&f.buffer, text_of("ID"));
    trl_put_u8(&f.buffer, TRL_TAG_UUID);
    trl_put_u8(&f.buffer, 1U);
    trl_reply reply;
    TRL_CHECK(test, decode_buffer(&f, 'P', &reply) && reply.as.prepared.statement == 1U);
    TRL_CHECK(test, reply.as.prepared.parameter_count == 1U && reply.as.prepared.parameters[0].varying);
    TRL_CHECK(test, reply.as.prepared.columns[0].named && slice_is(reply.as.prepared.columns[0].name, "ID"));
    TRL_CHECK(test, reply.as.prepared.columns[0].type.tag == TRL_TAG_UUID && reply.as.prepared.columns[0].not_null);
    const uint8_t description[] = {1, 0, 0, 0, 0, TRL_TAG_CHARACTER, 1, 0, 0, 0, 0, 0};
    TRL_CHECK(test, decode_bytes(&f, 'D', description, sizeof description, &reply));
    TRL_CHECK(test, reply.as.description.count == 1U && !reply.as.description.columns[0].named);
    const uint8_t flag_of_two[] = {1, 0, 0, 0, 2, TRL_TAG_CHARACTER, 1, 0, 0, 0, 0, 0};
    TRL_CHECK(test, !decode_bytes(&f, 'D', flag_of_two, sizeof flag_of_two, &reply) && failed_connection(&f));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values] a column's type is not its values' tag: a
 * CHARACTER VARYING column's values come with tag 1.
 */
static void a_value_tag_is_not_its_column_type(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t rows[] = {1, 0, 0, 0, 1, 0, 0, 0, TRL_TAG_CHARACTER, 2, 0, 0, 0, 'h', 'i'};
    trl_reply reply;
    TRL_CHECK(test, decode_bytes(&f, 'B', rows, sizeof rows, &reply));
    TRL_CHECK(test, reply.as.rows.rows[0].values[0].tag == TRL_TAG_CHARACTER);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Notice 'N'] Notice is SQLSTATE, message and
 * diagnostics; [PROTOCOL Status 'T'] Status a name and a value, empty for
 * a null; [PROTOCOL Lob 'L'] a Lob reply data or a locator;
 * [PROTOCOL Suspended 'S'] Suspended and [PROTOCOL Pong 'G'] Pong nothing.
 */
static void later_replies(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_bytes(&f.buffer, text_of("01T01"));
    trl_put_bytes(&f.buffer, text_of("Reset rolled back the transaction that was active"));
    trl_put_u8(&f.buffer, 7U);
    trl_put_bytes(&f.buffer, text_of("NOTICE"));
    trl_reply reply;
    TRL_CHECK(test, decode_buffer(&f, 'N', &reply) && slice_is(reply.as.notice.sqlstate, "01T01"));
    TRL_CHECK(test, slice_is(reply.as.notice.diagnostics.text[0], "NOTICE"));
    fixture_reset(&f);
    trl_put_bytes(&f.buffer, text_of("SCHEMA"));
    trl_put_bytes(&f.buffer, text_of(""));
    TRL_CHECK(test, decode_buffer(&f, 'T', &reply) && slice_is(reply.as.status.value, ""));
    const uint8_t data[] = {0, 2, 0, 0, 0, 'a', 'b'};
    const uint8_t locator[] = {1, 9, 0, 0, 0};
    const uint8_t neither[] = {2, 9, 0, 0, 0};
    TRL_CHECK(test, decode_bytes(&f, 'L', data, sizeof data, &reply) && slice_is(reply.as.lob.data, "ab"));
    TRL_CHECK(test, decode_bytes(&f, 'L', locator, sizeof locator, &reply) && reply.as.lob.locator == 9U);
    TRL_CHECK(test, !decode_bytes(&f, 'L', neither, sizeof neither, &reply) && failed_connection(&f));
    fixture_reset(&f);
    TRL_CHECK(test, decode_bytes(&f, 'S', NULL, 0U, &reply) && decode_bytes(&f, 'G', NULL, 0U, &reply));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Errors and notices] a malformed message fails the
 * connection: a kind no reply has, a request's kind, a body cut short or
 * with a byte after its end, a tag of no value.
 */
static void malformed_replies_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_reply reply;
    const uint8_t kinds[] = {'Q', 'H', 0U, 'z'};
    for (size_t i = 0U; i < TRL_TEST_COUNT(kinds); ++i) {
        fixture_reset(&f);
        TRL_CHECK(test, !decode_bytes(&f, kinds[i], NULL, 0U, &reply) && failed_connection(&f));
    }
    const uint8_t after_end[] = {0};
    const uint8_t no_value[] = {1, 0, 0, 0, 1, 0, 0, 0, 99};
    const uint8_t cut[] = {1, 0, 0, 0, 9, 0, 0, 0};
    fixture_reset(&f);
    TRL_CHECK(test, !decode_bytes(&f, 'G', after_end, sizeof after_end, &reply) && failed_connection(&f));
    fixture_reset(&f);
    TRL_CHECK(test, !decode_bytes(&f, 'B', no_value, sizeof no_value, &reply) && failed_connection(&f));
    fixture_reset(&f);
    TRL_CHECK(test, !decode_bytes(&f, 'B', cut, sizeof cut, &reply) && failed_connection(&f));
    TRL_CHECK(test, strstr(f.error.message, "reply received is malformed") != NULL);
    fixture_free(&f);
}

/* Whether a Complete whose tail is `tags`, each 6 with its rows, fails. */
static bool complete_tail_fails(fixture *f, const uint8_t *tags, size_t count) {
    const uint8_t complete[] = {0, 0, 0, 0, 5, 0, 0, 0, '0', '0', '0', '0', '0', 0};
    fixture_reset(f);
    trl_put_raw(&f->buffer, complete, sizeof complete);
    for (size_t j = 0U; j < count; ++j) {
        trl_put_u8(&f->buffer, tags[j]);
        if (tags[j] == 6U) {
            trl_put_u64(&f->buffer, 1U);
        }
    }
    trl_reply reply;
    return !decode_buffer(f, 'K', &reply) && failed_connection(f);
}

/**
 * Verifies: [PROTOCOL The tail] a Complete's tail tag out of order,
 * repeated, or of another message fails the connection.
 */
static void tails_out_of_place_fail(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t backwards[] = {6U, 5U};
    const uint8_t twice[] = {6U, 6U};
    const uint8_t attributes[] = {1U};
    const uint8_t severity[] = {7U};
    TRL_CHECK(test, complete_tail_fails(&f, backwards, sizeof backwards));
    TRL_CHECK(test, complete_tail_fails(&f, twice, sizeof twice));
    TRL_CHECK(test, complete_tail_fails(&f, attributes, sizeof attributes));
    TRL_CHECK(test, complete_tail_fails(&f, severity, sizeof severity));
    fixture_free(&f);
}

/* Puts an unnamed column of tag 2, precision 10 and scale 0. */
static void put_exact_column(trl_buffer *b) {
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_EXACT);
    trl_put_u32(b, 10U);
    trl_put_u32(b, 0U);
    trl_put_u8(b, 0U);
}

/* A Description of `columns` exact columns, its tail 19 telling `told`
   declared types, if `told` is not 0, then `after`, a tag, if not 0. */
static void put_declared(fixture *f, uint32_t columns, uint32_t told, uint8_t after) {
    trl_buffer_free(&f->buffer);
    trl_buffer_init(&f->buffer);
    trl_put_u32(&f->buffer, columns);
    for (uint32_t i = 0U; i < columns; ++i) {
        put_exact_column(&f->buffer);
    }
    if (told > 0U) {
        trl_put_u8(&f->buffer, 19U);
        trl_put_u32(&f->buffer, told);
        for (uint32_t i = 0U; i < told; ++i) {
            trl_put_bytes(&f->buffer, text_of(i == 0U ? "INTEGER" : "NUMERIC"));
            trl_put_bytes(&f->buffer, text_of(i == 0U ? "INTEGER" : "NUMERIC(10,0)"));
        }
    }
    if (after != 0U) {
        trl_put_u8(&f->buffer, after);
    }
}

/**
 * Verifies: [PROTOCOL Declared types] a Description or a Prepared may end
 * with tail 19, of each column its declared type's name and its type as
 * SQL writes it, one for each column; one of more or fewer, or a tag after
 * 19, is malformed, the connection lost; without it, a column has none.
 */
static void declared_types(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_reply reply;
    put_declared(&f, 2U, 2U, 0U);
    TRL_CHECK(test, decode_buffer(&f, 'D', &reply) && reply.as.description.count == 2U);
    const trl_column *first = &reply.as.description.columns[0];
    const trl_column *second = &reply.as.description.columns[1];
    TRL_CHECK(test, first->declared && slice_is(first->declared_name, "INTEGER") &&
                        slice_is(first->declared_written, "INTEGER") && first->type.tag == TRL_TAG_EXACT);
    TRL_CHECK(test, second->declared && slice_is(second->declared_written, "NUMERIC(10,0)"));
    put_declared(&f, 2U, 0U, 0U);
    TRL_CHECK(test, decode_buffer(&f, 'D', &reply) && !reply.as.description.columns[0].declared);
    put_declared(&f, 2U, 1U, 0U);
    TRL_CHECK(test, !decode_buffer(&f, 'D', &reply) && failed_connection(&f));
    put_declared(&f, 1U, 1U, 20U);
    TRL_CHECK(test, !decode_buffer(&f, 'D', &reply) && failed_connection(&f));
    trl_buffer_free(&f.buffer);
    trl_buffer_init(&f.buffer);
    trl_put_u32(&f.buffer, 7U);
    trl_put_u32(&f.buffer, 0U);
    trl_put_u32(&f.buffer, 1U);
    put_exact_column(&f.buffer);
    trl_put_u8(&f.buffer, 19U);
    trl_put_u32(&f.buffer, 1U);
    trl_put_bytes(&f.buffer, text_of("SMALLINT"));
    trl_put_bytes(&f.buffer, text_of("SMALLINT"));
    TRL_CHECK(test, decode_buffer(&f, 'P', &reply) && reply.as.prepared.columns[0].declared &&
                        slice_is(reply.as.prepared.columns[0].declared_name, "SMALLINT"));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_worked_rows),         TRL_TEST_CASE(the_worked_complete),
        TRL_TEST_CASE(complete_tail),           TRL_TEST_CASE(error_carries_its_failure),
        TRL_TEST_CASE(ready_and_challenge),     TRL_TEST_CASE(prepared_and_description),
        TRL_TEST_CASE(declared_types),          TRL_TEST_CASE(a_value_tag_is_not_its_column_type),
        TRL_TEST_CASE(later_replies),           TRL_TEST_CASE(malformed_replies_fail),
        TRL_TEST_CASE(tails_out_of_place_fail),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
