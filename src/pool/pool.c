/**
 * @file pool.c
 * @brief Pools of connections [CLIENT Pools]: a bounded set a program's
 * threads share. Getting one hands out an idle connection that answers,
 * or makes one while fewer than the most are made, or waits for one to
 * come back, at most the wait. A connection handed out comes back as it
 * is released, reset to the state it logged in with (`reset.c`); one
 * lost, or whose reset fails, is closed instead. Idle beyond its idle
 * timeout, or older than its life, a connection is closed rather than
 * handed out, and no more than the most idle are kept. A lock and a wait
 * on it let every thread share it (RULES.md D11); it starts no thread,
 * each wait being the waiting borrower's.
 */
#include "pool/pool.h"

#include "codec/error.h"
#include "conn/conn.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "sync.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The SQLSTATE of no connection in time. */
#define TRL_SQLSTATE_UNABLE_TO_CONNECT "08001"
/* Room for a time's text. */
#define TRL_TIME_BYTES 32U

/* A connection idle in the pool, or handed out: when it was made, and
   since when idle. */
typedef struct held {
    trl_conn *conn;
    uint64_t made;
    uint64_t since;
} held;

struct trl_pool {
    trl_lock lock;
    trl_wait returned;
    size_t holds;
    trl_connector connect;
    void *context;
    /* The settings it connects by, if made of settings, else null. */
    const trl_settings *settings;
    trl_pool_config config;
    char **reset;
    held *idle;
    size_t idle_count;
    held *lent;
    size_t lent_count;
    size_t made;
};

trl_pool_config trl_pool_config_default(void) {
    trl_pool_config config = {
        TRL_POOL_MAX, TRL_POOL_MAX, TRL_POOL_IDLE_TIMEOUT_MS, TRL_POOL_MAX_LIFETIME_MS, TRL_POOL_WAIT_MS, true,
        NULL,         0U,
    };
    return config;
}

/* Copies the program's reset statements into the pool's own. */
static bool reset_copied(trl_pool *pool, const trl_pool_config *config) {
    pool->reset = (char **)trl_calloc(config->reset_count == 0U ? 1U : config->reset_count, sizeof(char *));
    if (pool->reset == NULL) {
        return false;
    }
    for (size_t i = 0U; i < config->reset_count; ++i) {
        size_t length = strlen(config->reset[i]) + 1U;
        pool->reset[i] = trl_malloc(length);
        if (pool->reset[i] == NULL) {
            return false;
        }
        memcpy(pool->reset[i], config->reset[i], length);
    }
    pool->config.reset = (const char *const *)pool->reset;
    return true;
}

/* Frees what a pool holds, its idle connections closed. */
static void pool_destroy(trl_pool *pool) {
    for (size_t i = 0U; i < pool->idle_count; ++i) {
        trl_conn_free(pool->idle[i].conn);
    }
    for (size_t i = 0U; pool->reset != NULL && i < pool->config.reset_count; ++i) {
        free(pool->reset[i]);
    }
    free((void *)pool->reset);
    free(pool->idle);
    free(pool->lent);
    free(pool);
}

/* Makes a pool of `connect`'s connections, or of `settings`'. */
static trl_status pool_make(trl_connector connect, void *context, const trl_settings *settings,
                            const trl_pool_config *config, trl_pool **out) {
    trl_pool_config defaults = trl_pool_config_default();
    const trl_pool_config *taken = config == NULL ? &defaults : config;
    if (taken->max == 0U || (taken->reset_count > 0U && taken->reset == NULL)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_pool *pool = trl_calloc(1U, sizeof *pool);
    if (pool == NULL) {
        return TRL_NO_MEMORY;
    }
    pool->connect = connect;
    pool->context = context;
    pool->settings = settings;
    pool->config = *taken;
    pool->holds = 1U;
    pool->idle = trl_calloc(taken->max, sizeof *pool->idle);
    pool->lent = trl_calloc(taken->max, sizeof *pool->lent);
    bool made = pool->idle != NULL && pool->lent != NULL && reset_copied(pool, taken);
    if (!made || !trl_lock_init(&pool->lock)) {
        pool_destroy(pool);
        return TRL_NO_MEMORY;
    }
    if (!trl_wait_init(&pool->returned)) {
        trl_lock_destroy(&pool->lock);
        pool_destroy(pool);
        return TRL_NO_MEMORY;
    }
    *out = pool;
    return TRL_OK;
}

trl_status trl_pool_new(trl_connector connect, void *context, const trl_pool_config *config, trl_pool **out) {
    if (out != NULL) {
        *out = NULL;
    }
    return connect == NULL || out == NULL ? TRL_INVALID_ARGUMENT : pool_make(connect, context, NULL, config, out);
}

trl_status trl_pool_of_settings(const trl_settings *settings, const trl_pool_config *config, trl_pool **out) {
    if (out != NULL) {
        *out = NULL;
    }
    return settings == NULL || out == NULL ? TRL_INVALID_ARGUMENT : pool_make(NULL, NULL, settings, config, out);
}

trl_pool *trl_pool_hold(trl_pool *pool) {
    if (pool != NULL) {
        trl_lock_take(&pool->lock);
        ++pool->holds;
        trl_lock_give(&pool->lock);
    }
    return pool;
}

/* Whether `idle` may still be handed out at `now`. */
static bool fresh(const trl_pool *pool, const held *idle, uint64_t now) {
    bool lived = pool->config.max_lifetime_ms == 0U || now - idle->made < pool->config.max_lifetime_ms;
    bool rested = pool->config.idle_timeout_ms == 0U || now - idle->since < pool->config.idle_timeout_ms;
    return lived && rested && !idle->conn->lost;
}

/* Notes a connection handed out, under the lock. */
static void lend(trl_pool *pool, trl_conn *conn, uint64_t made) {
    held h = {conn, made, 0U};
    pool->lent[pool->lent_count++] = h;
}

/* One fewer connection made, so a waiter may make one, under the lock. */
static void unmade(trl_pool *pool) {
    pool->made = pool->made > 0U ? pool->made - 1U : 0U;
    trl_wait_wake(&pool->returned);
}

/* Fails a get: no connection came back in time (SQLSTATE 08001). */
static trl_status none_in_time(const trl_pool *pool, trl_conn **out) {
    char waited[TRL_TIME_BYTES];
    trl_format_duration(waited, sizeof waited, pool->config.wait_ms);
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "no connection of the pool's %lu came back within %s",
               (unsigned long)pool->config.max, waited);
    trl_failure failure;
    trl_failure_init(&failure);
    trl_failure_set(&failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_UNABLE_TO_CONNECT, message);
    *out = trl_conn_failed_with(&failure);
    trl_failure_clear(&failure);
    return *out == NULL ? TRL_NO_MEMORY : TRL_FAILED;
}

/* What getting a connection takes next. */
typedef enum next {
    NEXT_IDLE = 0,
    NEXT_MAKE,
    NEXT_NONE,
} next;

/* What a get takes next, under the lock, waiting for a connection to come
   back until `deadline`: an idle one into `idle`, a place to make one, or
   none in time; stale ones put in `stale` to close. */
static next next_of(trl_pool *pool, uint64_t deadline, held *idle, held *stale, size_t *stale_count) {
    for (;;) {
        uint64_t now = trl_now_ms();
        while (pool->idle_count > 0U) {
            held h = pool->idle[--pool->idle_count];
            if (fresh(pool, &h, now)) {
                *idle = h;
                return NEXT_IDLE;
            }
            stale[(*stale_count)++] = h;
            pool->made = pool->made > 0U ? pool->made - 1U : 0U;
        }
        if (pool->made < pool->config.max) {
            ++pool->made;
            return NEXT_MAKE;
        }
        if (now >= deadline) {
            return NEXT_NONE;
        }
        uint64_t left = deadline - now;
        (void)trl_wait_for(&pool->returned, &pool->lock, left > UINT32_MAX ? UINT32_MAX : (uint32_t)left);
    }
}

/* Makes a new connection in the place a get took for it. */
static trl_status made_one(trl_pool *pool, trl_conn **out) {
    trl_conn *conn = NULL;
    trl_status made =
        pool->settings != NULL ? trl_settings_connect(pool->settings, &conn) : pool->connect(pool->context, &conn);
    trl_lock_take(&pool->lock);
    if (made == TRL_OK && conn != NULL) {
        lend(pool, conn, trl_now_ms());
    } else {
        unmade(pool);
    }
    trl_lock_give(&pool->lock);
    *out = conn;
    if (made == TRL_OK && conn == NULL) {
        return TRL_NO_MEMORY;
    }
    return made;
}

/* Hands out `idle` if it answers its check, else closes it, its place
   given back; whether it was handed out. */
static bool handed(trl_pool *pool, held idle) {
    bool answers = !pool->config.check || trl_pool_answers(idle.conn);
    trl_lock_take(&pool->lock);
    if (answers) {
        lend(pool, idle.conn, idle.made);
    } else {
        unmade(pool);
    }
    trl_lock_give(&pool->lock);
    if (!answers) {
        trl_conn_free(idle.conn);
    }
    return answers;
}

/* Closes the connections gone stale. */
static void closed(held *stale, size_t count) {
    for (size_t i = 0U; i < count; ++i) {
        trl_conn_free(stale[i].conn);
    }
}

trl_status trl_pool_get(trl_pool *pool, trl_conn **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (pool == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    uint64_t deadline = trl_now_ms() + pool->config.wait_ms;
    held *stale = trl_calloc(pool->config.max, sizeof *stale);
    if (stale == NULL) {
        return TRL_NO_MEMORY;
    }
    for (;;) {
        held idle = {NULL, 0U, 0U};
        size_t stale_count = 0U;
        trl_lock_take(&pool->lock);
        next taken = next_of(pool, deadline, &idle, stale, &stale_count);
        trl_lock_give(&pool->lock);
        closed(stale, stale_count);
        if (taken != NEXT_IDLE || handed(pool, idle)) {
            free(stale);
            if (taken == NEXT_IDLE) {
                *out = idle.conn;
                return TRL_OK;
            }
            return taken == NEXT_MAKE ? made_one(pool, out) : none_in_time(pool, out);
        }
    }
}

/* Takes `conn` off the list of those handed out, under the lock: whether
   it was there, and when it was made. */
static bool returned(trl_pool *pool, const trl_conn *conn, uint64_t *made) {
    for (size_t i = 0U; i < pool->lent_count; ++i) {
        if (pool->lent[i].conn == conn) {
            *made = pool->lent[i].made;
            pool->lent[i] = pool->lent[--pool->lent_count];
            return true;
        }
    }
    return false;
}

void trl_pool_release(trl_pool *pool, trl_conn *conn) {
    if (pool == NULL || conn == NULL) {
        return;
    }
    uint64_t made = 0U;
    trl_lock_take(&pool->lock);
    bool ours = returned(pool, conn, &made);
    trl_lock_give(&pool->lock);
    if (!ours) {
        trl_conn_free(conn);
        return;
    }
    uint64_t now = trl_now_ms();
    bool lived = pool->config.max_lifetime_ms != 0U && now - made >= pool->config.max_lifetime_ms;
    bool kept = !lived && trl_pool_reset(conn, &pool->config);
    trl_lock_take(&pool->lock);
    if (kept && pool->idle_count < pool->config.max_idle) {
        held h = {conn, made, trl_now_ms()};
        pool->idle[pool->idle_count++] = h;
        conn = NULL;
        trl_wait_wake(&pool->returned);
    } else {
        unmade(pool);
    }
    trl_lock_give(&pool->lock);
    trl_conn_free(conn);
}

void trl_pool_counts(trl_pool *pool, size_t *made, size_t *idle) {
    if (pool == NULL) {
        return;
    }
    trl_lock_take(&pool->lock);
    if (made != NULL) {
        *made = pool->made;
    }
    if (idle != NULL) {
        *idle = pool->idle_count;
    }
    trl_lock_give(&pool->lock);
}

void trl_pool_free(trl_pool *pool) {
    if (pool == NULL) {
        return;
    }
    trl_lock_take(&pool->lock);
    bool last = --pool->holds == 0U;
    trl_lock_give(&pool->lock);
    if (!last) {
        return;
    }
    trl_wait_destroy(&pool->returned);
    trl_lock_destroy(&pool->lock);
    pool_destroy(pool);
}
