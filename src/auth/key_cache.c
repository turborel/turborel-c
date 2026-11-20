/**
 * @file key_cache.c
 * @brief The SCRAM key cache [CLIENT The SCRAM key cache]: at most
 * TRL_KEY_CACHE_ENTRIES entries, oldest first, each found by the salt,
 * the iterations and an HMAC of the password under a secret of the
 * cache's own, so it holds nothing a password could be guessed from
 * offline. A lock lets every thread use it at once (RULES.md D11), and
 * its holds count who still uses it.
 */
#include "auth/key_cache.h"

#include "auth/crypto.h"
#include "auth/scram.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Each platform declares its locks in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK trl_lock;

static bool lock_init(trl_lock *lock) {
    InitializeSRWLock(lock);
    return true;
}

static void lock_destroy(trl_lock *lock) {
    (void)lock;
}

static void lock_take(trl_lock *lock) {
    AcquireSRWLockExclusive(lock);
}

static void lock_give(trl_lock *lock) {
    ReleaseSRWLockExclusive(lock);
}
#else
#include <pthread.h>
typedef pthread_mutex_t trl_lock;

static bool lock_init(trl_lock *lock) {
    return pthread_mutex_init(lock, NULL) == 0;
}

static void lock_destroy(trl_lock *lock) {
    (void)pthread_mutex_destroy(lock);
}

static void lock_take(trl_lock *lock) {
    (void)pthread_mutex_lock(lock);
}

static void lock_give(trl_lock *lock) {
    (void)pthread_mutex_unlock(lock);
}
#endif
// NOLINTEND(misc-include-cleaner)

/* What finds an entry. */
typedef struct entry_index {
    uint8_t tag[TRL_SHA256_BYTES];
    const uint8_t *salt;
    size_t salt_length;
    uint32_t iterations;
} entry_index;

/* An entry: its index and its keys. */
typedef struct entry {
    entry_index index;
    trl_scram_keys keys;
    uint8_t *salt;
} entry;

struct trl_key_cache {
    trl_lock lock;
    /* The secret its tags are made under; absent if the system gave none,
       and then nothing is kept. */
    char secret[TRL_SCRAM_NONCE_TEXT];
    entry held[TRL_KEY_CACHE_ENTRIES];
    size_t count;
    uint64_t hits;
    uint64_t misses;
    size_t holds;
};

trl_status trl_key_cache_new(trl_key_cache **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    trl_key_cache *cache = trl_calloc(1U, sizeof *cache);
    if (cache == NULL) {
        return TRL_NO_MEMORY;
    }
    if (!lock_init(&cache->lock)) {
        free(cache);
        return TRL_NO_MEMORY;
    }
    trl_scram_nonce(cache->secret);
    cache->holds = 1U;
    *out = cache;
    return TRL_OK;
}

void trl_key_cache_hold(trl_key_cache *cache) {
    lock_take(&cache->lock);
    ++cache->holds;
    lock_give(&cache->lock);
}

/* Drops the entry at `at`, the later ones moved up. */
static void drop(trl_key_cache *cache, size_t at) {
    free(cache->held[at].salt);
    memmove(&cache->held[at], &cache->held[at + 1U], (cache->count - at - 1U) * sizeof cache->held[0]);
    --cache->count;
    trl_cleanse(&cache->held[cache->count], sizeof cache->held[0]);
}

/* Drops every entry; the lock is held. */
static void drop_all(trl_key_cache *cache) {
    while (cache->count > 0U) {
        drop(cache, cache->count - 1U);
    }
}

void trl_key_cache_free(trl_key_cache *cache) {
    if (cache == NULL) {
        return;
    }
    lock_take(&cache->lock);
    bool last = --cache->holds == 0U;
    lock_give(&cache->lock);
    if (!last) {
        return;
    }
    drop_all(cache);
    lock_destroy(&cache->lock);
    trl_cleanse(cache, sizeof *cache);
    free(cache);
}

/* The index of a password, a salt and iterations; false if the cache has
   no secret. Its salt is a view of the caller's. */
static bool index_of(const trl_key_cache *cache, const char *password, const trl_salt *salt, entry_index *out) {
    if (cache->secret[0] == '\0') {
        return false;
    }
    out->salt = salt->bytes;
    out->salt_length = salt->length;
    out->iterations = salt->iterations;
    return trl_hmac_sha256((const uint8_t *)cache->secret, strlen(cache->secret), (const uint8_t *)password,
                           strlen(password), out->tag);
}

/* Where the entry of `index` is, or the count if none is; the lock is held. */
static size_t find(const trl_key_cache *cache, const entry_index *index) {
    for (size_t i = 0U; i < cache->count; ++i) {
        const entry_index *at = &cache->held[i].index;
        if (at->iterations == index->iterations && at->salt_length == index->salt_length &&
            memcmp(at->tag, index->tag, sizeof at->tag) == 0 &&
            (index->salt_length == 0U || memcmp(at->salt, index->salt, index->salt_length) == 0)) {
            return i;
        }
    }
    return cache->count;
}

/* Keeps `keys` under `index` unless kept already, the oldest dropped to
   make room; nothing is kept if memory cannot be had. The lock is held. */
static void keep(trl_key_cache *cache, const entry_index *index, const trl_scram_keys *keys) {
    if (find(cache, index) != cache->count) {
        return;
    }
    uint8_t *salt = trl_malloc(index->salt_length == 0U ? 1U : index->salt_length);
    if (salt == NULL) {
        return;
    }
    if (index->salt_length > 0U) {
        memcpy(salt, index->salt, index->salt_length);
    }
    if (cache->count == TRL_KEY_CACHE_ENTRIES) {
        drop(cache, 0U);
    }
    entry *added = &cache->held[cache->count++];
    added->index = *index;
    added->index.salt = salt;
    added->salt = salt;
    added->keys = *keys;
}

/* The keys of `index`, if kept, counted as a hit or a miss. */
static bool found(trl_key_cache *cache, const entry_index *index, trl_scram_keys *out) {
    lock_take(&cache->lock);
    size_t at = find(cache, index);
    bool kept = at != cache->count;
    if (kept) {
        *out = cache->held[at].keys;
        ++cache->hits;
    } else {
        ++cache->misses;
    }
    lock_give(&cache->lock);
    return kept;
}

bool trl_key_cache_derived(trl_key_cache *cache, const char *password, const trl_salt *salt, trl_scram_keys *out) {
    entry_index index;
    if (!index_of(cache, password, salt, &index)) {
        return trl_scram_derive(password, salt, out);
    }
    if (found(cache, &index, out)) {
        return true;
    }
    if (!trl_scram_derive(password, salt, out)) {
        return false;
    }
    lock_take(&cache->lock);
    keep(cache, &index, out);
    lock_give(&cache->lock);
    return true;
}

void trl_key_cache_forget(trl_key_cache *cache, const char *password, const trl_salt *salt) {
    entry_index index;
    if (!index_of(cache, password, salt, &index)) {
        return;
    }
    lock_take(&cache->lock);
    size_t at = find(cache, &index);
    if (at != cache->count) {
        drop(cache, at);
    }
    lock_give(&cache->lock);
}

size_t trl_key_cache_size(trl_key_cache *cache) {
    if (cache == NULL) {
        return 0U;
    }
    lock_take(&cache->lock);
    size_t count = cache->count;
    lock_give(&cache->lock);
    return count;
}

void trl_key_cache_lookups(trl_key_cache *cache, uint64_t *hits, uint64_t *misses) {
    uint64_t found = 0U;
    uint64_t derived = 0U;
    if (cache != NULL) {
        lock_take(&cache->lock);
        found = cache->hits;
        derived = cache->misses;
        lock_give(&cache->lock);
    }
    if (hits != NULL) {
        *hits = found;
    }
    if (misses != NULL) {
        *misses = derived;
    }
}

void trl_key_cache_clear(trl_key_cache *cache) {
    if (cache == NULL) {
        return;
    }
    lock_take(&cache->lock);
    drop_all(cache);
    lock_give(&cache->lock);
}
