/**
 * @file connect.c
 * @brief Connecting as settings say, to one of several servers [CLIENT
 * Several servers]: each host tried in the order given, or in an order
 * drawn at random for each connection, and each address a host's name
 * resolves to in turn, every step within the connect timeout. A server
 * reached, logged in to and of the role asked is kept; another is left
 * and the next tried. No server kept, the failure says why each was not.
 * TLS is used as the settings' mode says [CLIENT TLS by mode].
 */
#include "auth/crypto.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "conn/conn.h"
#include "conn/target.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "net/tls.h"
#include "net/transport.h"
#include "settings/settings.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The multiplier and increment of the generator that orders hosts at
   random (Knuth's MMIX linear congruential generator), and the high bits
   of its state taken for each draw, the low bits being the weakest. */
#define TRL_MULTIPLIER 6364136223846793005ULL
#define TRL_INCREMENT 1442695040888963407ULL
#define TRL_DRAW_SHIFT 33U
/* The connection attribute a program's name is given as. */
#define TRL_APPLICATION "application"
/* The octets of an SQLSTATE's class. */
#define TRL_CLASS_BYTES 2U

void trl_shuffle(size_t *order, size_t count, uint64_t seed) {
    uint64_t state = seed;
    for (size_t i = count; i > 1U; --i) {
        state = (state * TRL_MULTIPLIER) + TRL_INCREMENT;
        size_t drawn = (size_t)((state >> TRL_DRAW_SHIFT) % (uint64_t)i);
        size_t swapped = order[i - 1U];
        order[i - 1U] = order[drawn];
        order[drawn] = swapped;
    }
}

/* A seed of the system's randomness; of a library without one, of the
   time and the stack's place. */
static uint64_t seed(void) {
    uint64_t drawn = 0U;
    if (trl_random((uint8_t *)&drawn, sizeof drawn)) {
        return drawn;
    }
    uintptr_t place = (uintptr_t)&drawn;
    return ((uint64_t)time(NULL) << TRL_DRAW_SHIFT) ^ (uint64_t)clock() ^ (uint64_t)place;
}

/* Why each server was not kept: the reasons, the last failure, and the
   one failure of a connection that tried one server. */
typedef struct tried {
    trl_buffer reasons;
    size_t count;
    trl_failure last;
} tried;

/* Notes that the server at `at` was not kept, and why. */
static void note(tried *t, const char *at, const trl_failure *failure) {
    if (t->count > 0U) {
        trl_put_text(&t->reasons, "; ");
    }
    trl_put_text(&t->reasons, at);
    trl_put_text(&t->reasons, ": ");
    trl_put_text(&t->reasons, trl_failure_message(failure));
    trl_failure_copy(&t->last, failure);
    ++t->count;
}

/* The failure of a connection none tried gave, into `out`. */
static void none_kept(tried *t, trl_failure *out) {
    if (t->count == 1U) {
        trl_failure_copy(out, &t->last);
        return;
    }
    bool refused = t->count > 0U && strncmp(trl_failure_sqlstate(&t->last), "28", TRL_CLASS_BYTES) == 0;
    trl_put_u8(&t->reasons, 0U);
    char message[TRL_FAILURE_MESSAGE_BYTES];
    if (t->count == 0U) {
        trl_format(message, sizeof message, "no server is named");
    } else {
        trl_format(message, sizeof message, "no server was kept: %s",
                   t->reasons.state == TRL_BUFFER_OK ? (const char *)t->reasons.data : "");
    }
    if (refused) {
        trl_failure_set(out, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_INVALID_AUTHORIZATION, message);
    } else {
        trl_settings_unable(out, message);
    }
}

/* A connection under way: the settings, what it asks, its TLS. */
typedef struct attempt {
    const trl_settings *settings;
    trl_conn_options *options;
    trl_tls *tls;
    tried tried;
} attempt;

/* The certificate checks of a TLS mode. */
static trl_tls_checks checks_of(trl_tls_mode mode) {
    if (mode == TRL_TLS_VERIFY_FULL) {
        return TRL_CHECKS_FULL;
    }
    return mode == TRL_TLS_VERIFY_CA ? TRL_CHECKS_AUTHORITY : TRL_CHECKS_NONE;
}

/* Copies the TLS settings' failure, or memory not had of none made. */
static bool tls_failed(const trl_tls *tls, trl_failure *failure) {
    if (tls == NULL) {
        trl_failure_no_memory(failure);
    } else {
        trl_failure_copy(failure, trl_tls_failure(tls));
    }
    return false;
}

/* Trusts the authorities the settings name, of a mode that checks the
   server's certificate: one at least. */
static bool trusted(const trl_settings *settings, trl_tls *tls, trl_failure *failure) {
    const char *roots = settings->laid[TRL_KEY_TLS_ROOTS];
    if (roots != NULL && trl_tls_trust_file(tls, roots) != TRL_OK) {
        return tls_failed(tls, failure);
    }
    if (settings->system_roots && trl_tls_trust_platform(tls) != TRL_OK) {
        return tls_failed(tls, failure);
    }
    if (roots == NULL && !settings->system_roots) {
        trl_failure_broken(failure, "no certificate authority is trusted: name a file of them, or the platform's");
        return false;
    }
    return true;
}

/* Presents the certificate the settings name, if any, with its key. */
static bool presented(const trl_settings *settings, trl_tls *tls, trl_failure *failure) {
    const char *chain = settings->laid[TRL_KEY_TLS_CERTIFICATE];
    const char *key = settings->laid[TRL_KEY_TLS_KEY];
    if (chain == NULL) {
        return true;
    }
    if (key == NULL) {
        trl_failure_broken(failure, "a client certificate is presented with its key, and none is named");
        return false;
    }
    return trl_tls_present(tls, chain, key) == TRL_OK || tls_failed(tls, failure);
}

/* The TLS the settings' mode makes, if any, trusting and presenting what
   they name; false, with why, if it cannot be made. */
static bool tls_made(const trl_settings *settings, trl_tls **out, trl_failure *failure) {
    *out = NULL;
    if (settings->tls_mode == TRL_TLS_DISABLE) {
        return true;
    }
#ifndef TRL_HAVE_TLS
    if (settings->tls_mode == TRL_TLS_PREFER) {
        return true;
    }
#endif
    if (trl_tls_new(out) != TRL_OK) {
        return tls_failed(*out, failure);
    }
    bool verifies = settings->tls_mode == TRL_TLS_VERIFY_CA || settings->tls_mode == TRL_TLS_VERIFY_FULL;
    if ((verifies && !trusted(settings, *out, failure)) || !presented(settings, *out, failure)) {
        return false;
    }
    trl_tls_set_checks(*out, checks_of(settings->tls_mode));
    return true;
}

/* The password of host `host`, whole however long, as turborel-client
   keeps it, into `*out`, the caller's to cleanse and free, and its length;
   `*given` whether one is; false if memory cannot be had. */
static bool password_of(const trl_settings *settings, size_t host, char **out, size_t *length, bool *given) {
    *given = trl_settings_password_for(settings, host, NULL, 0U, length);
    if (!*given) {
        return true;
    }
    *out = trl_malloc(*length + 1U);
    if (*out == NULL || !trl_settings_password_for(settings, host, *out, *length + 1U, NULL)) {
        free(*out);
        *out = NULL;
        return false;
    }
    return true;
}

/* What proves the user at host `host`, over TLS if `secured`: SCRAM of a
   password, else the certificate named, else trust; null, with why, of a
   channel binding required that cannot bind (SQLSTATE 28000). */
static trl_mechanism *proof(const attempt *a, size_t host, bool secured, trl_failure *failure) {
    const trl_settings *settings = a->settings;
    size_t length = 0U;
    char *password = NULL;
    bool given = false;
    if (!password_of(settings, host, &password, &length, &given)) {
        trl_failure_no_memory(failure);
        return NULL;
    }
    trl_mechanism *mechanism = NULL;
    bool binds = false;
    trl_status made = TRL_OK;
#ifdef TRL_HAVE_TLS
    if (given) {
        made = trl_mechanism_scram(password, &mechanism);
        made = made == TRL_OK ? trl_mechanism_scram_channel_binding(mechanism, settings->channel_binding) : made;
        made = made == TRL_OK ? trl_mechanism_scram_key_cache(mechanism, settings->key_cache) : made;
        binds = true;
    }
#else
    if (given) {
        made = trl_mechanism_password(password, &mechanism);
    }
#endif
    if (password != NULL) {
        trl_cleanse((uint8_t *)password, length + 1U);
        free(password);
    }
    if (!given) {
        made = settings->laid[TRL_KEY_TLS_CERTIFICATE] != NULL ? trl_mechanism_certificate(&mechanism)
                                                               : trl_mechanism_trust(&mechanism);
    }
    if (made != TRL_OK) {
        trl_mechanism_free(mechanism);
        trl_failure_no_memory(failure);
        return NULL;
    }
    if (settings->channel_binding == TRL_CHANNEL_BINDING_REQUIRE && !(binds && secured)) {
        trl_mechanism_free(mechanism);
        trl_failure_set(failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_INVALID_AUTHORIZATION,
                        "channel binding is required: SCRAM over TLS alone binds one");
        return NULL;
    }
    return mechanism;
}

/* Whether the server is of the role the settings ask; else the connection
   is refused, saying why (SQLSTATE 08001). */
static bool of_role(const trl_settings *settings, trl_conn *conn, trl_failure *failure) {
    if (settings->target == TRL_TARGET_ANY) {
        return true;
    }
    trl_server_role wanted = settings->target == TRL_TARGET_READ_WRITE ? TRL_SERVER_READ_WRITE : TRL_SERVER_READ_ONLY;
    trl_server_role found = TRL_SERVER_UNKNOWN;
    if (trl_conn_role(conn, &found) != TRL_OK) {
        trl_failure_copy(failure, trl_conn_failure(conn));
        return false;
    }
    if (found == wanted) {
        return true;
    }
    static const char *const described[] = {"read-write", "read-only", "of a role this session cannot see"};
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "the server is %s, and %s is asked", described[found], described[wanted]);
    trl_settings_unable(failure, message);
    return false;
}

/* A connection over `target`, or `plain` if TLS cannot be begun under
   `prefer`, logged in and of the role asked; null, the reason noted, if
   it was not kept. */
static trl_conn *attempted(attempt *a, size_t host, const char *at, const trl_target *target, const trl_target *plain) {
    trl_tls_channel channel;
    bool opened_now = false;
    trl_conn *conn = trl_conn_open(target, plain, a->settings->connect_ms, &channel, &opened_now);
    if (conn == NULL) {
        trl_failure no_memory;
        trl_failure_init(&no_memory);
        trl_failure_no_memory(&no_memory);
        note(&a->tried, at, &no_memory);
        trl_failure_clear(&no_memory);
        return NULL;
    }
    trl_failure why;
    trl_failure_init(&why);
    trl_mechanism *mechanism = opened_now ? proof(a, host, trl_conn_over_tls(conn), &why) : NULL;
    bool kept = mechanism != NULL &&
                trl_conn_log_in(conn, a->settings->laid[TRL_KEY_USER], mechanism, a->options, &channel) == TRL_OK;
    if (mechanism != NULL && !kept) {
        trl_failure_copy(&why, trl_conn_failure(conn));
    }
    kept = kept && of_role(a->settings, conn, &why);
    trl_tls_channel_free(&channel);
    trl_mechanism_free(mechanism);
    if (!kept) {
        note(&a->tried, at, opened_now ? &why : trl_conn_failure(conn));
        trl_failure_clear(&why);
        trl_conn_free(conn);
        return NULL;
    }
    trl_failure_clear(&why);
    return conn;
}

/* A connection to the address `address` of the host at `host`, as the
   settings' TLS mode says. */
static trl_conn *at_address(attempt *a, size_t host, const trl_address *address) {
    const trl_host *h = &a->settings->hosts[host];
    char at[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(at, sizeof at, "%s (%s)", h->name, address->shown);
    const char *server_name = a->settings->laid[TRL_KEY_SERVER_NAME];
    trl_target *plain = NULL;
    trl_target *secured = NULL;
    trl_status made = trl_target_tcp(address->host, h->port, &plain);
    if (made == TRL_OK && a->tls != NULL) {
        made = trl_target_tls(address->host, h->port, server_name != NULL ? server_name : h->name, a->tls, &secured);
    }
    trl_conn *conn = NULL;
    if (made == TRL_OK) {
        bool prefer = a->settings->tls_mode == TRL_TLS_PREFER;
        conn = secured == NULL ? attempted(a, host, at, plain, NULL)
                               : attempted(a, host, at, secured, prefer ? plain : NULL);
    } else {
        const trl_target *failed = secured != NULL ? secured : plain;
        trl_failure no_memory;
        trl_failure_init(&no_memory);
        trl_failure_no_memory(&no_memory);
        note(&a->tried, at, failed != NULL ? trl_target_failure(failed) : &no_memory);
        trl_failure_clear(&no_memory);
    }
    trl_target_free(plain);
    trl_target_free(secured);
    return conn;
}

/* A connection to the host at `host`: its local socket, or each address
   its name resolves to in turn. */
static trl_conn *at_host(attempt *a, size_t host) {
    const trl_host *h = &a->settings->hosts[host];
    if (h->local) {
        trl_target *local = NULL;
        trl_conn *conn = NULL;
        if (trl_target_local(h->name, &local) == TRL_OK) {
            conn = attempted(a, host, h->name, local, NULL);
        } else {
            trl_failure no_memory;
            trl_failure_init(&no_memory);
            trl_failure_no_memory(&no_memory);
            note(&a->tried, h->name, local != NULL ? trl_target_failure(local) : &no_memory);
            trl_failure_clear(&no_memory);
        }
        trl_target_free(local);
        return conn;
    }
    trl_address *addresses = NULL;
    size_t count = 0U;
    trl_io_error error;
    memset(&error, 0, sizeof error);
    if (!trl_tcp_resolve(h->name, h->port, &addresses, &count, &error)) {
        char at[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(at, sizeof at, "%s:%u", h->name, (unsigned int)h->port);
        trl_failure why;
        trl_failure_init(&why);
        trl_io_failed(&why, "resolve the name", &error);
        note(&a->tried, at, &why);
        trl_failure_clear(&why);
        return NULL;
    }
    trl_conn *conn = NULL;
    for (size_t i = 0U; conn == NULL && i < count; ++i) {
        conn = at_address(a, host, &addresses[i]);
    }
    free(addresses);
    return conn;
}

/* What a connection of the settings asks for as it opens: every
   capability but compression, its program's name as the attribute
   `application`, and the connect timeout. */
static trl_status options_of(const trl_settings *settings, trl_conn_options **out) {
    trl_status made = trl_conn_options_new(out);
    const char *name = settings->laid[TRL_KEY_APPLICATION_NAME];
    if (made == TRL_OK && name != NULL) {
        made = trl_conn_options_attribute(*out, TRL_APPLICATION, name);
    }
    return made == TRL_OK ? trl_conn_options_timeouts(*out, settings->connect_ms, 0U) : made;
}

/* Tries each host in the settings' order; the connection kept, or null. */
static trl_conn *each_host(attempt *a, size_t *order) {
    size_t count = a->settings->host_count;
    for (size_t i = 0U; i < count; ++i) {
        order[i] = i;
    }
    if (a->settings->load_balance == TRL_LOAD_BALANCE_RANDOM) {
        trl_shuffle(order, count, seed());
    }
    trl_conn *conn = NULL;
    for (size_t i = 0U; conn == NULL && i < count; ++i) {
        conn = at_host(a, order[i]);
    }
    return conn;
}

trl_status trl_settings_connect(const trl_settings *settings, trl_conn **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (settings == NULL || out == NULL || settings->laid[TRL_KEY_USER] == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    attempt a;
    memset(&a, 0, sizeof a);
    a.settings = settings;
    trl_buffer_init(&a.tried.reasons);
    trl_failure_init(&a.tried.last);
    size_t *order = trl_calloc(settings->host_count == 0U ? 1U : settings->host_count, sizeof *order);
    trl_failure why;
    trl_failure_init(&why);
    trl_conn *conn = NULL;
    if (order == NULL || options_of(settings, &a.options) != TRL_OK) {
        trl_failure_no_memory(&why);
    } else if (tls_made(settings, &a.tls, &why)) {
        conn = each_host(&a, order);
        if (conn == NULL) {
            none_kept(&a.tried, &why);
        }
    }
    free(order);
    trl_conn_options_free(a.options);
    trl_tls_free(a.tls);
    trl_buffer_free(&a.tried.reasons);
    trl_failure_clear(&a.tried.last);
    if (conn != NULL) {
        trl_failure_clear(&why);
        *out = conn;
        return TRL_OK;
    }
    trl_status status = trl_failure_sqlcode(&why) == TRL_SQLCODE_RESOURCES ? TRL_NO_MEMORY : TRL_FAILED;
    *out = status == TRL_FAILED ? trl_conn_failed_with(&why) : NULL;
    trl_failure_clear(&why);
    return *out == NULL && status == TRL_FAILED ? TRL_NO_MEMORY : status;
}
