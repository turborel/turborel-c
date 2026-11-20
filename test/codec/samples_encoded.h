/**
 * @file samples_encoded.h
 * @brief The request of each frame a client sends among the sample
 * frames, made by the driver's own encoders [PROTOCOL Sample frames].
 */
#ifndef TRL_TEST_CODEC_SAMPLES_ENCODED_H
#define TRL_TEST_CODEC_SAMPLES_ENCODED_H

#include "codec/buffer.h"
#include "codec/error.h"

#include <stdbool.h>
#include <stddef.h>

/** The x's of the Query the compressed client sample holds. */
#define COMPRESSED_XS 2000U

/** A client sample's name, and the encoder of the request its meaning gives. */
typedef struct sample_request {
    const char *name;
    bool (*encode)(trl_buffer *buffer, trl_codec_error *error);
} sample_request;

/** The request of each client sample but the compressed one. */
extern const sample_request sample_requests[];

/** How many. */
extern const size_t sample_request_count;

/** Encodes the Query the compressed client sample holds: SELECT 'x...x' FROM T, of COMPRESSED_XS x's, ends 1. */
bool compressed_query(trl_buffer *buffer, trl_codec_error *error);

#endif
