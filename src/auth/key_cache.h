/**
 * @file key_cache.h
 * @brief The SCRAM key cache's use by a mechanism
 * [CLIENT The SCRAM key cache]: keys found or derived, and forgotten.
 */
#ifndef TRL_AUTH_KEY_CACHE_H
#define TRL_AUTH_KEY_CACHE_H

#include "auth/crypto.h"
#include "auth/scram.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Takes another hold of the cache, which trl_key_cache_free() gives back. */
void trl_key_cache_hold(trl_key_cache *cache);

/**
 * @brief The keys of a password, a salt and iterations: found, a hit, or
 * derived and kept, a miss.
 */
bool trl_key_cache_derived(trl_key_cache *cache, const char *password, const trl_salt *salt, trl_scram_keys *out);

/** @brief Drops the entry of a password, a salt and iterations, if kept. */
void trl_key_cache_forget(trl_key_cache *cache, const char *password, const trl_salt *salt);

#endif
