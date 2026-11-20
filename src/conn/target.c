/**
 * @file target.c
 * @brief Targets [CLIENT Connecting]: TCP, a Unix socket or named pipe,
 * or TLS on TCP, each opened within the connect timeout.
 */
#include "conn/target.h"

#include "failure.h"
#include "memory.h"
#include "net/tls.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Which kind of target it is. */
typedef enum kind {
    KIND_TCP = 0,
    KIND_LOCAL,
    KIND_TLS,
} kind;

struct trl_target {
    kind kind;
    /* TCP and TLS: the host and port. */
    char *host;
    uint16_t port;
    /* Local: the socket's path, or the pipe's name. */
    char *path;
    /* TLS: the name the certificate is checked against, and the context held. */
    char *server_name;
    void *tls;
    trl_failure failure;
};

/* A copy of `text`, or null if memory cannot be had or `text` is null. */
static char *copy_of(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    size_t length = strlen(text);
    char *copy = trl_malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, text, length + 1U);
    }
    return copy;
}

/* Makes a target of `kind`, of copies of `text` as its host or path. */
static trl_status make(kind kind, const char *text, trl_target **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (text == NULL || text[0] == '\0') {
        return TRL_INVALID_ARGUMENT;
    }
    trl_target *target = trl_calloc(1U, sizeof *target);
    char *copy = copy_of(text);
    if (target == NULL || copy == NULL) {
        free(target);
        free(copy);
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&target->failure);
    target->kind = kind;
    if (kind == KIND_LOCAL) {
        target->path = copy;
    } else {
        target->host = copy;
    }
    *out = target;
    return TRL_OK;
}

trl_status trl_target_tcp(const char *host, uint16_t port, trl_target **out) {
    trl_status made = make(KIND_TCP, host, out);
    if (made == TRL_OK) {
        (*out)->port = port;
    }
    return made;
}

trl_status trl_target_local(const char *path, trl_target **out) {
    return make(KIND_LOCAL, path, out);
}

trl_status trl_target_tls(const char *host, uint16_t port, const char *server_name, trl_tls *tls, trl_target **out) {
    if (tls == NULL) {
        if (out != NULL) {
            *out = NULL;
        }
        return TRL_INVALID_ARGUMENT;
    }
    trl_status made = make(KIND_TLS, host, out);
    if (made != TRL_OK) {
        return made;
    }
    trl_target *target = *out;
    target->port = port;
    target->server_name = copy_of(server_name == NULL ? host : server_name);
    if (target->server_name == NULL) {
        trl_target_free(target);
        *out = NULL;
        return TRL_NO_MEMORY;
    }
    target->tls = trl_tls_hold(tls, &target->failure);
    return target->tls == NULL ? TRL_FAILED : TRL_OK;
}

const trl_failure *trl_target_failure(const trl_target *target) {
    return target == NULL ? NULL : &target->failure;
}

void trl_target_free(trl_target *target) {
    if (target == NULL) {
        return;
    }
    free(target->host);
    free(target->path);
    free(target->server_name);
    if (target->tls != NULL) {
        trl_tls_release(target->tls);
    }
    trl_failure_clear(&target->failure);
    free(target);
}

trl_target *trl_target_clone(const trl_target *target) {
    trl_target *copy = trl_calloc(1U, sizeof *copy);
    if (copy == NULL) {
        return NULL;
    }
    trl_failure_init(&copy->failure);
    copy->kind = target->kind;
    copy->port = target->port;
    copy->host = copy_of(target->host);
    copy->path = copy_of(target->path);
    copy->server_name = copy_of(target->server_name);
    copy->tls = target->tls == NULL ? NULL : trl_tls_context_hold(target->tls);
    bool whole = (target->host == NULL || copy->host != NULL) && (target->path == NULL || copy->path != NULL) &&
                 (target->server_name == NULL || copy->server_name != NULL) &&
                 (target->tls == NULL || copy->tls != NULL);
    if (!whole) {
        trl_target_free(copy);
        return NULL;
    }
    return copy;
}

bool trl_target_is_plain_tcp(const trl_target *target) {
    return target->kind == KIND_TCP;
}

void trl_io_failed(trl_failure *failure, const char *what, const trl_io_error *error) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    if (error->timed_out) {
        (void)snprintf(message, sizeof message, "cannot %s: the server did not answer in time (%s)", what, error->text);
    } else {
        (void)snprintf(message, sizeof message, "cannot %s: %s", what, error->text);
    }
    trl_failure_broken(failure, message);
}

bool trl_target_open(const trl_target *target, uint32_t within_ms, trl_transport *out, trl_tls_channel *channel,
                     trl_failure *failure) {
    memset(channel, 0, sizeof *channel);
    memset(out, 0, sizeof *out);
    bool opened = target->kind == KIND_LOCAL ? trl_local_open(target->path, within_ms, out)
                                             : trl_tcp_open(target->host, target->port, within_ms, out);
    if (!opened) {
        trl_io_failed(failure, "connect", &out->error);
        return false;
    }
    if (target->kind != KIND_TLS) {
        return true;
    }
    return trl_tls_secure(target->tls, target->server_name, out, channel, failure);
}
