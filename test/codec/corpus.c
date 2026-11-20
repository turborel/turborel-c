/**
 * @file corpus.c
 * @brief A reply of every kind, with every data type and value in it, for
 * the tests that decode them whole, damaged or with memory bounded
 * (RULES.md R-TEST-7).
 */
#include "corpus.h"

#include "codec/buffer.h"
#include "codec/json_nodes.h"
#include "codec/tags.h"
#include "fixture.h"
#include "turborel/turborel.h"

#include <stdint.h>
#include <string.h>

/* Appends a schema and a name. */
static void put_name(trl_buffer *b, const char *schema, const char *name) {
    trl_put_bytes(b, text_of(schema));
    trl_put_bytes(b, text_of(name));
}

/* Appends the columns of the types that nest or name others. */
static void put_constructed_types(trl_buffer *b) {
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_ARRAY);
    trl_put_u32(b, 3U);
    trl_put_u8(b, TRL_TAG_ROW);
    trl_put_u32(b, 1U);
    trl_put_bytes(b, text_of("F"));
    trl_put_u8(b, TRL_TAG_MULTISET);
    trl_put_u8(b, TRL_TAG_PLUGIN);
    trl_put_bytes(b, text_of("P"));
    trl_put_u32(b, 1U);
    trl_put_u32(b, 7U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_REFERENCE);
    put_name(b, "S", "T");
    trl_put_u32(b, 1U);
    put_name(b, "S", "U");
    trl_put_u8(b, 1U);
    put_name(b, "S", "V");
    trl_put_u8(b, 0U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_XML);
    trl_put_u8(b, TRL_XML_CONTENT);
    trl_put_u8(b, TRL_XML_XMLSCHEMA);
    put_name(b, "S", "X");
    trl_put_u8(b, 1U);
    trl_put_bytes(b, text_of("urn:x"));
    trl_put_u8(b, 1U);
    trl_put_bytes(b, text_of("e"));
    trl_put_u8(b, 0U);
}

/* Appends every data type, as columns of a Description. */
static void put_types(trl_buffer *b) {
    const uint8_t sized[] = {
        TRL_TAG_EXACT,  TRL_TAG_APPROXIMATE, TRL_TAG_DATE, TRL_TAG_TIME_ZONED, TRL_TAG_DECFLOAT,
        TRL_TAG_BINARY, TRL_TAG_BOOLEAN,     TRL_TAG_JSON, TRL_TAG_UUID,
    };
    trl_put_u32(b, 15U);
    for (size_t i = 0U; i < sizeof sized; ++i) {
        trl_put_u8(b, 1U);
        trl_put_bytes(b, text_of("C"));
        trl_put_u8(b, sized[i]);
        if (sized[i] == TRL_TAG_EXACT) {
            trl_put_u32(b, 18U);
        }
        if (sized[i] != TRL_TAG_BOOLEAN && sized[i] != TRL_TAG_JSON && sized[i] != TRL_TAG_UUID) {
            trl_put_u32(b, 4U);
        }
        trl_put_u8(b, 0U);
    }
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_VARYING);
    trl_put_u32(b, 10U);
    trl_put_u8(b, 1U);
    trl_put_u8(b, 1U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_CLOB);
    trl_put_u64(b, 99U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, 0U);
    trl_put_u8(b, TRL_TAG_INTERVAL);
    for (unsigned int i = 0U; i < 4U; ++i) {
        trl_put_u32(b, i);
    }
    trl_put_u8(b, 0U);
    put_constructed_types(b);
}

/* Appends the scalar values of a row. */
static void put_scalars(trl_buffer *b) {
    trl_put_u8(b, TRL_TAG_NULL);
    trl_put_u8(b, TRL_TAG_CHARACTER);
    trl_put_bytes(b, text_of("abc"));
    trl_put_u8(b, TRL_TAG_EXACT);
    trl_put_i128(b, (trl_int128){125U, 0});
    trl_put_u32(b, 2U);
    trl_put_u8(b, TRL_TAG_APPROXIMATE);
    trl_put_u64(b, 0x3FF0000000000000U);
    trl_put_u8(b, TRL_TAG_PLUGIN);
    trl_put_bytes(b, text_of("P"));
    trl_put_bytes(b, text_of("t"));
    trl_put_bytes(b, text_of("b"));
    trl_put_u8(b, TRL_TAG_TIMESTAMP);
    trl_put_u8(b, 1U);
    trl_put_u8(b, 6U);
    trl_put_u32(b, 60U);
    trl_put_u64(b, 1U);
    trl_put_u8(b, TRL_TAG_INTERVAL);
    for (unsigned int i = 0U; i < 4U; ++i) {
        trl_put_u32(b, i);
    }
    trl_put_i128(b, (trl_int128){5U, 0});
    trl_put_u8(b, TRL_TAG_BOOLEAN);
    trl_put_u8(b, 1U);
    trl_put_u8(b, TRL_TAG_DECFLOAT);
    trl_put_i128(b, (trl_int128){15U, 0});
    trl_put_u32(b, 0xFFFFFFFFU);
}

/* Appends the values whose bodies are bytes, and the large objects. */
static void put_octets(trl_buffer *b) {
    const uint8_t json[] = {1, TRL_JSON_ARRAY, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, TRL_JSON_TRUE};
    uint8_t uuid[16];
    memset(uuid, 3, sizeof uuid);
    trl_put_u8(b, TRL_TAG_BINARY);
    trl_put_bytes(b, text_of("\x01\x02"));
    trl_put_u8(b, TRL_TAG_REFERENCE);
    trl_put_bytes(b, text_of("r"));
    trl_put_u8(b, TRL_TAG_JSON);
    trl_put_bytes(b, bytes_of(json, sizeof json));
    trl_put_u8(b, TRL_TAG_XML);
    trl_put_bytes(b, text_of("<a/>"));
    trl_put_u8(b, TRL_TAG_UUID);
    trl_put_bytes(b, bytes_of(uuid, sizeof uuid));
    trl_put_u8(b, TRL_TAG_LOB_WHOLE);
    trl_put_u8(b, TRL_LOB_CHARACTER);
    trl_put_bytes(b, text_of("tea"));
    trl_put_u8(b, TRL_TAG_LOB_LOCATOR);
    trl_put_u8(b, TRL_LOB_BINARY);
    trl_put_u32(b, 7U);
    trl_put_u64(b, 100000U);
    trl_put_u64(b, 0U);
}

/* Appends the constructed values: an array of a multiset of a row, and a
   structured value. */
static void put_constructed(trl_buffer *b) {
    trl_put_u8(b, TRL_TAG_ARRAY);
    trl_put_u32(b, 1U);
    trl_put_u8(b, TRL_TAG_MULTISET);
    trl_put_u32(b, 1U);
    trl_put_u8(b, TRL_TAG_ROW);
    trl_put_u32(b, 1U);
    trl_put_u8(b, TRL_TAG_NULL);
    trl_put_u8(b, TRL_TAG_STRUCTURED);
    put_name(b, "S", "T");
    trl_put_u32(b, 1U);
    trl_put_u8(b, TRL_TAG_BOOLEAN);
    trl_put_u8(b, 0U);
}

/* Appends a failure with every diagnostic. */
static void put_failure(trl_buffer *b) {
    trl_put_u32(b, (uint32_t)-919);
    trl_put_bytes(b, text_of("08006"));
    trl_put_bytes(b, text_of("gone"));
    trl_put_u8(b, 1U);
    trl_put_bytes(b, text_of("rule"));
    trl_put_u8(b, 1U);
    trl_put_u64(b, 3U);
    trl_put_u8(b, 1U);
    for (uint8_t tag = 7U; tag <= 15U; ++tag) {
        trl_put_u8(b, tag);
        trl_put_bytes(b, text_of("d"));
    }
}

/* Appends a Complete with every field of its tail. */
static void put_complete(trl_buffer *b) {
    trl_put_u32(b, 0U);
    trl_put_bytes(b, text_of("00000"));
    trl_put_u8(b, 1U);
    trl_put_u8(b, 4U);
    trl_put_u64(b, 1U);
    trl_put_u8(b, 5U);
    trl_put_u8(b, 6U);
    trl_put_u64(b, 2U);
    trl_put_u8(b, 16U);
    trl_put_u32(b, 1U);
    trl_put_bytes(b, text_of("3"));
    trl_put_u8(b, 17U);
    trl_put_u32(b, 1U);
    trl_buffer condition;
    trl_buffer_init(&condition);
    put_failure(&condition);
    trl_put_bytes(b, bytes_of(condition.data, condition.length));
    trl_buffer_free(&condition);
    trl_put_u8(b, 18U);
    trl_put_u64(b, 1U);
    trl_put_u64(b, 2U);
    trl_put_u64(b, 3U);
}

/* Appends a Ready. */
static void put_ready(trl_buffer *b) {
    uint8_t key[16];
    memset(key, 1, sizeof key);
    trl_put_bytes(b, text_of("ANN"));
    trl_put_raw(b, key, sizeof key);
    trl_put_u32(b, 1U);
    trl_put_bytes(b, text_of("ping"));
    trl_put_bytes(b, text_of("turborel 0.0.0 (alpha)"));
}

/* Fills the corpus; gives how many samples it holds. */
size_t corpus(sample *samples) {
    for (size_t i = 0U; i < SAMPLES; ++i) {
        trl_buffer_init(&samples[i].body);
    }
    const uint8_t kinds[] = {'D', 'B', 'B', 'B', 'K', 'E', 'R', 'P', 'N', 'T', 'L', 'L', 'C'};
    for (size_t i = 0U; i < sizeof kinds; ++i) {
        samples[i].kind = kinds[i];
    }
    put_types(&samples[0].body);
    trl_put_u32(&samples[1].body, 1U);
    trl_put_u32(&samples[1].body, 9U);
    put_scalars(&samples[1].body);
    trl_put_u32(&samples[2].body, 1U);
    trl_put_u32(&samples[2].body, 7U);
    put_octets(&samples[2].body);
    trl_put_u32(&samples[3].body, 1U);
    trl_put_u32(&samples[3].body, 2U);
    put_constructed(&samples[3].body);
    put_complete(&samples[4].body);
    put_failure(&samples[5].body);
    put_ready(&samples[6].body);
    const uint8_t prepared[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, TRL_TAG_BOOLEAN, 1, 0, 0, 0, 0, TRL_TAG_UUID, 1};
    trl_put_raw(&samples[7].body, prepared, sizeof prepared);
    trl_put_bytes(&samples[8].body, text_of("01T01"));
    trl_put_bytes(&samples[8].body, text_of("m"));
    trl_put_u8(&samples[8].body, 7U);
    trl_put_bytes(&samples[8].body, text_of("NOTICE"));
    trl_put_bytes(&samples[9].body, text_of("SCHEMA"));
    trl_put_bytes(&samples[9].body, text_of("ANN"));
    const uint8_t data[] = {0, 1, 0, 0, 0, 'x'};
    const uint8_t locator[] = {1, 9, 0, 0, 0};
    trl_put_raw(&samples[10].body, data, sizeof data);
    trl_put_raw(&samples[11].body, locator, sizeof locator);
    trl_put_bytes(&samples[12].body, text_of("s"));
    return sizeof kinds;
}

/* Frees the corpus. */
void free_corpus(sample *samples) {
    for (size_t i = 0U; i < SAMPLES; ++i) {
        trl_buffer_free(&samples[i].body);
    }
}
