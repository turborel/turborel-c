/**
 * @file requests_test.c
 * @brief Every request encoded, byte for byte, its tail written only with
 * what it says [PROTOCOL Requests].
 */
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "codec/values.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/**
 * Verifies: [PROTOCOL A worked example] the Hello of trust as ANN asking
 * for ping is the 41 bytes the spec prints, with no tail.
 */
static void the_worked_hello(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const trl_slice capabilities[] = {text_of("ping")};
    trl_hello hello = {1U, text_of("ANN"), text_of("trust"), {NULL, 0U}, capabilities, 1U, NULL, 0U};
    TRL_CHECK(test, trl_encode_hello(&f.buffer, &hello, &f.error));
    const uint8_t expected[] = {
        0x48, 0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x41,
        0x4E, 0x4E, 0x05, 0x00, 0x00, 0x00, 0x74, 0x72, 0x75, 0x73, 0x74, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x70, 0x69, 0x6E, 0x67,
    };
    TRL_CHECK(test, sizeof expected == 41U && buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Hello 'H'] Hello's attributes are its tail, tag 1, a
 * count then each name and value.
 */
static void hello_carries_attributes(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const trl_attribute attribute = {text_of("application"), text_of("report")};
    trl_hello hello = {1U, text_of("A"), text_of("trust"), {NULL, 0U}, NULL, 0U, &attribute, 1U};
    TRL_CHECK(test, trl_encode_hello(&f.buffer, &hello, &f.error));
    const uint8_t tail[] = {
        1,   1,   0,   0,   0,   11, 0, 0, 0, 'a', 'p', 'p', 'l', 'i', 'c',
        'a', 't', 'i', 'o', 'n', 6,  0, 0, 0, 'r', 'e', 'p', 'o', 'r', 't',
    };
    TRL_CHECK(test, f.buffer.length > sizeof tail);
    TRL_CHECK(test, memcmp(f.buffer.data + f.buffer.length - sizeof tail, tail, sizeof tail) == 0);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL A worked example] the Query of VALUES 1 with ends set
 * is the bytes the spec prints.
 */
static void the_worked_query(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_query_request query = {text_of("VALUES 1"), true, NULL, 0U, false};
    TRL_CHECK(test, trl_encode_query(&f.buffer, &query, &f.error));
    const uint8_t expected[] = {
        0x51, 0x0D, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x56, 0x41, 0x4C, 0x55, 0x45, 0x53, 0x20, 0x31, 0x01,
    };
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Query 'Q'] a Query's tail is tag 1 attributes, then
 * tag 2 multi, in that order.
 */
static void query_tail_in_order(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const trl_attribute attribute = {text_of("traceparent"), text_of("00-1")};
    trl_query_request query = {text_of("X"), false, &attribute, 1U, true};
    TRL_CHECK(test, trl_encode_query(&f.buffer, &query, &f.error));
    const uint8_t start[] = {'Q', 35, 0, 0, 0, 1, 0, 0, 0, 'X', 0, 1, 1, 0, 0, 0};
    TRL_CHECK(test, memcmp(f.buffer.data, start, sizeof start) == 0);
    TRL_CHECK(test, f.buffer.data[f.buffer.length - 1U] == 2U && f.buffer.length == 40U);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Execute 'E'] an Execute is statement, values, rows,
 * ends, then tail 1 attributes and 3 cursor; cursor 0 is written as
 * nothing, so the message is its first form.
 */
static void execute_is_encoded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_value values[2] = {{TRL_TAG_NULL, {{NULL, 0U}}}, {TRL_TAG_BOOLEAN, {{NULL, 0U}}}};
    values[1].as.truth = true;
    trl_execute_request execute = {7U, values, 2U, 100U, false, NULL, 0U, 0U};
    TRL_CHECK(test, trl_encode_execute(&f.buffer, &execute, &f.error));
    const uint8_t first[] = {'E', 16, 0, 0, 0, 7, 0, 0, 0, 2, 0, 0, 0, 0, 14, 1, 100, 0, 0, 0, 0};
    TRL_CHECK(test, buffer_is(&f.buffer, first, sizeof first));
    fixture_reset(&f);
    execute.cursor = 3U;
    TRL_CHECK(test, trl_encode_execute(&f.buffer, &execute, &f.error));
    const uint8_t tail[] = {3, 3, 0, 0, 0};
    TRL_CHECK(test, f.buffer.length == sizeof first + sizeof tail);
    TRL_CHECK(test, memcmp(f.buffer.data + sizeof first, tail, sizeof tail) == 0);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Prepare 'P'] Prepare, [PROTOCOL Fetch 'F'] Fetch,
 * [PROTOCOL Close 'C'] Close and [PROTOCOL Deallocate 'D'] Deallocate are
 * their fields, Fetch's and Close's cursor in tag 3.
 */
static void statement_requests(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, trl_encode_prepare(&f.buffer, 1U, text_of("SELECT 1"), &f.error));
    TRL_CHECK(test, trl_encode_fetch(&f.buffer, 1U, 2U, 0U, &f.error));
    TRL_CHECK(test, trl_encode_close(&f.buffer, 1U, 4U, &f.error));
    TRL_CHECK(test, trl_encode_deallocate(&f.buffer, 1U, &f.error));
    const uint8_t expected[] = {
        'P', 16, 0, 0, 0, 1,   0, 0, 0, 8, 0, 0, 0, 'S', 'E', 'L', 'E', 'C', 'T', ' ', '1', 'F', 8, 0, 0, 0, 1, 0, 0,
        0,   2,  0, 0, 0, 'C', 9, 0, 0, 0, 1, 0, 0, 0,   3,   4,   0,   0,   0,   'D', 4,   0,   0, 0, 1, 0, 0, 0,
    };
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Lob 'L'] a Lob request is its op, then by op: read,
 * create, append, free and hold; another op or kind is refused.
 */
static void lob_requests(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_lob_request read = {TRL_LOB_READ, 9U, 262144U, 262144U, true, 0U, {NULL, 0U}};
    trl_lob_request create = {TRL_LOB_CREATE, 0U, 0U, 0U, false, TRL_LOB_BINARY, {NULL, 0U}};
    trl_lob_request append = {TRL_LOB_APPEND, 9U, 0U, 0U, false, 0U, text_of("ab")};
    trl_lob_request free_it = {TRL_LOB_FREE, 9U, 0U, 0U, false, 0U, {NULL, 0U}};
    trl_lob_request hold = {TRL_LOB_HOLD, 9U, 0U, 0U, false, 0U, {NULL, 0U}};
    TRL_CHECK(test, trl_encode_lob(&f.buffer, &read, &f.error) && trl_encode_lob(&f.buffer, &create, &f.error));
    TRL_CHECK(test, trl_encode_lob(&f.buffer, &append, &f.error) && trl_encode_lob(&f.buffer, &free_it, &f.error));
    TRL_CHECK(test, trl_encode_lob(&f.buffer, &hold, &f.error));
    const uint8_t expected[] = {
        'L', 22,  0,   0,   0, 0, 9, 0, 0, 0, 0,   0,  4,   0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0,
        0,   0,   1,   'L', 2, 0, 0, 0, 1, 1, 'L', 11, 0,   0, 0, 2, 9, 0, 0, 0, 2, 0, 0, 0,
        'a', 'b', 'L', 5,   0, 0, 0, 3, 9, 0, 0,   0,  'L', 5, 0, 0, 0, 4, 9, 0, 0, 0,
    };
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    size_t before = f.buffer.length;
    trl_lob_request wrong = {5U, 0U, 0U, 0U, false, 0U, {NULL, 0U}};
    TRL_CHECK(test, !trl_encode_lob(&f.buffer, &wrong, &f.error) && f.buffer.length == before);
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_API, TRL_SQLSTATE_API));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Cancel 'Z'] Cancel is the 16-byte key alone; a key
 * of another length is refused.
 */
static void cancel_is_the_key(trl_test *test) {
    fixture f;
    fixture_init(&f);
    uint8_t key[16];
    memset(key, 0x5A, sizeof key);
    TRL_CHECK(test, trl_encode_cancel(&f.buffer, bytes_of(key, sizeof key), &f.error));
    TRL_CHECK(test, f.buffer.length == 21U && f.buffer.data[0] == 'Z' && f.buffer.data[1] == 16U);
    fixture_reset(&f);
    TRL_CHECK(test, !trl_encode_cancel(&f.buffer, bytes_of(key, 15U), &f.error) && f.buffer.length == 0U);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL A worked example] Ping is a header alone, as are
 * Terminate and Reset [PROTOCOL Terminate 'X'] [PROTOCOL Reset 'W'].
 */
static void bare_requests(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, trl_encode_bare(&f.buffer, TRL_KIND_PING, &f.error));
    TRL_CHECK(test, trl_encode_bare(&f.buffer, TRL_KIND_TERMINATE, &f.error));
    TRL_CHECK(test, trl_encode_bare(&f.buffer, TRL_KIND_RESET, &f.error));
    const uint8_t expected[] = {0x47, 0, 0, 0, 0, 'X', 0, 0, 0, 0, 'W', 0, 0, 0, 0};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    TRL_CHECK(test, !trl_encode_bare(&f.buffer, TRL_KIND_QUERY, &f.error));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Describe 'Y'] Describe is what, statement and cursor;
 * a statement's of a cursor other than 0 is refused.
 */
static void describe_is_encoded(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, trl_encode_describe(&f.buffer, TRL_DESCRIBE_CURSOR, 2U, 5U, &f.error));
    const uint8_t expected[] = {'Y', 9, 0, 0, 0, 1, 2, 0, 0, 0, 5, 0, 0, 0};
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    TRL_CHECK(test, !trl_encode_describe(&f.buffer, TRL_DESCRIBE_STATEMENT, 2U, 5U, &f.error));
    TRL_CHECK(test, f.buffer.length == sizeof expected);
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL ChangeUser 'U'] ChangeUser is user, mechanism and
 * initial bytes; [PROTOCOL Answer 'A'] Answer is bytes.
 */
static void authentication_requests(trl_test *test) {
    fixture f;
    fixture_init(&f);
    TRL_CHECK(test, trl_encode_change_user(&f.buffer, text_of("B"), text_of("trust"), text_of("x"), &f.error));
    TRL_CHECK(test, trl_encode_answer(&f.buffer, text_of("ok"), &f.error));
    const uint8_t expected[] = {
        'U', 19, 0, 0, 0, 1,   0,   0, 0, 'B', 5, 0, 0, 0, 't', 'r', 'u', 's',
        't', 1,  0, 0, 0, 'x', 'A', 6, 0, 0,   0, 2, 0, 0, 0,   'o', 'k',
    };
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL CopyIn 'I'] CopyIn is table, columns and ends;
 * [PROTOCOL CopyData 'J'] CopyData a list of rows; [PROTOCOL CopyEnd 'N']
 * CopyEnd the abandon flag.
 */
static void copy_requests(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const trl_slice columns[] = {text_of("ID")};
    trl_copy_in_request copy = {text_of("T"), columns, 1U, true};
    trl_value values[1] = {{TRL_TAG_NULL, {{NULL, 0U}}}};
    trl_row_values rows[2] = {{values, 1U}, {values, 1U}};
    TRL_CHECK(test, trl_encode_copy_in(&f.buffer, &copy, &f.error));
    TRL_CHECK(test, trl_encode_copy_data(&f.buffer, rows, 2U, &f.error));
    TRL_CHECK(test, trl_encode_copy_end(&f.buffer, false, &f.error));
    const uint8_t expected[] = {
        'I', 16, 0, 0, 0, 1, 0, 0, 0, 'T', 1, 0, 0, 0, 2, 0, 0, 0,   'I', 'D', 1, 'J', 14,
        0,   0,  0, 2, 0, 0, 0, 1, 0, 0,   0, 0, 1, 0, 0, 0, 0, 'N', 1,   0,   0, 0,   0,
    };
    TRL_CHECK(test, buffer_is(&f.buffer, expected, sizeof expected));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL The tail] more attributes than 32, a name of no
 * byte or past 64, or a value past 1024, are refused before they are
 * sent, the buffer left as it was.
 */
static void attributes_out_of_bounds_are_refused(trl_test *test) {
    fixture f;
    fixture_init(&f);
    trl_attribute many[33];
    for (size_t i = 0U; i < TRL_TEST_COUNT(many); ++i) {
        many[i].name = text_of("n");
        many[i].value = text_of("v");
    }
    trl_query_request query = {text_of("X"), false, many, 33U, false};
    TRL_CHECK(test, !trl_encode_query(&f.buffer, &query, &f.error) && f.buffer.length == 0U);
    query.attribute_count = 32U;
    TRL_CHECK(test, trl_encode_query(&f.buffer, &query, &f.error));
    char name[66];
    memset(name, 'n', 65U);
    name[65] = '\0';
    trl_attribute long_name = {text_of(name), text_of("v")};
    trl_attribute no_name = {text_of(""), text_of("v")};
    trl_query_request bad = {text_of("X"), false, &long_name, 1U, false};
    fixture_reset(&f);
    TRL_CHECK(test, !trl_encode_query(&f.buffer, &bad, &f.error));
    bad.attributes = &no_name;
    fixture_reset(&f);
    TRL_CHECK(test, !trl_encode_query(&f.buffer, &bad, &f.error));
    TRL_CHECK(test, failed_with(&f, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_CONNECTION_FAILURE));
    fixture_free(&f);
}

/**
 * Verifies: [PROTOCOL Values and fields] a string to send that is not
 * UTF-8, a Lob create of no kind, or a CopyData given no rows, is refused,
 * and the buffer left as it was.
 */
static void fields_that_cannot_be_sent_are_refused(trl_test *test) {
    fixture f;
    fixture_init(&f);
    const uint8_t bad[] = {0xFF};
    const trl_slice not_utf8 = {bad, sizeof bad};
    trl_hello hello = {1U, not_utf8, text_of("trust"), {NULL, 0U}, NULL, 0U, NULL, 0U};
    trl_query_request query = {not_utf8, false, NULL, 0U, false};
    trl_copy_in_request copy = {not_utf8, NULL, 0U, false};
    trl_lob_request create = {TRL_LOB_CREATE, 0U, 0U, 0U, false, 2U, {NULL, 0U}};
    TRL_CHECK(test, !trl_encode_hello(&f.buffer, &hello, &f.error));
    TRL_CHECK(test, !trl_encode_query(&f.buffer, &query, &f.error));
    TRL_CHECK(test, !trl_encode_prepare(&f.buffer, 1U, not_utf8, &f.error));
    TRL_CHECK(test, !trl_encode_change_user(&f.buffer, not_utf8, text_of("x"), text_of(""), &f.error));
    TRL_CHECK(test, !trl_encode_copy_in(&f.buffer, &copy, &f.error));
    TRL_CHECK(test, !trl_encode_lob(&f.buffer, &create, &f.error));
    TRL_CHECK(test, !trl_encode_copy_data(&f.buffer, NULL, 1U, &f.error));
    trl_value value = {TRL_TAG_CHARACTER, {not_utf8}};
    trl_execute_request execute = {1U, &value, 1U, 0U, false, NULL, 0U, 0U};
    TRL_CHECK(test, !trl_encode_execute(&f.buffer, &execute, &f.error));
    TRL_CHECK(test, f.buffer.length == 0U && failed_with(&f, TRL_SQLCODE_DATA, TRL_SQLSTATE_NOT_UTF8));
    fixture_free(&f);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_worked_hello),
        TRL_TEST_CASE(hello_carries_attributes),
        TRL_TEST_CASE(the_worked_query),
        TRL_TEST_CASE(query_tail_in_order),
        TRL_TEST_CASE(execute_is_encoded),
        TRL_TEST_CASE(statement_requests),
        TRL_TEST_CASE(lob_requests),
        TRL_TEST_CASE(cancel_is_the_key),
        TRL_TEST_CASE(bare_requests),
        TRL_TEST_CASE(describe_is_encoded),
        TRL_TEST_CASE(authentication_requests),
        TRL_TEST_CASE(copy_requests),
        TRL_TEST_CASE(attributes_out_of_bounds_are_refused),
        TRL_TEST_CASE(fields_that_cannot_be_sent_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
