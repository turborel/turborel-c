/**
 * @file cancel.c
 * @brief Cancel [PROTOCOL Cancel 'Z'] [CLIENT Running statements]: the
 * key a connection's Ready gave, sent alone over a connection of its own
 * to the same target, which the server closes with no reply.
 */
#include "auth/crypto.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/requests.h"
#include "conn/conn.h"
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

struct trl_canceller {
    trl_target *target;
    uint8_t key[TRL_CANCEL_KEY_BYTES];
    trl_failure failure;
};

trl_status trl_canceller_new(trl_conn *conn, trl_canceller **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (conn == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    if (conn->reach == NULL || !conn->has_key) {
        trl_failure_set(&conn->failure, TRL_SQLCODE_API, TRL_SQLSTATE_API,
                        conn->reach == NULL ? "a connection over the program's own stream has no canceller"
                                            : "a connection that never logged in has no canceller");
        return TRL_FAILED;
    }
    trl_canceller *canceller = trl_calloc(1U, sizeof *canceller);
    if (canceller == NULL) {
        return TRL_NO_MEMORY;
    }
    canceller->target = trl_target_clone(conn->reach);
    if (canceller->target == NULL) {
        free(canceller);
        return TRL_NO_MEMORY;
    }
    memcpy(canceller->key, conn->key, sizeof canceller->key);
    trl_failure_init(&canceller->failure);
    *out = canceller;
    return TRL_OK;
}

/* Says why a send or receive failed, as turborel-client's canceller says
   it: what the system said, a wait run out not told apart. */
static void plain_failure(trl_failure *failure, const char *what, const trl_io_error *error) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    (void)snprintf(message, sizeof message, "cannot %s: %s", what, error->text);
    trl_failure_broken(failure, message);
}

trl_status trl_cancel_within(trl_canceller *canceller, uint32_t within_ms) {
    if (canceller == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_transport transport;
    trl_tls_channel channel;
    if (!trl_target_open(canceller->target, within_ms, &transport, &channel, &canceller->failure)) {
        return TRL_FAILED;
    }
    trl_tls_channel_free(&channel);
    trl_buffer request;
    trl_buffer_init(&request);
    trl_codec_error error = {0, "", ""};
    trl_slice key = {canceller->key, sizeof canceller->key};
    bool sent =
        trl_encode_cancel(&request, key, &error) && trl_transport_write_all(&transport, request.data, request.length);
    trl_cleanse(request.data, request.length);
    trl_buffer_free(&request);
    if (!sent) {
        if (error.sqlcode != 0) {
            trl_failure_from_codec(&canceller->failure, &error);
        } else {
            plain_failure(&canceller->failure, "send", &transport.error);
        }
        trl_transport_close(&transport);
        return TRL_FAILED;
    }
    uint8_t rest[TRL_CANCEL_KEY_BYTES];
    int64_t got = 0;
    do {
        got = transport.ops->read(&transport, rest, sizeof rest);
    } while (got > 0);
    if (got < 0) {
        plain_failure(&canceller->failure, "receive", &transport.error);
    }
    trl_transport_close(&transport);
    return got < 0 ? TRL_FAILED : TRL_OK;
}

trl_status trl_cancel(trl_canceller *canceller) {
    return trl_cancel_within(canceller, TRL_CANCEL_WITHIN_MS);
}

const trl_failure *trl_canceller_failure(const trl_canceller *canceller) {
    return canceller == NULL ? NULL : &canceller->failure;
}

void trl_canceller_free(trl_canceller *canceller) {
    if (canceller == NULL) {
        return;
    }
    trl_target_free(canceller->target);
    trl_failure_clear(&canceller->failure);
    trl_cleanse(canceller, sizeof *canceller);
    free(canceller);
}
