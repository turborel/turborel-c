/**
 * @file arena.h
 * @brief Memory for what one decoded message holds, freed at once.
 *
 * A decoded reply holds arrays of values, columns and strings; each comes
 * from its arena, and freeing the arena frees them all, so a reply has one
 * owner and nothing in it leaks (RULES.md D10).
 */
#ifndef TRL_CODEC_ARENA_H
#define TRL_CODEC_ARENA_H

#include <stddef.h>

/** @brief A block of an arena; opaque to its users. */
typedef struct trl_arena_block trl_arena_block;

/** @brief An arena: blocks allocated as needed, freed together. */
typedef struct trl_arena {
    /** @brief The newest block, which allocations come from. */
    trl_arena_block *blocks;
    /**
     * @brief The most bytes its blocks may hold, 0 for no bound: past it an
     * allocation fails as memory not had, so what one reply holds is
     * bounded.
     */
    size_t limit;
    /** @brief The bytes its blocks hold. */
    size_t held;
} trl_arena;

/**
 * @brief Readies an arena that holds nothing yet.
 *
 * @param arena The arena.
 */
void trl_arena_init(trl_arena *arena);

/**
 * @brief Frees every allocation of `arena`, which is then empty.
 *
 * @param arena The arena; nothing is done if null.
 */
void trl_arena_free(trl_arena *arena);

/**
 * @brief Memory for `count` items of `size` bytes, zeroed and aligned for
 * any type, owned by `arena`.
 *
 * @param arena The arena.
 * @param count How many items.
 * @param size The bytes of each.
 * @return The memory; null if it could not be had, or the product
 * overflows. A request of no bytes gives a valid pointer.
 */
void *trl_arena_alloc(trl_arena *arena, size_t count, size_t size);

#endif
