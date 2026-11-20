/**
 * @file arena.c
 * @brief An arena of blocks, each at least a page's worth, so many small
 * allocations cost few calls to the allocator.
 */
#include "codec/arena.h"

#include "memory.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The least bytes a block holds: small items share blocks. */
#define TRL_ARENA_BLOCK_BYTES 4096U

/* A unit aligned for every type an allocation holds: its size is a
   multiple of each member's alignment, so allocations in whole units are
   aligned for any of them. Written out, as MSVC's C has no max_align_t. */
typedef union trl_arena_unit {
    long double number;
    long long integer;
    void *pointer;
    void (*function)(void);
} trl_arena_unit;

struct trl_arena_block {
    trl_arena_block *next;
    size_t capacity;
    size_t used;
    /* The block's bytes follow, in whole units. */
    trl_arena_unit units[];
};

void trl_arena_init(trl_arena *arena) {
    arena->blocks = NULL;
    arena->limit = 0U;
    arena->held = 0U;
}

void trl_arena_free(trl_arena *arena) {
    if (arena == NULL) {
        return;
    }
    trl_arena_block *block = arena->blocks;
    while (block != NULL) {
        trl_arena_block *next = block->next;
        free(block);
        block = next;
    }
    arena->blocks = NULL;
    arena->held = 0U;
}

/* `size` rounded up to whole units; 0 if that overflows. */
static size_t aligned(size_t size) {
    const size_t align = sizeof(trl_arena_unit);
    size_t rest = size % align;
    if (rest == 0U) {
        return size;
    }
    if (size > SIZE_MAX - (align - rest)) {
        return 0U;
    }
    return size + (align - rest);
}

/* A new block of at least `bytes`, put first in `arena`. */
static trl_arena_block *grow(trl_arena *arena, size_t bytes) {
    size_t capacity = bytes > TRL_ARENA_BLOCK_BYTES ? bytes : TRL_ARENA_BLOCK_BYTES;
    if (arena->limit != 0U) {
        size_t left = arena->held < arena->limit ? arena->limit - arena->held : 0U;
        if (bytes > left) {
            return NULL;
        }
        capacity = capacity < left ? capacity : left;
    }
    if (capacity > SIZE_MAX - sizeof(trl_arena_block)) {
        return NULL;
    }
    trl_arena_block *block = trl_malloc(sizeof(trl_arena_block) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->next = arena->blocks;
    block->capacity = capacity;
    block->used = 0U;
    arena->blocks = block;
    arena->held += capacity;
    return block;
}

void *trl_arena_alloc(trl_arena *arena, size_t count, size_t size) {
    if (size != 0U && count > SIZE_MAX / size) {
        return NULL;
    }
    size_t bytes = aligned(count * size);
    if (bytes == 0U && count * size != 0U) {
        return NULL;
    }
    trl_arena_block *block = arena->blocks;
    if (block == NULL || block->capacity - block->used < bytes) {
        block = grow(arena, bytes);
        if (block == NULL) {
            return NULL;
        }
    }
    unsigned char *at = (unsigned char *)block->units + block->used;
    block->used += bytes;
    if (bytes != 0U) {
        memset(at, 0, bytes);
    }
    return at;
}
