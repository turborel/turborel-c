/**
 * @file samples.h
 * @brief What the sample frames' tests share (RULES.md R-TEST-7):
 * Turborel's `doc/client/05-protocol-frames.tsv`, copied at the commit
 * CONFORMANCE.md names, read line by line [PROTOCOL Sample frames]; and a
 * reply written as text, each field it holds, to compare with what a
 * sample's meaning says.
 */
#ifndef TRL_TEST_CODEC_SAMPLES_H
#define TRL_TEST_CODEC_SAMPLES_H

#include "codec/replies.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** The most samples the file holds, and the bytes of a frame's most. */
#define SAMPLES_MOST 256U
#define SAMPLE_FRAME_MOST 4096U
/** The bytes of a sample's name, capabilities and meaning. */
#define SAMPLE_TEXT_BYTES 256U
/** The bytes a reply's text is given. */
#define REPLY_TEXT_BYTES 4096U

/** A line of the file: who sends the frame, its name, what it needs, the frame and what it means. */
typedef struct sample {
    bool from_client;
    char name[SAMPLE_TEXT_BYTES];
    char needs[SAMPLE_TEXT_BYTES];
    uint8_t frame[SAMPLE_FRAME_MOST];
    size_t length;
    char means[SAMPLE_TEXT_BYTES];
} sample;

/** Every sample of the file, in its order. */
typedef struct samples {
    sample *of;
    size_t count;
    /** Whether every line but a comment was five fields, its hex upper case and whole. */
    bool well_formed;
} samples;

/** Reads the file; false if it cannot be read. Frees with samples_free(). */
bool samples_read(samples *out);

/** Frees what samples_read() made. */
void samples_free(samples *s);

/** The sample of `name`, or null. */
const sample *sample_named(const samples *s, const char *name);

/** A server sample's name, and its reply as reply_text() writes it, as the sample's meaning says. */
typedef struct sample_reply {
    const char *name;
    const char *text;
} sample_reply;

/** The reply of each server sample. */
extern const sample_reply sample_replies[];

/** How many. */
extern const size_t sample_reply_count;

/** Writes `reply`, of the kind it is, as text into `out`: every field it holds. */
void reply_text(const trl_reply *reply, char *out, size_t capacity);

#endif
