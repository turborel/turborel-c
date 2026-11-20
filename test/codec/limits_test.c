/**
 * @file limits_test.c
 * @brief Memory not had, at every point a codec function asks for it: the
 * call fails as SQLCODE -918, SQLSTATE TB918, never anything worse, and
 * leaks nothing ([DESIGN] D9).
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/json.h"
#include "codec/kinds.h"
#include "codec/reader.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "codec/values.h"
#include "corpus.h"
#include "fixture.h"
#include "harness.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The most bytes a sweep of limits tries. */
#define MOST_LIMIT 9000U

/* Whether the fixture's error is memory not had. */
static bool out_of_memory(const fixture *f) {
    return failed_with(f, TRL_SQLCODE_RESOURCES, TRL_SQLSTATE_RESOURCES);
}

/**
 * Verifies: [DESIGN] D9 a reply decoded within an arena bounded at any
 * size is decoded whole or fails as memory not had.
 */
static void replies_within_any_arena(trl_test *test) {
    sample samples[SAMPLES];
    size_t count = corpus(samples);
    for (size_t i = 0U; i < count; ++i) {
        bool whole = false;
        for (size_t limit = 1U; limit <= MOST_LIMIT && !whole; limit += 16U) {
            fixture f;
            fixture_init(&f);
            f.arena.limit = limit;
            trl_reply reply;
            whole = decode_bytes(&f, samples[i].kind, samples[i].body.data, samples[i].body.length, &reply);
            TRL_CHECK(test, whole || out_of_memory(&f));
            fixture_free(&f);
        }
        TRL_CHECK(test, whole);
    }
    free_corpus(samples);
}

/* Encodes the requests of every kind into `f`'s buffer; gives whether all
   were encoded. */
static bool encode_every_request(fixture *f) {
    const trl_slice capabilities[] = {text_of("ping"), text_of("status")};
    const trl_attribute attribute = {text_of("application"), text_of("report")};
    trl_hello hello = {1U, text_of("ANN"), text_of("trust"), text_of("x"), capabilities, 2U, &attribute, 1U};
    trl_query_request query = {text_of("VALUES 1"), true, &attribute, 1U, true};
    trl_value values[2] = {{TRL_TAG_CHARACTER, {text_of("abc")}}, {TRL_TAG_NULL, {{NULL, 0U}}}};
    trl_execute_request execute = {1U, values, 2U, 0U, false, &attribute, 1U, 2U};
    trl_lob_request append = {TRL_LOB_APPEND, 1U, 0U, 0U, false, 0U, text_of("data")};
    const trl_slice columns[] = {text_of("ID")};
    trl_copy_in_request copy = {text_of("T"), columns, 1U, false};
    trl_row_values rows[1] = {{values, 2U}};
    uint8_t key[16] = {0};
    return trl_encode_hello(&f->buffer, &hello, &f->error) && trl_encode_query(&f->buffer, &query, &f->error) &&
           trl_encode_execute(&f->buffer, &execute, &f->error) && trl_encode_lob(&f->buffer, &append, &f->error) &&
           trl_encode_prepare(&f->buffer, 1U, text_of("SELECT 1"), &f->error) &&
           trl_encode_change_user(&f->buffer, text_of("B"), text_of("trust"), text_of(""), &f->error) &&
           trl_encode_copy_in(&f->buffer, &copy, &f->error) && trl_encode_copy_data(&f->buffer, rows, 1U, &f->error) &&
           trl_encode_cancel(&f->buffer, bytes_of(key, sizeof key), &f->error) &&
           trl_encode_answer(&f->buffer, text_of("a"), &f->error);
}

/**
 * Verifies: [DESIGN] D9 requests encoded into a buffer bounded at any size
 * are all encoded or fail as memory not had.
 */
static void requests_within_any_buffer(trl_test *test) {
    bool whole = false;
    for (size_t limit = 1U; limit <= MOST_LIMIT && !whole; ++limit) {
        fixture f;
        fixture_init(&f);
        f.buffer.limit = limit;
        whole = encode_every_request(&f);
        TRL_CHECK(test, whole || out_of_memory(&f));
        fixture_free(&f);
    }
    TRL_CHECK(test, whole);
}

/* Whether a JSON text is made a document and written back as text, within
   an arena and buffers of `limit` bytes. */
static bool json_within(fixture *f, const char *text, size_t limit) {
    f->arena.limit = limit;
    f->buffer.limit = limit;
    if (!trl_json_from_text(&f->buffer, (const uint8_t *)text, strlen(text), &f->arena, &f->error)) {
        return false;
    }
    trl_buffer out;
    trl_buffer_init(&out);
    out.limit = limit;
    bool written = trl_json_to_text(&out, f->buffer.data, f->buffer.length, &f->error);
    trl_buffer_free(&out);
    return written;
}

/**
 * Verifies: [DESIGN] D9 JSON made from text and written as text within
 * any bound is made whole or fails as memory not had.
 */
static void json_within_any_bound(trl_test *test) {
    const char *text = "{\"b\":[1,2.5,\"x\\n\",true,null,{}],\"a\":1E3,\"b\":12345678901234567890}";
    bool whole = false;
    for (size_t limit = 1U; limit <= MOST_LIMIT && !whole; ++limit) {
        fixture f;
        fixture_init(&f);
        whole = json_within(&f, text, limit);
        TRL_CHECK(test, whole || out_of_memory(&f));
        fixture_free(&f);
    }
    TRL_CHECK(test, whole);
}

/**
 * Verifies: [DESIGN] D9 the first failure of a call is the one told; a
 * failure recorded nowhere is ignored.
 */
static void the_first_failure_is_kept(trl_test *test) {
    trl_codec_error error;
    memset(&error, 0, sizeof error);
    trl_codec_malformed(&error, "reply", 3U, "first");
    trl_codec_no_memory(&error);
    TRL_CHECK(test, error.sqlcode == TRL_SQLCODE_CONNECTION && strstr(error.message, "byte 3: first") != NULL);
    trl_codec_fail(NULL, TRL_SQLCODE_API, TRL_SQLSTATE_API, "nowhere");
    trl_codec_no_memory(NULL);
    trl_arena_free(NULL);
    trl_buffer_free(NULL);
}

/* Reads one value from the fixture's buffer, all of it. */
static bool read_one(fixture *f, trl_value *out) {
    trl_reader r;
    trl_reader_init(&r, f->buffer.data, f->buffer.length, &f->arena, &f->error);
    return trl_read_value(&r, out) && trl_reader_finish(&r);
}

/**
 * Verifies: [PROTOCOL Values] a structured value nests as an array does,
 * one level for itself and its attributes, so one 16 deep is read as
 * turborel-protocol reads it, and 17 deep refused.
 */
static void structured_values_nest_one_level(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value back;
    memset(&back, 0, sizeof back);
    for (unsigned int depth = 16U; depth <= 17U; ++depth) {
        fixture_reset(&f);
        for (unsigned int i = 0U; i < depth; ++i) {
            trl_put_u8(&f.buffer, TRL_TAG_STRUCTURED);
            trl_put_bytes(&f.buffer, text_of("S"));
            trl_put_bytes(&f.buffer, text_of("T"));
            trl_put_u32(&f.buffer, 1U);
        }
        trl_put_u8(&f.buffer, TRL_TAG_NULL);
        bool read = read_one(&f, &back);
        TRL_CHECK(test, depth == 16U ? read : (!read && failed_connection(&f)));
    }
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Composite fields] a list is read whole however many
 * items it has, its room grown as they are read; a count past the items
 * sent fails the connection without holding room for every item it says.
 */
static void lists_grow_as_they_are_read(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_put_u8(&f.buffer, TRL_TAG_ARRAY);
    trl_put_u32(&f.buffer, 1000U);
    for (unsigned int i = 0U; i < 1000U; ++i) {
        trl_put_u8(&f.buffer, TRL_TAG_BOOLEAN);
        trl_put_u8(&f.buffer, (uint8_t)(i % 2U));
    }
    trl_value back;
    memset(&back, 0, sizeof back);
    TRL_CHECK(test, read_one(&f, &back) && back.as.list.count == 1000U);
    TRL_CHECK(test, back.as.list.items[999].as.truth && !back.as.list.items[998].as.truth);
    fixture_reset(&f);
    trl_put_u8(&f.buffer, TRL_TAG_ARRAY);
    trl_put_u32(&f.buffer, 64U);
    for (unsigned int i = 0U; i < 64U; ++i) {
        trl_put_u8(&f.buffer, TRL_TAG_ARRAY);
    }
    TRL_CHECK(test, !read_one(&f, &back) && failed_connection(&f));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(replies_within_any_arena),
        TRL_TEST_CASE(requests_within_any_buffer),
        TRL_TEST_CASE(json_within_any_bound),
        TRL_TEST_CASE(the_first_failure_is_kept),
        TRL_TEST_CASE(structured_values_nest_one_level),
        TRL_TEST_CASE(lists_grow_as_they_are_read),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
