/**
 * @file frame.c
 * @brief The frame header's encoding and decoding [PROTOCOL Frames].
 *
 * The header is the kind, one byte, then the body's length as a u32,
 * least significant byte first, as every integer of the protocol is
 * [PROTOCOL Values and fields].
 */
#include "codec/frame.h"

#include <stddef.h>
#include <stdint.h>

/* Where the length begins in the header, after the kind's byte. */
#define TRL_FRAME_LENGTH_AT 1U
/* The bytes of the length, a u32. */
#define TRL_FRAME_LENGTH_BYTES 4U
/* The bits of one byte, the step between a u32's bytes. */
#define TRL_BITS_PER_BYTE 8U
/* The low byte of a wider integer. */
#define TRL_LOW_BYTE 0xFFU

trl_frame_result trl_frame_header_encode(trl_frame_header header, uint8_t *out, size_t capacity) {
    if (out == NULL || capacity < TRL_FRAME_HEADER_BYTES) {
        return TRL_FRAME_SHORT;
    }
    out[0] = header.kind;
    for (unsigned int i = 0U; i < TRL_FRAME_LENGTH_BYTES; ++i) {
        out[TRL_FRAME_LENGTH_AT + i] = (uint8_t)((header.length >> (i * TRL_BITS_PER_BYTE)) & TRL_LOW_BYTE);
    }
    return TRL_FRAME_OK;
}

trl_frame_result trl_frame_header_decode(const uint8_t *in, size_t available, uint32_t max_body,
                                         trl_frame_header *out) {
    if (in == NULL || out == NULL || available < TRL_FRAME_HEADER_BYTES) {
        return TRL_FRAME_SHORT;
    }
    uint32_t length = 0U;
    for (unsigned int i = 0U; i < TRL_FRAME_LENGTH_BYTES; ++i) {
        length |= (uint32_t)in[TRL_FRAME_LENGTH_AT + i] << (i * TRL_BITS_PER_BYTE);
    }
    if (length > max_body) {
        return TRL_FRAME_TOO_LONG;
    }
    out->kind = in[0];
    out->length = length;
    return TRL_FRAME_OK;
}
