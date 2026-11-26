/**
 * @file samples_encoded.c
 * @brief The request of each frame a client sends among the sample
 * frames, made by the driver's own encoders as the sample's meaning
 * gives it [PROTOCOL Sample frames] [PROTOCOL Requests].
 */
#include "samples_encoded.h"

#include "codec/buffer.h"
#include "codec/debug.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/requests.h"
#include "codec/tags.h"
#include "codec/tail.h"
#include "codec/values.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The longest attribute's name and value, the most attributes, a cancel key's octets. */
#define NAME_MOST 64U
#define VALUE_MOST 1024U
#define ATTRIBUTES_MOST 32U
#define KEY_BYTES 16U
#define KEY_OCTET 0xA5U
/* The rows a fetch asks for, a read's count, and the octet C3. */
#define FETCH_ROWS 100U
#define READ_COUNT 65536U
#define HALF_A_CHARACTER 0xC3U
/* The statement, cursor and locators the samples name. */
#define CURSOR 7U
#define APPENDED 4U
#define READ 3U

static trl_slice slice_of(const char *text) {
    trl_slice s = {(const uint8_t *)text, strlen(text)};
    return s;
}

/* What a Hello as ANN by trust asks for, and the attributes it gives. */
typedef struct asking {
    const trl_slice *asked;
    size_t count;
    const trl_attribute *attributes;
    size_t attribute_count;
} asking;

static bool hello_of(trl_buffer *b, trl_codec_error *e, const asking *a) {
    trl_hello hello = {
        1U, slice_of("ANN"), slice_of("trust"), {NULL, 0U}, a->asked, a->count, a->attributes, a->attribute_count,
    };
    return trl_encode_hello(b, &hello, e);
}

static bool hello_ping(trl_buffer *b, trl_codec_error *e) {
    const trl_slice asked[] = {slice_of("ping")};
    const asking a = {asked, 1U, NULL, 0U};
    return hello_of(b, e, &a);
}

static bool hello_none(trl_buffer *b, trl_codec_error *e) {
    const asking a = {NULL, 0U, NULL, 0U};
    return hello_of(b, e, &a);
}

static bool hello_every(trl_buffer *b, trl_codec_error *e) {
    const trl_slice asked[] = {
        slice_of("status"),      slice_of("describe"),       slice_of("cursors"),
        slice_of("compression"), slice_of("declared-types"),
    };
    const trl_attribute application = {slice_of("application"), slice_of("app")};
    const asking a = {asked, sizeof asked / sizeof asked[0], &application, 1U};
    return hello_of(b, e, &a);
}

static bool hello_attribute_longest(trl_buffer *b, trl_codec_error *e) {
    static uint8_t name[NAME_MOST];
    static uint8_t value[VALUE_MOST];
    memset(name, 'a', sizeof name);
    memset(value, 'v', sizeof value);
    const trl_attribute longest = {{name, sizeof name}, {value, sizeof value}};
    const asking a = {NULL, 0U, &longest, 1U};
    return hello_of(b, e, &a);
}

static bool hello_attributes_most(trl_buffer *b, trl_codec_error *e) {
    static char names[ATTRIBUTES_MOST][4];
    trl_attribute most[ATTRIBUTES_MOST];
    for (unsigned int i = 0U; i < ATTRIBUTES_MOST; ++i) {
        names[i][0] = 'a';
        names[i][1] = (char)(i < 10U ? '0' + i : '0' + (i / 10U));
        names[i][2] = (char)(i < 10U ? '\0' : '0' + (i % 10U));
        names[i][3] = '\0';
        most[i].name = slice_of(names[i]);
        most[i].value = slice_of("");
    }
    const asking a = {NULL, 0U, most, ATTRIBUTES_MOST};
    return hello_of(b, e, &a);
}

static bool hello_scram(trl_buffer *b, trl_codec_error *e) {
    trl_hello hello = {1U, slice_of("ANN"), slice_of("scram-sha-256"), slice_of("n,,n=,r=abc"), NULL, 0U, NULL, 0U};
    return trl_encode_hello(b, &hello, e);
}

static bool answer(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_answer(b, slice_of("c=biws,r=abc,p=xyz"), e);
}

static bool answer_empty(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_answer(b, slice_of(""), e);
}

static bool cancel(trl_buffer *b, trl_codec_error *e) {
    uint8_t key[KEY_BYTES];
    memset(key, KEY_OCTET, sizeof key);
    trl_slice k = {key, sizeof key};
    return trl_encode_cancel(b, k, e);
}

static bool query(trl_buffer *b, trl_codec_error *e) {
    trl_query_request q = {slice_of("VALUES 1"), true, NULL, 0U, false};
    return trl_encode_query(b, &q, e);
}

static bool query_empty(trl_buffer *b, trl_codec_error *e) {
    trl_query_request q = {slice_of(""), false, NULL, 0U, false};
    return trl_encode_query(b, &q, e);
}

static bool query_attributes(trl_buffer *b, trl_codec_error *e) {
    const trl_attribute trace = {slice_of("trace"), slice_of("t1")};
    trl_query_request q = {slice_of("VALUES QUERY_ATTRIBUTE('trace')"), false, &trace, 1U, false};
    return trl_encode_query(b, &q, e);
}

static bool query_multi(trl_buffer *b, trl_codec_error *e) {
    trl_query_request q = {slice_of("VALUES 1; VALUES 2"), true, NULL, 0U, true};
    return trl_encode_query(b, &q, e);
}

static bool prepare(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_prepare(b, 1U, slice_of("SELECT K FROM T WHERE K > ?"), e);
}

static bool execute(trl_buffer *b, trl_codec_error *e) {
    trl_value values[3];
    memset(values, 0, sizeof values);
    values[0].tag = TRL_TAG_NULL;
    values[1].tag = TRL_TAG_CHARACTER;
    values[1].as.bytes = slice_of("");
    values[2].tag = TRL_TAG_BOOLEAN;
    values[2].as.truth = true;
    trl_execute_request x = {1U, values, 3U, 2U, false, NULL, 0U, 0U};
    return trl_encode_execute(b, &x, e);
}

static bool execute_cursor(trl_buffer *b, trl_codec_error *e) {
    const trl_attribute ab = {slice_of("a"), slice_of("b")};
    trl_execute_request x = {1U, NULL, 0U, 0U, true, &ab, 1U, CURSOR};
    return trl_encode_execute(b, &x, e);
}

static bool fetch(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_fetch(b, 1U, FETCH_ROWS, 0U, e);
}

static bool fetch_cursor(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_fetch(b, 1U, UINT32_MAX, CURSOR, e);
}

static bool close_statement(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_close(b, 1U, 0U, e);
}

static bool close_cursor(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_close(b, 1U, CURSOR, e);
}

static bool deallocate(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_deallocate(b, UINT32_MAX, e);
}

static bool terminate(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_bare(b, TRL_KIND_TERMINATE, e);
}

static bool lob_read(trl_buffer *b, trl_codec_error *e) {
    trl_lob_request r = {TRL_LOB_READ, READ, 0U, READ_COUNT, true, 0U, {NULL, 0U}};
    return trl_encode_lob(b, &r, e);
}

static bool lob_create(trl_buffer *b, trl_codec_error *e) {
    trl_lob_request r = {TRL_LOB_CREATE, 0U, 0U, 0U, false, TRL_LOB_BINARY, {NULL, 0U}};
    return trl_encode_lob(b, &r, e);
}

static bool lob_append(trl_buffer *b, trl_codec_error *e) {
    static const uint8_t half = HALF_A_CHARACTER;
    trl_lob_request r = {TRL_LOB_APPEND, APPENDED, 0U, 0U, false, 0U, {&half, 1U}};
    return trl_encode_lob(b, &r, e);
}

static bool lob_free(trl_buffer *b, trl_codec_error *e) {
    trl_lob_request r = {TRL_LOB_FREE, APPENDED, 0U, 0U, false, 0U, {NULL, 0U}};
    return trl_encode_lob(b, &r, e);
}

static bool lob_hold(trl_buffer *b, trl_codec_error *e) {
    trl_lob_request r = {TRL_LOB_HOLD, READ, 0U, 0U, false, 0U, {NULL, 0U}};
    return trl_encode_lob(b, &r, e);
}

static bool describe_statement(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_describe(b, TRL_DESCRIBE_STATEMENT, 1U, 0U, e);
}

static bool describe_cursor(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_describe(b, TRL_DESCRIBE_CURSOR, 1U, CURSOR, e);
}

static bool ping(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_bare(b, TRL_KIND_PING, e);
}

static bool reset(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_bare(b, TRL_KIND_RESET, e);
}

static bool change_user(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_change_user(b, slice_of("BOB"), slice_of("password"), slice_of("secret"), e);
}

static bool copy_in(trl_buffer *b, trl_codec_error *e) {
    const trl_slice columns[] = {slice_of("ID"), slice_of("NAME")};
    trl_copy_in_request c = {slice_of("S.T"), columns, 2U, true};
    return trl_encode_copy_in(b, &c, e);
}

static bool copy_in_every_column(trl_buffer *b, trl_codec_error *e) {
    trl_copy_in_request c = {slice_of("T"), NULL, 0U, false};
    return trl_encode_copy_in(b, &c, e);
}

static bool copy_data(trl_buffer *b, trl_codec_error *e) {
    trl_value values[4];
    memset(values, 0, sizeof values);
    static const char *const texts[] = {"1", NULL, "2", "b"};
    for (size_t i = 0U; i < 4U; ++i) {
        values[i].tag = texts[i] != NULL ? TRL_TAG_CHARACTER : TRL_TAG_NULL;
        values[i].as.bytes = texts[i] != NULL ? slice_of(texts[i]) : slice_of("");
    }
    trl_row_values rows[2] = {{values, 2U}, {values + 2, 2U}};
    return trl_encode_copy_data(b, rows, 2U, e);
}

/* Two rows by column: 1 and null, b and the empty string. */
static bool copy_columns(trl_buffer *b, trl_codec_error *e) {
    static const uint8_t first_nulls[] = {0x02U};
    static const uint8_t second_nulls[] = {0x00U};
    static const uint32_t ends[] = {1U, 1U};
    trl_copy_column columns[2] = {
        {{first_nulls, 1U}, ends, slice_of("1")},
        {{second_nulls, 1U}, ends, slice_of("b")},
    };
    return trl_encode_copy_columns(b, 2U, columns, 2U, e);
}

static bool copy_end(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_copy_end(b, false, e);
}

static bool copy_end_abandon(trl_buffer *b, trl_codec_error *e) {
    return trl_encode_copy_end(b, true, e);
}

/* A Debug request asking `what`, its other fields as the samples give them. */
static bool debug_of(trl_buffer *b, trl_codec_error *e, uint8_t what) {
    trl_debug_request request;
    memset(&request, 0, sizeof request);
    request.what = what;
    request.connection = 2U;
    request.kind = 0U;
    request.schema = slice_of("ANN");
    request.name = slice_of("RAISE_PAY");
    request.line = 4U;
    request.conditioned = true;
    request.condition = slice_of("PCT > 5");
    request.number = 1U;
    request.action = 1U;
    return trl_encode_debug(b, &request, e);
}

static bool debug_identify(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_IDENTIFY);
}

static bool debug_attach(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_ATTACH);
}

static bool debug_detach(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_DETACH);
}

static bool debug_break(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_BREAK);
}

static bool debug_clear(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_CLEAR);
}

static bool debug_breakpoints(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_BREAKPOINTS);
}

static bool debug_resume(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_RESUME);
}

static bool debug_interrupt(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_INTERRUPT);
}

static bool debug_stack(trl_buffer *b, trl_codec_error *e) {
    return debug_of(b, e, TRL_DEBUG_STACK);
}

bool compressed_query(trl_buffer *b, trl_codec_error *e) {
    static char sql[sizeof "SELECT '' FROM T" + COMPRESSED_XS];
    memcpy(sql, "SELECT '", sizeof "SELECT '" - 1U);
    memset(sql + sizeof "SELECT '" - 1U, 'x', COMPRESSED_XS);
    memcpy(sql + sizeof "SELECT '" - 1U + COMPRESSED_XS, "' FROM T", sizeof "' FROM T");
    trl_query_request q = {slice_of(sql), true, NULL, 0U, false};
    return trl_encode_query(b, &q, e);
}

const sample_request sample_requests[] = {
    {"hello-ping", hello_ping},
    {"hello-none", hello_none},
    {"hello-every", hello_every},
    {"hello-attribute-longest", hello_attribute_longest},
    {"hello-attributes-most", hello_attributes_most},
    {"hello-scram", hello_scram},
    {"answer", answer},
    {"answer-empty", answer_empty},
    {"cancel", cancel},
    {"query", query},
    {"query-empty", query_empty},
    {"query-attributes", query_attributes},
    {"query-multi", query_multi},
    {"prepare", prepare},
    {"execute", execute},
    {"execute-cursor", execute_cursor},
    {"fetch", fetch},
    {"fetch-cursor", fetch_cursor},
    {"close", close_statement},
    {"close-cursor", close_cursor},
    {"deallocate", deallocate},
    {"terminate", terminate},
    {"lob-read", lob_read},
    {"lob-create", lob_create},
    {"lob-append", lob_append},
    {"lob-free", lob_free},
    {"lob-hold", lob_hold},
    {"describe-statement", describe_statement},
    {"describe-cursor", describe_cursor},
    {"ping", ping},
    {"reset", reset},
    {"change-user", change_user},
    {"copy-in", copy_in},
    {"copy-in-every-column", copy_in_every_column},
    {"copy-data", copy_data},
    {"copy-columns", copy_columns},
    {"copy-end", copy_end},
    {"copy-end-abandon", copy_end_abandon},
    {"debug-identify", debug_identify},
    {"debug-attach", debug_attach},
    {"debug-detach", debug_detach},
    {"debug-break", debug_break},
    {"debug-clear", debug_clear},
    {"debug-breakpoints", debug_breakpoints},
    {"debug-resume", debug_resume},
    {"debug-interrupt", debug_interrupt},
    {"debug-stack", debug_stack},
};

const size_t sample_request_count = sizeof sample_requests / sizeof sample_requests[0];
