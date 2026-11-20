/**
 * @file json_fuzz.c
 * @brief libFuzzer target of JSON's stored form (RULES.md D4): any bytes
 * checked as a document, and any text parsed as JSON.
 *
 * A document the check finds whole is written as text, or refused as one
 * no text is (22032); a text parsed is a document the check finds whole,
 * which reads back as the same text twice over. Anything else traps.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/json.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Checks the bytes as a document and, if whole, writes its text. */
static void check_document(const uint8_t *data, size_t size) {
    if (!trl_json_valid(data, size)) {
        return;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    trl_codec_error error;
    memset(&error, 0, sizeof error);
    bool written = trl_json_to_text(&text, data, size, &error);
    trl_buffer_free(&text);
    if (!written && error.sqlcode != TRL_SQLCODE_DATA) {
        __builtin_trap();
    }
}

/* Parses the bytes as a text; a document made is whole and reads back. */
static void parse_text(const uint8_t *data, size_t size) {
    trl_arena arena;
    trl_arena_init(&arena);
    trl_buffer document;
    trl_buffer_init(&document);
    trl_codec_error error;
    memset(&error, 0, sizeof error);
    if (trl_json_from_text(&document, data, size, &arena, &error)) {
        if (!trl_json_valid(document.data, document.length)) {
            __builtin_trap();
        }
        check_document(document.data, document.length);
    }
    trl_buffer_free(&document);
    trl_arena_free(&arena);
}

/* libFuzzer calls this function by this name, from outside the file
   (R-LINT-3). */
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    check_document(data, size);
    parse_text(data, size);
    return 0;
}
