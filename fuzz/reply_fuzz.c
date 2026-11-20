/**
 * @file reply_fuzz.c
 * @brief libFuzzer target of the reply decoder (RULES.md D4): the first
 * byte a reply's kind, the rest its body.
 *
 * Every input decodes as a reply or fails the connection (-919) or as
 * memory not had (-918); anything else traps, which libFuzzer reports as a
 * crash, and the sanitizers make any read out of bounds a crash too. The
 * arena is bounded, so the fuzzer finds no reply that holds memory without
 * bound.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/replies.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The most memory one decoded reply may hold here. */
#define TRL_FUZZ_ARENA_BYTES ((size_t)64U * 1024U * 1024U)

/* libFuzzer calls this function by this name, from outside the file
   (R-LINT-3). */
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0U) {
        return 0;
    }
    trl_arena arena;
    trl_arena_init(&arena);
    arena.limit = TRL_FUZZ_ARENA_BYTES;
    trl_codec_error error;
    memset(&error, 0, sizeof error);
    trl_reply reply;
    trl_slice body = {data + 1, size - 1U};
    bool decoded = trl_decode_reply(data[0], body, &arena, &reply, &error);
    trl_arena_free(&arena);
    if (!decoded && error.sqlcode != TRL_SQLCODE_CONNECTION && error.sqlcode != TRL_SQLCODE_RESOURCES) {
        __builtin_trap();
    }
    return 0;
}
