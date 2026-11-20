/**
 * @file base64_test.c
 * @brief Base 64 as SCRAM writes its binary fields [PROTOCOL Mechanisms]:
 * RFC 4648's vectors, and every malformed text refused.
 */
#include "auth/base64.h"
#include "harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Room for the longest vector, encoded or decoded. */
#define ROOM 16U

/**
 * Verifies: [PROTOCOL Mechanisms] RFC 4648's vectors encode to their text
 * and decode back, as SCRAM's salt, proof and signature are written.
 */
static void the_rfc_vectors_encode_and_decode(trl_test *test) {
    static const char *const bytes[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    static const char *const texts[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (size_t i = 0U; i < sizeof bytes / sizeof bytes[0]; ++i) {
        char text[ROOM];
        trl_base64_encode((const uint8_t *)bytes[i], strlen(bytes[i]), text);
        TRL_CHECK(test, strcmp(text, texts[i]) == 0);
        TRL_CHECK(test, trl_base64_length(strlen(bytes[i])) == strlen(texts[i]));
        uint8_t back[ROOM];
        size_t written = 0U;
        TRL_CHECK(test, trl_base64_decode(texts[i], strlen(texts[i]), back, &written));
        TRL_CHECK(test, written == strlen(bytes[i]) && memcmp(back, bytes[i], written) == 0);
    }
    static const uint8_t high[] = {0xff, 0xfe, 0x00, 0x3e, 0x3f};
    char text[ROOM];
    trl_base64_encode(high, sizeof high, text);
    uint8_t back[ROOM];
    size_t written = 0U;
    TRL_CHECK(test, trl_base64_decode(text, strlen(text), back, &written));
    TRL_CHECK(test, written == sizeof high && memcmp(back, high, sizeof high) == 0);
}

/**
 * Verifies: [PROTOCOL Mechanisms] a text not whole groups of four, with
 * padding but at the end or more than two, a character not of the
 * alphabet, or bits set past the last byte, is refused.
 */
static void a_malformed_text_is_refused(trl_test *test) {
    static const char *const texts[] = {"Zg=", "Zm9", "Zm,v", "Z===", "Zg==Zm9v", "Zm9v Zg==", "Zh==", "Zm9="};
    for (size_t i = 0U; i < sizeof texts / sizeof texts[0]; ++i) {
        uint8_t back[ROOM];
        size_t written = 0U;
        TRL_CHECK(test, !trl_base64_decode(texts[i], strlen(texts[i]), back, &written));
    }
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_rfc_vectors_encode_and_decode),
        TRL_TEST_CASE(a_malformed_text_is_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
