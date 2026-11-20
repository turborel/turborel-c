/**
 * @file corpus.h
 * @brief A reply of every kind, with every data type and value in it
 * (RULES.md R-TEST-7).
 */
#ifndef TRL_TEST_CODEC_CORPUS_H
#define TRL_TEST_CODEC_CORPUS_H

#include "codec/buffer.h"

#include <stddef.h>
#include <stdint.h>

/** The most samples the corpus holds. */
#define SAMPLES 16U

/** A reply of the corpus: its kind and body. */
typedef struct sample {
    uint8_t kind;
    trl_buffer body;
} sample;

/** Fills the corpus; gives how many samples it holds. */
size_t corpus(sample *samples);

/** Frees the corpus. */
void free_corpus(sample *samples);

#endif
