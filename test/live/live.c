/**
 * @file live.c
 * @brief What the live tests share.
 */
#include "live.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/requests.h"
#include "conn/conn.h"
#include "failure.h"
#include "server.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

server live_server;

const char *live_path(const char *name) {
    static char path[SERVER_PATH_BYTES];
    server_path(&live_server, name, path, sizeof path);
    return path;
}

trl_tls *live_tls(bool present) {
    trl_tls *tls = NULL;
    if (trl_tls_new(&tls) != TRL_OK || trl_tls_trust_file(tls, live_path("ca.pem")) != TRL_OK) {
        trl_tls_free(tls);
        return NULL;
    }
    char chain[SERVER_PATH_BYTES];
    (void)snprintf(chain, sizeof chain, "%s", live_path("client.pem"));
    if (present && trl_tls_present(tls, chain, live_path("client.key")) != TRL_OK) {
        trl_tls_free(tls);
        return NULL;
    }
    return tls;
}

trl_conn *live_connect(trl_target *target, trl_mechanism *mechanism, const trl_conn_options *options) {
    trl_conn *conn = NULL;
    if (trl_connect(target, "ann", mechanism, options, &conn) != TRL_OK) {
        char text[SERVER_PATH_BYTES];
        (void)trl_failure_text(trl_conn_failure(conn), text, sizeof text);
        (void)fprintf(stderr, "connect failed: %s\n", text);
        trl_conn_free(conn);
        return NULL;
    }
    return conn;
}

trl_conn *live_trusted(const trl_conn_options *options) {
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    (void)trl_target_tcp("127.0.0.1", SERVER_PORT, &target);
    (void)trl_mechanism_trust(&trust);
    trl_conn *conn = live_connect(target, trust, options);
    trl_mechanism_free(trust);
    trl_target_free(target);
    return conn;
}

trl_status live_run(trl_conn *conn, const char *sql, uint64_t *rows) {
    trl_query query;
    memset(&query, 0, sizeof query);
    query.sql.bytes = (const uint8_t *)sql;
    query.sql.length = strlen(sql);
    trl_codec_error error = {0, "", ""};
    if (!trl_encode_query(&conn->out, &query, &error) || !trl_conn_send(conn)) {
        return TRL_FAILED;
    }
    for (;;) {
        trl_reply reply;
        if (!trl_conn_receive(conn, &reply)) {
            return TRL_FAILED;
        }
        if (reply.kind == TRL_KIND_COMPLETE) {
            if (rows != NULL && reply.as.complete.has_rows) {
                *rows = reply.as.complete.rows;
            }
            return TRL_OK;
        }
        if (reply.kind == TRL_KIND_ERROR) {
            trl_failure_from_wire(&conn->failure, &reply.as.error);
            return TRL_FAILED;
        }
    }
}
