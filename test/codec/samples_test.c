/**
 * @file samples_test.c
 * @brief The sample frames read as the page lays them out, each frame a
 * client sends encoded byte for byte by the driver, and each compressed
 * frame inflated to the frame its meaning names [PROTOCOL Sample frames].
 */
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "fixture.h"
#include "harness.h"
#include "samples.h"
#include "samples_encoded.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The samples the file holds at the commit CONFORMANCE.md names, and how many of each side, and compressed. */
#define SAMPLES 147U
#define CLIENT_SAMPLES 38U
#define COMPRESSED_SAMPLES 2U
/* The rows of the compressed server sample. */
#define COMPRESSED_ROWS 64U

/* Whether `name` is a capability of the protocol's. */
static bool capability(const char *name) {
    static const char *const known[] = {
        TRL_CAPABILITY_NOTICES,
        TRL_CAPABILITY_STATUS,
        TRL_CAPABILITY_DIAGNOSTICS,
        TRL_CAPABILITY_DESCRIBE,
        TRL_CAPABILITY_CURSORS,
        TRL_CAPABILITY_PING,
        TRL_CAPABILITY_RESET,
        TRL_CAPABILITY_CHANGE_USER,
        TRL_CAPABILITY_QUERY_ATTRIBUTES,
        TRL_CAPABILITY_CONNECTION_ATTRIBUTES,
        TRL_CAPABILITY_MULTI_STATEMENT,
        TRL_CAPABILITY_COMPRESSION,
        TRL_CAPABILITY_COMMIT_POSITION,
        TRL_CAPABILITY_COPY,
        TRL_CAPABILITY_ROW_COUNTS,
        TRL_CAPABILITY_GENERATED_VALUES,
        TRL_CAPABILITY_FETCH_ENDS,
        TRL_CAPABILITY_CONDITIONS,
        TRL_CAPABILITY_DECLARED_TYPES,
    };
    for (size_t i = 0U; i < sizeof known / sizeof known[0]; ++i) {
        if (strcmp(known[i], name) == 0) {
            return true;
        }
    }
    return false;
}

/* Whether each capability a sample needs, apart by commas, or - for none, is one. */
static bool needs_known(const sample *s) {
    if (strcmp(s->needs, "-") == 0) {
        return true;
    }
    char needs[SAMPLE_TEXT_BYTES];
    memcpy(needs, s->needs, sizeof needs);
    bool known = true;
    for (char *at = needs; at != NULL && known;) {
        char *comma = strchr(at, ',');
        if (comma != NULL) {
            *comma = '\0';
        }
        known = capability(at);
        at = comma != NULL ? comma + 1 : NULL;
    }
    return known;
}

/* Whether the sample's header says its kind and the length of its body. */
static bool header_whole(const sample *s) {
    trl_frame_header header;
    return trl_frame_header_decode(s->frame, s->length, TRL_FRAME_MAX_BODY, &header) == TRL_FRAME_OK &&
           (size_t)header.length + TRL_FRAME_HEADER_BYTES == s->length;
}

/**
 * Verifies: [PROTOCOL Sample frames] every line but a comment is five
 * fields apart by tabs: client or server, a name unique in the file, the
 * capabilities it needs, each the protocol's, or -, the whole frame in
 * upper-case hexadecimal, its header's length its body's, and a meaning.
 */
static void every_line_is_read(trl_test *test) {
    samples s;
    TRL_CHECK(test, samples_read(&s) && s.well_formed);
    size_t clients = 0U;
    bool every = true;
    for (size_t i = 0U; i < s.count; ++i) {
        const sample *one = &s.of[i];
        clients += one->from_client ? 1U : 0U;
        every = every && sample_named(&s, one->name) == one && one->means[0] != '\0' && needs_known(one) &&
                header_whole(one);
    }
    TRL_CHECK(test, every);
    TRL_CHECK(test, s.count == SAMPLES && clients == CLIENT_SAMPLES);
    samples_free(&s);
}

/* Whether the request `r` encodes to its sample's frame, told on standard error if not. */
static bool encodes_to(const sample_request *r, const sample *s) {
    fixture f;
    fixture_init(&f);
    bool same = r->encode(&f.buffer, &f.error) && buffer_is(&f.buffer, s->frame, s->length);
    if (!same) {
        (void)fprintf(stderr, "%s (%s) is encoded otherwise\n", s->name, s->means);
    }
    fixture_free(&f);
    return same;
}

/**
 * Verifies: [PROTOCOL Sample frames] every frame a client sends, the
 * compressed one but, encoded by the driver's own requests byte for byte
 * as the file holds it [PROTOCOL Requests] [PROTOCOL The tail].
 */
static void every_client_frame_is_encoded(trl_test *test) {
    samples s;
    TRL_CHECK(test, samples_read(&s));
    size_t clients = 0U;
    size_t held = 0U;
    for (size_t i = 0U; i < s.count; ++i) {
        if (!s.of[i].from_client || s.of[i].frame[0] == TRL_KIND_COMPRESSED) {
            continue;
        }
        ++clients;
        const sample_request *r = NULL;
        for (size_t k = 0U; k < sample_request_count && r == NULL; ++k) {
            r = strcmp(sample_requests[k].name, s.of[i].name) == 0 ? &sample_requests[k] : NULL;
        }
        held += r != NULL && encodes_to(r, &s.of[i]) ? 1U : 0U;
    }
    TRL_CHECK(test, clients == sample_request_count && held == clients);
    samples_free(&s);
}

/* The kind and length a compressed sample's meaning says it holds: "kind B, length 3972". */
static bool held_of(const sample *s, uint8_t *kind, size_t *length) {
    const char *at = strstr(s->means, "kind ");
    if (at == NULL || at[sizeof "kind " - 1U] == '\0') {
        return false;
    }
    *kind = (uint8_t)at[sizeof "kind " - 1U];
    const char *of = strstr(at, "length ");
    *length = of != NULL ? (size_t)strtoul(of + sizeof "length " - 1U, NULL, 10) : 0U;
    return of != NULL;
}

/* Whether the compressed client sample's frame is the Query the driver encodes, and the driver's own compressed one
 * holds it too. */
static bool query_inflated(const fixture *f, uint8_t kind) {
    fixture q;
    fixture_init(&q);
    bool same = kind == TRL_KIND_QUERY && compressed_query(&q.buffer, &q.error) &&
                q.buffer.length == f->buffer.length + TRL_FRAME_HEADER_BYTES &&
                memcmp(q.buffer.data + TRL_FRAME_HEADER_BYTES, f->buffer.data, f->buffer.length) == 0;
    trl_buffer ours;
    trl_buffer_init(&ours);
    trl_slice frame = {q.buffer.data, q.buffer.length};
    uint8_t held = 0U;
    trl_buffer inflated;
    trl_buffer_init(&inflated);
    same = same && trl_compress_frames(&ours, frame, TRL_COMPRESS_THRESHOLD, &q.error) &&
           ours.data[0] == TRL_KIND_COMPRESSED;
    trl_slice body = {ours.data + TRL_FRAME_HEADER_BYTES, ours.length - TRL_FRAME_HEADER_BYTES};
    same = same && trl_decompress_frame(body, TRL_FRAME_MAX_BODY, &held, &inflated, &q.error) &&
           held == TRL_KIND_QUERY && buffer_is(&inflated, q.buffer.data + TRL_FRAME_HEADER_BYTES, f->buffer.length);
    trl_buffer_free(&inflated);
    trl_buffer_free(&ours);
    fixture_free(&q);
    return same;
}

/* Whether the compressed server sample's frame decodes to its 64 rows. */
static bool rows_inflated(fixture *f, uint8_t kind) {
    trl_reply reply;
    trl_slice body = {f->buffer.data, f->buffer.length};
    return kind == TRL_KIND_ROWS && trl_decode_reply(kind, body, &f->arena, &reply, &f->error) &&
           reply.as.rows.count == COMPRESSED_ROWS;
}

/* Whether a compressed sample inflates to the kind and length its meaning names, and holds what it says. */
static bool inflated(const sample *s) {
    fixture f;
    fixture_init(&f);
    uint8_t kind = 0U;
    uint8_t named = 0U;
    size_t length = 0U;
    trl_slice body = {s->frame + TRL_FRAME_HEADER_BYTES, s->length - TRL_FRAME_HEADER_BYTES};
    bool held = held_of(s, &named, &length) &&
                trl_decompress_frame(body, TRL_FRAME_MAX_BODY, &kind, &f.buffer, &f.error) && kind == named &&
                f.buffer.length == length && (s->from_client ? query_inflated(&f, kind) : rows_inflated(&f, kind));
    if (!held) {
        (void)fprintf(stderr, "%s (%s) is inflated otherwise\n", s->name, s->means);
    }
    fixture_free(&f);
    return held;
}

/**
 * Verifies: [PROTOCOL Sample frames] each compressed sample inflated to
 * the kind and length its meaning names: the server's to its Rows, the
 * client's to the Query the driver encodes; the driver's own compressed
 * Query, of another zlib stream, inflated to the same frame
 * [PROTOCOL Compressed frames].
 */
static void every_compressed_frame_is_inflated(trl_test *test) {
    if (!trl_compression_built()) {
        return;
    }
    samples s;
    TRL_CHECK(test, samples_read(&s));
    size_t compressed = 0U;
    bool every = true;
    for (size_t i = 0U; i < s.count; ++i) {
        if (s.of[i].frame[0] == TRL_KIND_COMPRESSED) {
            ++compressed;
            every = every && inflated(&s.of[i]);
        }
    }
    TRL_CHECK(test, every && compressed == COMPRESSED_SAMPLES);
    samples_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(every_line_is_read),
        TRL_TEST_CASE(every_client_frame_is_encoded),
        TRL_TEST_CASE(every_compressed_frame_is_inflated),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
