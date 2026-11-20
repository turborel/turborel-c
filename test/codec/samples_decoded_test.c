/**
 * @file samples_decoded_test.c
 * @brief Every frame a server sends among the sample frames decoded to
 * what its meaning says [PROTOCOL Sample frames]: each written as text,
 * every field it holds, and held to the text its meaning gives.
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/compress.h"
#include "codec/fields.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/values.h"
#include "data/params.h"
#include "fixture.h"
#include "harness.h"
#include "samples.h"
#ifdef TRL_FAULTS
#include "faults.h"
#endif

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

/* Copies a Rows reply's values into `arena`, row by row; whether it could. */
static bool rows_copied(trl_arena *arena, const trl_reply *in, trl_reply *out) {
    out->as.rows.rows = trl_arena_alloc(arena, in->as.rows.count + 1U, sizeof(trl_row));
    for (uint32_t r = 0U; out->as.rows.rows != NULL && r < in->as.rows.count; ++r) {
        const trl_row *row = &in->as.rows.rows[r];
        trl_row *copy = &out->as.rows.rows[r];
        copy->count = row->count;
        copy->values = trl_arena_alloc(arena, row->count + 1U, sizeof(trl_value));
        for (uint32_t v = 0U; copy->values != NULL && v < row->count; ++v) {
            if (!trl_value_copy(arena, &row->values[v], &copy->values[v], 0U)) {
                return false;
            }
        }
        if (copy->values == NULL) {
            return false;
        }
    }
    return out->as.rows.rows != NULL;
}

/* Copies a Prepared reply's parameters' types and its columns into `arena`; whether it could. */
static bool prepared_copied(trl_arena *arena, const trl_reply *in, trl_reply *out) {
    uint32_t count = in->as.prepared.parameter_count;
    out->as.prepared.parameters = trl_arena_alloc(arena, count + 1U, sizeof(trl_parameter));
    for (uint32_t i = 0U; out->as.prepared.parameters != NULL && i < count; ++i) {
        out->as.prepared.parameters[i].varying = in->as.prepared.parameters[i].varying;
        if (!trl_type_copy(arena, &in->as.prepared.parameters[i].type, &out->as.prepared.parameters[i].type, 0U)) {
            return false;
        }
    }
    out->as.prepared.columns = trl_columns_copy(arena, in->as.prepared.columns, in->as.prepared.column_count);
    return out->as.prepared.parameters != NULL && out->as.prepared.columns != NULL;
}

/* A copy of `in` into `arena`, of a reply of values or types; whether it could. */
static bool reply_copied(trl_arena *arena, const trl_reply *in, trl_reply *out) {
    *out = *in;
    switch (in->kind) {
    case TRL_KIND_ROWS:
        return rows_copied(arena, in, out);
    case TRL_KIND_DESCRIPTION:
        out->as.description.columns = trl_columns_copy(arena, in->as.description.columns, in->as.description.count);
        return out->as.description.columns != NULL;
    case TRL_KIND_PREPARED:
        return prepared_copied(arena, in, out);
    default:
        return true;
    }
}

/* Whether a reply of `kind` holds values or types, which a result kept copies. */
static bool of_values(uint8_t kind) {
    return kind == TRL_KIND_ROWS || kind == TRL_KIND_DESCRIPTION || kind == TRL_KIND_PREPARED;
}

/* Whether the server sample `s` copied whole reads as the reply decoded. */
static bool copies_whole(const sample *s) {
    fixture f;
    fixture_init(&f);
    trl_arena copies;
    trl_arena_init(&copies);
    trl_reply reply;
    trl_reply copy;
    char decoded_text[REPLY_TEXT_BYTES] = "";
    char copied_text[REPLY_TEXT_BYTES] = "";
    bool read = decoded(&f, s, &reply) && reply_copied(&copies, &reply, &copy);
    if (read) {
        reply_text(&reply, decoded_text, sizeof decoded_text);
        /* The decoded reply's arena freed first, so the copy holds nothing of it. */
        trl_arena_free(&f.arena);
        reply_text(&copy, copied_text, sizeof copied_text);
    }
    bool same = read && strcmp(decoded_text, copied_text) == 0;
    if (!same) {
        (void)fprintf(stderr, "%s copied:\n  %s\n  %s\n", s->name, decoded_text, copied_text);
    }
    trl_arena_free(&copies);
    fixture_free(&f);
    return same;
}

/**
 * Verifies: [PROTOCOL Values] [PROTOCOL Data types] every value and type
 * a server frame holds, each tag of each, copied whole, as a result kept
 * past its reply copies them, reads as it was decoded.
 */
static void every_server_frame_is_copied_whole(trl_test *test) {
    samples s;
    TRL_CHECK(test, samples_read(&s));
    bool every = true;
    for (size_t i = 0U; i < s.count; ++i) {
        bool unread = s.of[i].frame[0] == TRL_KIND_COMPRESSED && !trl_compression_built();
        bool copied = !s.of[i].from_client && !unread && of_values(s.of[i].frame[0]);
        every = every && (!copied || copies_whole(&s.of[i]));
    }
    TRL_CHECK(test, every);
    samples_free(&s);
}

#ifdef TRL_FAULTS
/* The samples the faulted copies are of: each kind of type and value that
   holds others, or names. Read once, before any allocation fails. */
static samples faulted;

static const char *const rich[] = {
    "prepared",
    "prepared-declared",
    "description-declared",
    "description-row",
    "description-array",
    "description-structured",
    "description-reference-scoped",
    "description-xml-schema",
    "description-plugin-type",
    "value-row",
    "value-array",
    "value-multiset",
    "value-structured",
    "value-plugin",
    "value-json",
    "value-lob-character",
};

/* Decodes each rich sample and copies it, each allocation able to fail. */
static bool copy_each(void) {
    for (size_t i = 0U; i < sizeof rich / sizeof rich[0]; ++i) {
        const sample *one = sample_named(&faulted, rich[i]);
        fixture f;
        fixture_init(&f);
        trl_arena copies;
        trl_arena_init(&copies);
        trl_reply reply;
        trl_reply copy;
        if (one != NULL && decoded(&f, one, &reply)) {
            (void)reply_copied(&copies, &reply, &copy);
        }
        trl_arena_free(&copies);
        fixture_free(&f);
    }
    return true;
}

/**
 * Verifies: [TOOL] R-MEM-2 each allocation of decoding and copying a
 * reply of every kind of type and value that holds others failing in
 * turn fails the copy, leaking and crashing on nothing.
 */
static void copying_survives_each_fault(trl_test *test) {
    TRL_CHECK(test, samples_read(&faulted));
    TRL_CHECK(test, every_fault(copy_each));
    samples_free(&faulted);
}
#endif

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(every_server_frame_is_decoded),
        TRL_TEST_CASE(every_server_frame_is_copied_whole),
#ifdef TRL_FAULTS
        TRL_TEST_CASE(copying_survives_each_fault),
#endif
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
