/**
 * @file lobs_test.c
 * @brief Large objects against a server of the test's own, at their edges
 * [PROTOCOL Lob 'L']: replies out of turn, data longer than asked, a
 * server that sends less than a value's length, Appends of a chunk cut
 * where a character ends, and arguments refused.
 */
#include "codec/buffer.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "harness.h"
#include "script.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* What the server answers a Lob request with. */
typedef enum lob_answer {
    ANSWER_DATA = 0,
    ANSWER_NO_DATA,
    ANSWER_LOCATOR,
    ANSWER_PONG,
    ANSWER_ERROR,
    ANSWER_COMPLETE,
} lob_answer;

/* The server's answer to each request after Ready. */
typedef struct serving {
    lob_answer answer;
} serving;

/* Answers a Lob request, or any other, as planned. */
static void respond(script *s, uint8_t kind, trl_slice body) {
    (void)body;
    const serving *plan = s->data;
    static const uint8_t data[] = {'h', 'e', 'l', 'l', 'o'};
    if (kind == TRL_KIND_HELLO) {
        script_ready(s, "ANN", NULL, 0U);
        return;
    }
    if (plan->answer == ANSWER_DATA) {
        script_lob_data(s, data, sizeof data);
    } else if (plan->answer == ANSWER_NO_DATA) {
        script_lob_data(s, data, 0U);
    } else if (plan->answer == ANSWER_LOCATOR) {
        script_lob_locator(s, 7U);
    } else if (plan->answer == ANSWER_PONG) {
        script_bare(s, TRL_KIND_PONG);
    } else if (plan->answer == ANSWER_ERROR) {
        script_error(s, -301, "0F001", "the locator is not valid", NULL);
    } else {
        script_complete(s, 0, "00000", false, false);
    }
}

/* A connection over a script answering as `plan` says. */
static trl_conn *connected(script *s, serving *plan) {
    script_init(s, respond, plan);
    trl_mechanism *trust = NULL;
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = NULL;
    (void)script_connect(s, trust, NULL, &conn);
    trl_mechanism_free(trust);
    return conn;
}

/* A character large object's locator value, of `octets` octets. */
static trl_value located(uint64_t octets) {
    trl_value value;
    memset(&value, 0, sizeof value);
    value.tag = TRL_TAG_LOB_LOCATOR;
    value.as.locator.kind = TRL_LOB_CHARACTER;
    value.as.locator.locator = 3U;
    value.as.locator.octets = octets;
    value.as.locator.characters = octets;
    return value;
}

/* Whether the connection's failure says `message`. */
static bool failed_with(const trl_conn *conn, const char *message) {
    return strcmp(trl_failure_message(trl_conn_failure(conn)), message) == 0;
}

/**
 * Verifies: [PROTOCOL Lob 'L'] a read by locator answered by a Lob reply
 * of its data; an Error its failure, the connection kept; a reply out of
 * turn, a locator, or more data than asked lose the connection.
 */
static void a_read_takes_its_data(trl_test *test) {
    serving plan = {ANSWER_DATA};
    script s;
    trl_conn *conn = connected(&s, &plan);
    trl_value lob = located(100U);
    uint8_t out[16];
    size_t length = sizeof out;
    trl_lob_range range = {0U, 10U, false};
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, out, &length) == TRL_OK && length == 5U);
    TRL_CHECK(test, memcmp(out, "hello", 5U) == 0);
    plan.answer = ANSWER_ERROR;
    length = sizeof out;
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, out, &length) == TRL_FAILED && !trl_conn_is_lost(conn));
    plan.answer = ANSWER_DATA;
    length = 2U;
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, out, &length) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "the server sent no data for a Lob read") && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
    plan.answer = ANSWER_LOCATOR;
    conn = connected(&s, &plan);
    length = sizeof out;
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, out, &length) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "the server sent no data for a Lob read"));
    trl_conn_free(conn);
    script_free(&s);
    plan.answer = ANSWER_PONG;
    conn = connected(&s, &plan);
    length = sizeof out;
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, out, &length) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "the server sent no Lob reply") && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
}

/**
 * Verifies: [CLIENT Large objects] a reader stops at the value's length; a
 * server that sends less fails the read (SQLSTATE 22000), the connection
 * kept; free and hold are answered by Complete, a reply out of turn losing
 * the connection.
 */
static void readers_and_locators_at_their_edges(trl_test *test) {
    serving plan = {ANSWER_DATA};
    script s;
    trl_conn *conn = connected(&s, &plan);
    trl_value lob = located(5U);
    trl_lob_reader *reader = NULL;
    TRL_CHECK(test, trl_lob_reader_new(conn, &lob, &reader) == TRL_OK);
    uint8_t out[8];
    size_t length = 0U;
    TRL_CHECK(test, trl_lob_reader_read(reader, out, sizeof out, &length) == TRL_OK && length == 5U);
    TRL_CHECK(test, trl_lob_reader_read(reader, out, sizeof out, &length) == TRL_OK && length == 0U);
    trl_lob_reader_free(reader);
    trl_value longer = located(100U);
    plan.answer = ANSWER_NO_DATA;
    TRL_CHECK(test, trl_lob_reader_new(conn, &longer, &reader) == TRL_OK);
    TRL_CHECK(test, trl_lob_reader_read(reader, out, sizeof out, &length) == TRL_FAILED);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "22000") == 0 && !trl_conn_is_lost(conn));
    TRL_CHECK(test, trl_lob_reader_read(reader, out, 0U, &length) == TRL_OK && length == 0U);
    trl_lob_reader_free(reader);
    plan.answer = ANSWER_COMPLETE;
    TRL_CHECK(test, trl_lob_hold(conn, &lob) == TRL_OK && trl_lob_free_locator(conn, &lob) == TRL_OK);
    plan.answer = ANSWER_PONG;
    TRL_CHECK(test, trl_lob_hold(conn, &lob) == TRL_FAILED && failed_with(conn, "the server sent no completion"));
    trl_conn_free(conn);
    script_free(&s);
}

/* How many Appends the client sent, and whether each held at most a chunk
   ending where a character ends. */
static size_t appends_sent(const script *s, bool *whole) {
    size_t count = 0U;
    *whole = true;
    const trl_buffer *sent = &s->from_client;
    for (size_t at = 0U; sent->length - at >= TRL_FRAME_HEADER_BYTES;) {
        trl_frame_header header = {0U, 0U};
        (void)trl_frame_header_decode(sent->data + at, TRL_FRAME_HEADER_BYTES, UINT32_MAX, &header);
        const uint8_t *body = sent->data + at + TRL_FRAME_HEADER_BYTES;
        if (header.kind == TRL_KIND_LOB_REQUEST && body[0] == TRL_LOB_APPEND) {
            ++count;
            /* op, locator, then the bytes' u32 length and the bytes. */
            const uint8_t *bytes = body + 1U + 4U + 4U;
            size_t length = header.length - 1U - 4U - 4U;
            *whole = *whole && length <= TRL_LOB_CHUNK_BYTES && (bytes[length - 1U] & 0xC0U) != 0xC0U;
        }
        at += TRL_FRAME_HEADER_BYTES + header.length;
    }
    return count;
}

/**
 * Verifies: [CLIENT Large objects] a writer's octets sent in Appends of a
 * chunk, a character large object's cut where a character ends, the rest
 * at its finish, and its locator then a parameter; a Create answered by
 * data, or an Append by an Error, fails [PROTOCOL Lob 'L'].
 */
static void writers_send_chunks(trl_test *test) {
    serving plan = {ANSWER_LOCATOR};
    script s;
    trl_conn *conn = connected(&s, &plan);
    trl_lob_writer *writer = NULL;
    TRL_CHECK(test, trl_lob_writer_new(conn, true, &writer) == TRL_OK);
    plan.answer = ANSWER_COMPLETE;
    static uint8_t text[TRL_LOB_CHUNK_BYTES + 1000U];
    for (size_t i = 0U; i < sizeof text; i += 2U) {
        text[i] = 0xC3U;
        text[i + 1U] = 0xA9U;
    }
    TRL_CHECK(test, trl_lob_write(writer, text, TRL_LOB_CHUNK_BYTES - 1U) == TRL_OK);
    TRL_CHECK(test,
              trl_lob_write(writer, text + TRL_LOB_CHUNK_BYTES - 1U, sizeof text - TRL_LOB_CHUNK_BYTES + 1U) == TRL_OK);
    TRL_CHECK(test, trl_lob_write(writer, NULL, 0U) == TRL_OK);
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    TRL_CHECK(test, trl_lob_writer_finish(writer, params) == TRL_OK && trl_params_count(params) == 1U);
    TRL_CHECK(test, trl_lob_write(writer, text, 1U) == TRL_INVALID_ARGUMENT);
    bool whole = false;
    TRL_CHECK(test, appends_sent(&s, &whole) == 2U && whole);
    trl_lob_writer_free(writer);
    TRL_CHECK(test, trl_lob_writer_new(conn, false, &writer) == TRL_FAILED && writer == NULL);
    TRL_CHECK(test, failed_with(conn, "the server sent no Lob reply") && trl_conn_is_lost(conn));
    trl_conn_free(conn);
    script_free(&s);
    plan.answer = ANSWER_LOCATOR;
    conn = connected(&s, &plan);
    TRL_CHECK(test, trl_lob_writer_new(conn, false, &writer) == TRL_OK);
    plan.answer = ANSWER_ERROR;
    TRL_CHECK(test, trl_lob_write(writer, text, sizeof text) == TRL_FAILED);
    trl_lob_writer_free(writer);
    plan.answer = ANSWER_DATA;
    TRL_CHECK(test, trl_lob_writer_new(conn, false, &writer) == TRL_FAILED);
    TRL_CHECK(test, failed_with(conn, "the server sent data for a Lob create") && trl_conn_is_lost(conn));
    trl_params_free(params);
    trl_conn_free(conn);
    script_free(&s);
}

/* Writes `count` octets of `text` to a new character large object, then
   `tail`, then finishes it; the Appends sent, each whole if `whole`. */
static size_t written_in_appends(const uint8_t *text, size_t count, const char *tail, bool *whole) {
    serving plan = {ANSWER_LOCATOR};
    script s;
    trl_conn *conn = connected(&s, &plan);
    trl_lob_writer *writer = NULL;
    trl_params *params = NULL;
    (void)trl_params_new(&params);
    bool done = trl_lob_writer_new(conn, true, &writer) == TRL_OK;
    plan.answer = ANSWER_COMPLETE;
    done = done && trl_lob_write(writer, text, count) == TRL_OK;
    done = done && trl_lob_write(writer, (const uint8_t *)tail, strlen(tail)) == TRL_OK;
    done = done && trl_lob_writer_finish(writer, params) == TRL_OK;
    size_t appends = done ? appends_sent(&s, whole) : 0U;
    trl_lob_writer_free(writer);
    trl_params_free(params);
    trl_conn_free(conn);
    script_free(&s);
    return appends;
}

/**
 * Verifies: [PROTOCOL Lob 'L'] a character large object's chunk is cut
 * where a character ends: before a character the chunk's end splits,
 * whether its last octets begin a character of two, three or four octets,
 * or the chunk's end falls within one; a chunk ending with a whole
 * character is sent whole.
 */
static void chunks_are_cut_where_characters_end(trl_test *test) {
    static uint8_t text[TRL_LOB_CHUNK_BYTES + 16U];
    text[0] = (uint8_t)'a';
    for (size_t i = 1U; i + 1U < sizeof text; i += 2U) {
        text[i] = 0xC3U;
        text[i + 1U] = 0xA9U;
    }
    bool whole = false;
    TRL_CHECK(test, written_in_appends(text, sizeof text - 1U, "", &whole) == 2U && whole);
    memset(text, 'a', sizeof text);
    static const uint8_t endings[][4] = {
        {0xE2U, 0x82U, 0U, 0U},
        {0xF0U, 0x9FU, 0x98U, 0x80U},
        {0xC3U, 0xA9U, 0U, 0U},
        {'a', 0U, 0U, 0U},
    };
    static const size_t lengths[] = {2U, 4U, 2U, 1U};
    static const char *const tails[] = {"\xac", "", "", ""};
    for (size_t i = 0U; i < sizeof lengths / sizeof lengths[0]; ++i) {
        memset(text, 'a', sizeof text);
        memcpy(text + TRL_LOB_CHUNK_BYTES - lengths[i], endings[i], lengths[i]);
        whole = false;
        TRL_CHECK(test, written_in_appends(text, TRL_LOB_CHUNK_BYTES, tails[i], &whole) >= 1U && whole);
    }
}

/**
 * Verifies: [DESIGN] D9 a null pointer, a value of another kind, or no
 * room for one octet, refused with a status, nothing sent.
 */
static void arguments_are_refused(trl_test *test) {
    serving plan = {ANSWER_DATA};
    script s;
    trl_conn *conn = connected(&s, &plan);
    trl_value lob = located(10U);
    trl_value number;
    memset(&number, 0, sizeof number);
    number.tag = TRL_TAG_EXACT;
    uint8_t out[4];
    size_t length = 0U;
    trl_lob_range range = {0U, 1U, false};
    trl_lob_info info;
    TRL_CHECK(test, trl_value_lob(&number, &info) == TRL_INVALID_ARGUMENT &&
                        trl_value_lob(NULL, &info) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_lob(&lob, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, out, &length) == TRL_INVALID_ARGUMENT);
    length = 3U;
    trl_lob_range characters = {0U, 1U, true};
    TRL_CHECK(test, trl_lob_read(conn, &lob, characters, out, &length) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_read(NULL, &lob, range, out, &length) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_read(conn, &number, range, out, &length) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_read(conn, &lob, range, NULL, &length) == TRL_INVALID_ARGUMENT);
    trl_lob_reader *reader = NULL;
    TRL_CHECK(test, trl_lob_reader_new(conn, &number, &reader) == TRL_INVALID_ARGUMENT && reader == NULL);
    TRL_CHECK(test, trl_lob_reader_new(NULL, &lob, &reader) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_reader_new(conn, &lob, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_reader_read(NULL, out, 1U, &length) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_free_locator(NULL, &lob) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_hold(conn, &number) == TRL_INVALID_ARGUMENT);
    trl_lob_writer *writer = NULL;
    TRL_CHECK(test, trl_lob_writer_new(NULL, true, &writer) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_writer_new(conn, true, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_write(NULL, out, 1U) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_lob_writer_finish(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, s.from_client.length > 0U && !trl_conn_is_lost(conn));
    trl_lob_reader_free(NULL);
    trl_lob_writer_free(NULL);
    trl_conn_free(conn);
    script_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_read_takes_its_data), TRL_TEST_CASE(readers_and_locators_at_their_edges),
        TRL_TEST_CASE(writers_send_chunks),   TRL_TEST_CASE(chunks_are_cut_where_characters_end),
        TRL_TEST_CASE(arguments_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
