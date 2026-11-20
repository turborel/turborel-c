/**
 * @file key_cache_test.c
 * @brief The SCRAM key cache [CLIENT The SCRAM key cache]: keys found by
 * the salt, the iterations and the password's tag; at most 64, the
 * oldest dropped first; an entry forgotten; hits and misses counted.
 */
#include "auth/crypto.h"
#include "auth/key_cache.h"
#include "auth/scram.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Few iterations, so the tests derive quickly. */
#define ITERATIONS 2U

/* A salt of one byte, `n`. */
static bool derive(trl_key_cache *cache, const char *password, uint8_t n, trl_scram_keys *out) {
    trl_salt salt = {&n, 1U, ITERATIONS};
    return trl_key_cache_derived(cache, password, &salt, out);
}

/* The cache's hits and misses. */
static bool counted(trl_key_cache *cache, uint64_t hits, uint64_t misses) {
    uint64_t found = 0U;
    uint64_t derived = 0U;
    trl_key_cache_lookups(cache, &found, &derived);
    return found == hits && derived == misses;
}

/**
 * Verifies: [CLIENT The SCRAM key cache] a later login of the same salt
 * and iterations finds the keys the first derived, a hit; another
 * password, salt or count of iterations finds nothing, a miss.
 */
static void keys_are_found_by_salt_iterations_and_password(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_key_cache *cache = NULL;
    TRL_CHECK(test, trl_key_cache_new(&cache) == TRL_OK);
    trl_scram_keys first;
    trl_scram_keys again;
    trl_scram_keys plain;
    TRL_CHECK(test, derive(cache, "ann's", 1U, &first) && counted(cache, 0U, 1U));
    TRL_CHECK(test, derive(cache, "ann's", 1U, &again) && counted(cache, 1U, 1U));
    TRL_CHECK(test, memcmp(&first, &again, sizeof first) == 0);
    uint8_t byte = 1U;
    trl_salt salt = {&byte, 1U, ITERATIONS};
    TRL_CHECK(test, trl_scram_derive("ann's", &salt, &plain));
    TRL_CHECK(test, memcmp(&first, &plain, sizeof first) == 0);
    TRL_CHECK(test, derive(cache, "bob's", 1U, &again) && counted(cache, 1U, 2U));
    TRL_CHECK(test, derive(cache, "ann's", 2U, &again) && counted(cache, 1U, 3U));
    salt.iterations = ITERATIONS + 1U;
    TRL_CHECK(test, trl_key_cache_derived(cache, "ann's", &salt, &again));
    TRL_CHECK(test, counted(cache, 1U, 4U) && trl_key_cache_size(cache) == 4U);
    trl_salt empty = {NULL, 0U, ITERATIONS};
    TRL_CHECK(test, trl_key_cache_derived(cache, "ann's", &empty, &again) && counted(cache, 1U, 5U));
    TRL_CHECK(test, trl_key_cache_derived(cache, "ann's", &empty, &again) && counted(cache, 2U, 5U));
    trl_key_cache_clear(cache);
    TRL_CHECK(test, trl_key_cache_size(cache) == 0U);
    trl_key_cache_free(cache);
}

/**
 * Verifies: [CLIENT The SCRAM key cache] it keeps 64 entries at most, the
 * oldest dropped first, and drops an entry a failed login used.
 */
static void at_most_64_entries_the_oldest_dropped(trl_test *test) {
    if (!trl_crypto_built()) {
        return;
    }
    trl_key_cache *cache = NULL;
    TRL_CHECK(test, trl_key_cache_new(&cache) == TRL_OK);
    trl_scram_keys keys;
    for (uint8_t n = 0U; n <= TRL_KEY_CACHE_ENTRIES; ++n) {
        TRL_CHECK(test, derive(cache, "p", n, &keys));
    }
    TRL_CHECK(test, trl_key_cache_size(cache) == TRL_KEY_CACHE_ENTRIES);
    TRL_CHECK(test, derive(cache, "p", 1U, &keys) && counted(cache, 1U, TRL_KEY_CACHE_ENTRIES + 1U));
    TRL_CHECK(test, derive(cache, "p", 0U, &keys) && counted(cache, 1U, TRL_KEY_CACHE_ENTRIES + 2U));
    /* Salt 0 came back as salt 1, the oldest then, was dropped. */
    TRL_CHECK(test, derive(cache, "p", 1U, &keys) && counted(cache, 1U, TRL_KEY_CACHE_ENTRIES + 3U));
    uint8_t byte = 3U;
    trl_salt salt = {&byte, 1U, ITERATIONS};
    trl_key_cache_forget(cache, "p", &salt);
    TRL_CHECK(test, trl_key_cache_size(cache) == TRL_KEY_CACHE_ENTRIES - 1U);
    TRL_CHECK(test, derive(cache, "p", 3U, &keys) && counted(cache, 1U, TRL_KEY_CACHE_ENTRIES + 4U));
    trl_key_cache_forget(cache, "q", &salt);
    TRL_CHECK(test, trl_key_cache_size(cache) == TRL_KEY_CACHE_ENTRIES);
    trl_key_cache_free(cache);
}

/**
 * Verifies: [CLIENT The SCRAM key cache] a cache lives while a holder
 * holds it, and a null one asks nothing.
 */
static void a_cache_lives_while_held(trl_test *test) {
    trl_key_cache *cache = NULL;
    TRL_CHECK(test, trl_key_cache_new(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_key_cache_new(&cache) == TRL_OK);
    trl_key_cache_hold(cache);
    trl_key_cache_free(cache);
    TRL_CHECK(test, trl_key_cache_size(cache) == 0U);
    trl_key_cache_free(cache);
    trl_key_cache_free(NULL);
    trl_key_cache_clear(NULL);
    TRL_CHECK(test, trl_key_cache_size(NULL) == 0U);
    uint64_t hits = 1U;
    uint64_t misses = 1U;
    trl_key_cache_lookups(NULL, &hits, &misses);
    TRL_CHECK(test, hits == 0U && misses == 0U);
    trl_key_cache_lookups(NULL, NULL, NULL);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(keys_are_found_by_salt_iterations_and_password),
        TRL_TEST_CASE(at_most_64_entries_the_oldest_dropped),
        TRL_TEST_CASE(a_cache_lives_while_held),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
