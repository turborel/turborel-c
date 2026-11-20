/**
 * @file sweep_test.c
 * @brief Every reply, with every data type and value in it, decoded whole,
 * then cut at each byte, lengthened by one, and each byte flipped: a
 * malformed reply fails the connection, never anything worse
 * [PROTOCOL Errors and notices].
 */
#include "codec/buffer.h"
#include "codec/replies.h"
#include "corpus.h"
#include "fixture.h"
#include "harness.h"

#include <stdint.h>
#include <string.h>

/* Whether decoding `length` bytes of `body` succeeds or fails the
   connection, nothing else. */
static bool decodes_or_fails(fixture *f, uint8_t kind, const uint8_t *body, size_t length) {
    fixture_reset(f);
    trl_reply reply;
    return decode_bytes(f, kind, body, length, &reply) || failed_connection(f) || failed_value(f);
}

/**
 * Verifies: [PROTOCOL Replies] a reply of every kind, with every data type
 * and value, is decoded whole.
 */
static void every_reply_decodes(trl_test *test) {
    sample samples[SAMPLES];
    size_t count = corpus(samples);
    fixture f;
    fixture_init(&f);
    for (size_t i = 0U; i < count; ++i) {
        fixture_reset(&f);
        trl_reply reply;
        TRL_CHECK(test, decode_bytes(&f, samples[i].kind, samples[i].body.data, samples[i].body.length, &reply));
    }
    fixture_free(&f);
    free_corpus(samples);
}

/**
 * Verifies: [PROTOCOL Errors and notices] each reply cut at each byte, or
 * with a byte after its end, decodes as a reply that ends there or fails
 * the connection; one cut inside its first form always fails.
 */
static void cut_and_lengthened_replies_fail(trl_test *test) {
    sample samples[SAMPLES];
    size_t count = corpus(samples);
    fixture f;
    fixture_init(&f);
    for (size_t i = 0U; i < count; ++i) {
        const trl_buffer *body = &samples[i].body;
        for (size_t cut = 0U; cut < body->length; ++cut) {
            TRL_CHECK(test, decodes_or_fails(&f, samples[i].kind, body->data, cut));
        }
        uint8_t longer[2048];
        TRL_CHECK(test, body->length < sizeof longer);
        memcpy(longer, body->data, body->length);
        longer[body->length] = 0U;
        fixture_reset(&f);
        trl_reply reply;
        TRL_CHECK(test, !decode_bytes(&f, samples[i].kind, longer, body->length + 1U, &reply) && failed_connection(&f));
    }
    fixture_reset(&f);
    trl_reply reply;
    TRL_CHECK(test, !decode_bytes(&f, 'B', samples[1].body.data, 5U, &reply) && failed_connection(&f));
    fixture_free(&f);
    free_corpus(samples);
}

/**
 * Verifies: [PROTOCOL Errors and notices] each byte of each reply flipped
 * decodes as some reply or fails the connection, never anything else.
 */
static void flipped_replies_fail(trl_test *test) {
    sample samples[SAMPLES];
    size_t count = corpus(samples);
    fixture f;
    fixture_init(&f);
    for (size_t i = 0U; i < count; ++i) {
        const trl_buffer *body = &samples[i].body;
        uint8_t changed[2048];
        for (size_t at = 0U; at < body->length && body->length <= sizeof changed; ++at) {
            memcpy(changed, body->data, body->length);
            changed[at] ^= 0xFFU;
            TRL_CHECK(test, decodes_or_fails(&f, samples[i].kind, changed, body->length));
            changed[at] ^= 0xFEU;
            TRL_CHECK(test, decodes_or_fails(&f, samples[i].kind, changed, body->length));
        }
    }
    fixture_free(&f);
    free_corpus(samples);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(every_reply_decodes),
        TRL_TEST_CASE(cut_and_lengthened_replies_fail),
        TRL_TEST_CASE(flipped_replies_fail),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
