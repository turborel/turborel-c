/**
 * @file frame_fuzz.c
 * @brief libFuzzer target of the codec (RULES.md D4): any bytes read as a
 * frame header.
 *
 * A header read is written back and must give the same bytes; one refused
 * must be refused for being short or too long, never anything else; else
 * the target traps, which libFuzzer reports as a crash. The sanitizers it
 * is built with make any read out of bounds or undefined behaviour a crash
 * too. The codec of fields and messages joins it as it is written.
 */
#include "codec/frame.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* libFuzzer calls this function by this name, from outside the file
   (R-LINT-3). */
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    trl_frame_header header = {0U, 0U};
    trl_frame_result read = trl_frame_header_decode(data, size, TRL_FRAME_MAX_BODY, &header);
    if (read == TRL_FRAME_OK) {
        uint8_t again[TRL_FRAME_HEADER_BYTES];
        if (trl_frame_header_encode(header, again, sizeof again) != TRL_FRAME_OK ||
            memcmp(again, data, sizeof again) != 0) {
            __builtin_trap();
        }
    } else if (read != TRL_FRAME_SHORT && read != TRL_FRAME_TOO_LONG) {
        __builtin_trap();
    }
    return 0;
}
