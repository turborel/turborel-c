/**
 * @file samples_decoded_test.c
 * @brief Every frame a server sends among the sample frames decoded to
 * what its meaning says [PROTOCOL Sample frames]: each written as text,
 * every field it holds, and held to the text its meaning gives.
 */
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "fixture.h"
#include "harness.h"
#include "samples.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Decodes the sample's frame, inflated if it is compressed, into `out`. */
static bool decoded(fixture *f, const sample *s, trl_reply *out) {
    trl_frame_header header;
    if (trl_frame_header_decode(s->frame, s->length, TRL_FRAME_MAX_BODY, &header) != TRL_FRAME_OK ||
        (size_t)header.length + TRL_FRAME_HEADER_BYTES != s->length) {
        return false;
    }
    trl_slice body = {s->frame + TRL_FRAME_HEADER_BYTES, header.length};
    uint8_t kind = header.kind;
    if (kind == TRL_KIND_COMPRESSED) {
        if (!trl_decompress_frame(body, TRL_FRAME_MAX_BODY, &kind, &f->buffer, &f->error)) {
            return false;
        }
        body.bytes = f->buffer.data;
        body.length = f->buffer.length;
    }
    return trl_decode_reply(kind, body, &f->arena, out, &f->error);
}

/* Whether the server sample `s` decodes to the text `want`, told on standard error if not. */
static bool decodes_to(const sample *s, const char *want) {
    fixture f;
    fixture_init(&f);
    trl_reply reply;
    char got[REPLY_TEXT_BYTES] = "";
    bool read = decoded(&f, s, &reply);
    if (read) {
        reply_text(&reply, got, sizeof got);
    }
    bool same = read && want != NULL && strcmp(got, want) == 0;
    if (!same) {
        (void)fprintf(stderr, "%s (%s):\n  got  %s\n  want %s\n", s->name, s->means, read ? got : "(not decoded)",
                      want != NULL ? want : "(none)");
    }
    fixture_free(&f);
    return same;
}

/**
 * Verifies: [PROTOCOL Sample frames] every frame a server sends read as a
 * frame, inflated if compressed where the library has zlib, and decoded
 * to the reply its meaning gives, every field of it, whatever
 * capabilities it needs; and no reply expected of a sample the file lacks.
 */
static void every_server_frame_is_decoded(trl_test *test) {
    samples s;
    TRL_CHECK(test, samples_read(&s));
    size_t servers = 0U;
    size_t held = 0U;
    for (size_t i = 0U; i < s.count; ++i) {
        if (s.of[i].from_client) {
            continue;
        }
        ++servers;
        const char *want = NULL;
        for (size_t e = 0U; e < sample_reply_count && want == NULL; ++e) {
            want = strcmp(sample_replies[e].name, s.of[i].name) == 0 ? sample_replies[e].text : NULL;
        }
        /* A library built without zlib cannot read a compressed frame; the
           frame it holds is decoded as every other's is. */
        bool unread = s.of[i].frame[0] == TRL_KIND_COMPRESSED && !trl_compression_built();
        held += unread || decodes_to(&s.of[i], want) ? 1U : 0U;
    }
    TRL_CHECK(test, servers == sample_reply_count && held == servers);
    samples_free(&s);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(every_server_frame_is_decoded),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
