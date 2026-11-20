/**
 * @file reader.c
 * @brief A large object read from its beginning to its end, a chunk at a
 * time [CLIENT Large objects]: of one sent by its locator, a Lob read a
 * chunk [PROTOCOL Lob 'L'].
 */
#include "codec/arena.h"
#include "codec/values.h"
#include "data/params.h"
#include "lob/lob.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A reader: its connection, a copy of its value, and the chunk read. */
struct trl_lob_reader {
    trl_conn *conn;
    trl_arena arena;
    trl_value lob;
    trl_lob_info info;
    uint64_t at;
    uint8_t *chunk;
    size_t chunk_length;
    size_t taken;
};

trl_status trl_lob_reader_new(trl_conn *conn, const trl_value *lob, trl_lob_reader **out) {
    if (out != NULL) {
        *out = NULL;
    }
    trl_lob_info info;
    if (conn == NULL || out == NULL || trl_value_lob(lob, &info) != TRL_OK) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_lob_reader *reader = trl_calloc(1U, sizeof *reader);
    if (reader == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_arena_init(&reader->arena);
    reader->chunk = info.located ? trl_malloc(TRL_LOB_CHUNK_BYTES) : NULL;
    if ((info.located && reader->chunk == NULL) || !trl_value_copy(&reader->arena, lob, &reader->lob, 0U)) {
        trl_lob_reader_free(reader);
        return TRL_NO_MEMORY;
    }
    reader->conn = conn;
    reader->info = info;
    *out = reader;
    return TRL_OK;
}

/* Reads the next chunk of a large object by its locator. */
static trl_status next_chunk(trl_lob_reader *reader) {
    trl_lob_range range = {reader->at, TRL_LOB_CHUNK_BYTES, false};
    size_t length = TRL_LOB_CHUNK_BYTES;
    trl_status read = trl_lob_read(reader->conn, &reader->lob, range, reader->chunk, &length);
    if (read != TRL_OK) {
        return read;
    }
    if (length == 0U) {
        trl_lob_misused(reader->conn, "the server sent less of the large object than its length");
        return TRL_FAILED;
    }
    reader->at += length;
    reader->chunk_length = length;
    reader->taken = 0U;
    return TRL_OK;
}

trl_status trl_lob_reader_read(trl_lob_reader *reader, uint8_t *out, size_t capacity, size_t *length) {
    if (reader == NULL || out == NULL || length == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *length = 0U;
    if (!reader->info.located) {
        trl_lob_range range = {reader->at, capacity, false};
        size_t read = capacity;
        trl_lob_held_range(&reader->lob, range, out, &read);
        reader->at += read;
        *length = read;
        return TRL_OK;
    }
    if (reader->taken == reader->chunk_length) {
        if (reader->at >= reader->info.octets || capacity == 0U) {
            return TRL_OK;
        }
        trl_status read = next_chunk(reader);
        if (read != TRL_OK) {
            return read;
        }
    }
    size_t left = reader->chunk_length - reader->taken;
    size_t n = left < capacity ? left : capacity;
    memcpy(out, reader->chunk + reader->taken, n);
    reader->taken += n;
    *length = n;
    return TRL_OK;
}

void trl_lob_reader_free(trl_lob_reader *reader) {
    if (reader == NULL) {
        return;
    }
    trl_arena_free(&reader->arena);
    free(reader->chunk);
    free(reader);
}
