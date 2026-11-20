/**
 * @file samples.c
 * @brief The sample frames read from Turborel's file, a line at a time
 * [PROTOCOL Sample frames]: five fields apart by one tab each, a line
 * beginning with # a comment.
 */
#include "samples.h"

#include "harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The fields of a line, and the bits of a hexadecimal digit. */
#define FIELDS 5U
#define NIBBLE_BITS 4U
#define DECIMAL_DIGITS 10

/* A digit of upper-case hexadecimal's value, or -1. */
static int digit_of(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + DECIMAL_DIGITS;
    }
    return -1;
}

/* Reads the upper-case hexadecimal `hex`, of `length` digits, into the sample's frame. */
static bool frame_read(const char *hex, size_t length, sample *out) {
    if (length % 2U != 0U || length / 2U > sizeof out->frame) {
        return false;
    }
    for (size_t i = 0U; i < length; i += 2U) {
        int high = digit_of(hex[i]);
        int low = digit_of(hex[i + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        out->frame[i / 2U] = (uint8_t)(((unsigned int)high << NIBBLE_BITS) | (unsigned int)low);
    }
    out->length = length / 2U;
    return true;
}

/* Copies a field of `length` bytes into `out`, of SAMPLE_TEXT_BYTES; false if it does not fit. */
static bool field_copy(const char *field, size_t length, char *out) {
    if (length >= SAMPLE_TEXT_BYTES) {
        return false;
    }
    memcpy(out, field, length);
    out[length] = '\0';
    return true;
}

/* Reads a line of `length` bytes into `out`; false if it is not five fields as the page lays them out. */
static bool line_read(const char *line, size_t length, sample *out) {
    const char *starts[FIELDS];
    size_t lengths[FIELDS];
    size_t field = 0U;
    starts[0] = line;
    for (size_t i = 0U; i <= length; ++i) {
        if (i == length || line[i] == '\t') {
            if (field >= FIELDS) {
                return false;
            }
            lengths[field] = (size_t)(line + i - starts[field]);
            ++field;
            if (field < FIELDS) {
                starts[field] = line + i + 1;
            }
        }
    }
    bool client = lengths[0] == strlen("client") && memcmp(starts[0], "client", lengths[0]) == 0;
    bool server = lengths[0] == strlen("server") && memcmp(starts[0], "server", lengths[0]) == 0;
    out->from_client = client;
    return field == FIELDS && (client || server) && field_copy(starts[1], lengths[1], out->name) &&
           field_copy(starts[2], lengths[2], out->needs) && frame_read(starts[3], lengths[3], out) &&
           field_copy(starts[4], lengths[4], out->means);
}

bool samples_read(samples *out) {
    out->of = NULL;
    out->count = 0U;
    out->well_formed = true;
    char *text = NULL;
    if (!trl_test_read_file(TRL_SAMPLES_FILE, &text)) {
        return false;
    }
    out->of = calloc(SAMPLES_MOST, sizeof *out->of);
    for (char *line = text; out->of != NULL && *line != '\0';) {
        char *end = strchr(line, '\n');
        size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
        if (length > 0U && line[0] != '#') {
            bool room = out->count < SAMPLES_MOST;
            out->well_formed = out->well_formed && room && line_read(line, length, &out->of[out->count]);
            out->count += room ? 1U : 0U;
        }
        line += length + (end != NULL ? 1U : 0U);
    }
    free(text);
    return out->of != NULL;
}

void samples_free(samples *s) {
    free(s->of);
    s->of = NULL;
    s->count = 0U;
}

const sample *sample_named(const samples *s, const char *name) {
    for (size_t i = 0U; i < s->count; ++i) {
        if (strcmp(s->of[i].name, name) == 0) {
            return &s->of[i];
        }
    }
    return NULL;
}
