/**
 * @file settings_test.c
 * @brief Connecting by settings against a real server (RULES.md R-TEST-8):
 * by URI and options, its program's name as a connection attribute; the
 * role asked; several servers, the unreachable passed for the next; TLS
 * by mode and its certificate checks; SCRAM by the password file, over
 * TLS and refused over plain TCP, its key cache shared; channel binding
 * required; a client certificate; a Unix socket; and `prefer` falling
 * back to plain TCP where a server's handshake fails.
 */
#include "harness.h"
#include "live.h"
#include "server.h"
#include "turborel/turborel.h"

/* The platform declares its sockets and threads in headers of its own,
   which the include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SKIPPED 77
#define PATH_BYTES 1024U
#define URI_BYTES 2048U

/* Runs `sql`, freeing its result; whether it succeeded. */
static bool run(trl_conn *conn, const char *sql) {
    trl_result *result = NULL;
    trl_status ran = trl_query(conn, sql, &result);
    trl_result_free(result);
    return ran == TRL_OK;
}

/* Connects by the URI `uri`; the connection, or null, saying why into
   `sqlstate` and `message` if given. */
static trl_conn *by_uri(const char *uri, char *sqlstate, char *message) {
    trl_settings *settings = NULL;
    trl_conn *conn = NULL;
    trl_status status = trl_settings_from_uri(uri, &settings);
    const trl_failure *failure = trl_settings_failure(settings);
    if (status == TRL_OK) {
        status = trl_settings_connect(settings, &conn);
        failure = trl_conn_failure(conn);
    }
    if (status != TRL_OK) {
        if (sqlstate != NULL) {
            (void)snprintf(sqlstate, 8U, "%s", trl_failure_sqlstate(failure));
        }
        if (message != NULL) {
            (void)snprintf(message, PATH_BYTES, "%s", trl_failure_message(failure));
        }
        trl_conn_free(conn);
        conn = NULL;
    }
    trl_settings_free(settings);
    return conn;
}

/* Whether the URI `uri` connects. */
static bool connects(const char *uri) {
    char sqlstate[8];
    char message[PATH_BYTES];
    trl_conn *conn = by_uri(uri, sqlstate, message);
    if (conn == NULL) {
        (void)fprintf(stderr, "%s: %s %s\n", uri, sqlstate, message);
    }
    trl_conn_free(conn);
    return conn != NULL;
}

/* Whether the URI `uri` fails with `sqlstate`, its message holding `part`. */
static bool fails_as(const char *uri, const char *sqlstate, const char *part) {
    char state[8] = "";
    char message[PATH_BYTES] = "";
    trl_conn *conn = by_uri(uri, state, message);
    bool same = conn == NULL && strcmp(state, sqlstate) == 0 && strstr(message, part) != NULL;
    if (!same) {
        (void)fprintf(stderr, "%s: %s %s\n", uri, state, message);
    }
    trl_conn_free(conn);
    return same;
}

/* A URI of the server, `query` its settings after `?`. */
static const char *uri_of(const char *hosts, const char *query) {
    static char uri[URI_BYTES];
    (void)snprintf(uri, sizeof uri, "turborel://ann@%s/?%s", hosts, query);
    return uri;
}

/**
 * Verifies: [CLIENT Connecting by settings] settings of a URI connect,
 * giving a connection like a target's; `application_name` is sent as the
 * connection attribute `application`; a server alone is read-write, kept
 * of `target=read-write` and refused of `read-only` (SQLSTATE 08001)
 * [CLIENT Several servers].
 */
static void a_uri_connects_and_names_its_program(trl_test *test) {
    trl_conn *conn = by_uri(uri_of("127.0.0.1:17438", "tls_mode=disable&application_name=settings-test"), NULL, NULL);
    TRL_CHECK(test, conn != NULL && strcmp(trl_conn_authorization(conn), "ANN") == 0);
    trl_result *result = NULL;
    TRL_CHECK(test, trl_query(conn,
                              "SELECT ATTRIBUTE_VALUE FROM INFORMATION_SCHEMA.CONNECTION_ATTRIBUTES "
                              "WHERE ATTRIBUTE_NAME = 'application'",
                              &result) == TRL_OK);
    size_t length = 0U;
    const char *name = trl_value_text(trl_result_value(result, 0U, 0U), &length);
    TRL_CHECK(test, name != NULL && length == strlen("settings-test") && memcmp(name, "settings-test", length) == 0);
    trl_result_free(result);
    trl_server_role role = TRL_SERVER_UNKNOWN;
    TRL_CHECK(test, trl_conn_role(conn, &role) == TRL_OK && role == TRL_SERVER_READ_WRITE);
    TRL_CHECK(test, run(conn, "ROLLBACK"));
    trl_conn_free(conn);
    TRL_CHECK(test, connects(uri_of("127.0.0.1:17438", "tls_mode=disable&target=read-write")));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", "tls_mode=disable&target=read-only"), "08001",
                             "the server is read-write, and read-only is asked"));
}

/**
 * Verifies: [CLIENT Several servers] hosts tried in the order given, at
 * each address a name resolves to, one that cannot be reached passed for
 * the next; with none kept, the failure names each address and why
 * (SQLSTATE 08001); in an order drawn at random, one is kept.
 */
static void several_servers_are_tried_in_turn(trl_test *test) {
    TRL_CHECK(test, connects(uri_of("127.0.0.1:1,127.0.0.1:17438", "tls_mode=disable")));
    TRL_CHECK(test, connects(uri_of("localhost:17438,127.0.0.1:17438", "tls_mode=disable&load_balance=random")));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:1,127.0.0.1:2", "tls_mode=disable"), "08001",
                             "no server was kept: 127.0.0.1 (127.0.0.1:1): cannot connect"));
    TRL_CHECK(test,
              fails_as(uri_of("no-such-host.invalid:17438", "tls_mode=disable"), "08006", "cannot resolve the name"));
}

/**
 * Verifies: [CLIENT TLS by mode] `require` begins TLS, its certificate not
 * checked; `verify-ca` checks it against the authorities trusted, not its
 * name; `verify-full` its name too, `server_name`'s if given; an authority
 * not the server's, or none trusted, refuses it.
 */
static void tls_by_mode(trl_test *test) {
    char query[URI_BYTES];
    TRL_CHECK(test, connects(uri_of("127.0.0.1:17438", "tls_mode=require")));
    (void)snprintf(query, sizeof query, "tls_mode=verify-full&tls_roots=%s", live_path("ca.pem"));
    TRL_CHECK(test, connects(uri_of("localhost:17438", query)));
    (void)snprintf(query, sizeof query, "tls_mode=verify-ca&tls_roots=%s&server_name=other.example",
                   live_path("ca.pem"));
    TRL_CHECK(test, connects(uri_of("127.0.0.1:17438", query)));
    (void)snprintf(query, sizeof query, "tls_mode=verify-full&tls_roots=%s&server_name=other.example",
                   live_path("ca.pem"));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", query), "08006", "certificate verify failed"));
    (void)snprintf(query, sizeof query, "tls_mode=verify-full&tls_roots=%s", live_path("other-ca.pem"));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", query), "08006", "certificate verify failed"));
    TRL_CHECK(test, fails_as(uri_of("localhost:17438", "tls_mode=verify-ca"), "08006",
                             "no certificate authority is trusted"));
    (void)snprintf(query, sizeof query, "tls_mode=verify-full&tls_roots=%s", live_path("none.pem"));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", query), "08006", "cannot read"));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", "tls_mode=verify-ca&tls_system_roots=true"), "08006",
                             "certificate verify failed"));
}

/**
 * Verifies: [CLIENT Connecting by settings] a password is proved by
 * SCRAM-SHA-256, of the settings or the password file, over TLS; refused
 * over plain TCP before anything is sent (SQLSTATE 28000); the settings'
 * key cache shared by their connections [CLIENT The SCRAM key cache];
 * channel binding required refuses a connection not over TLS (SQLSTATE
 * 28000) and binds one over it [CLIENT TLS by mode].
 */
static void scram_by_password_file(trl_test *test) {
    char passfile[PATH_BYTES];
    (void)snprintf(passfile, sizeof passfile, "%s", live_path("passfile"));
    TRL_CHECK(test, trl_test_write_file(passfile, "127.0.0.1:17438:ann:" SERVER_PASSWORD "\n"));
    char query[URI_BYTES];
    (void)snprintf(query, sizeof query, "tls_mode=require&passfile=%s", passfile);
    trl_settings *settings = NULL;
    TRL_CHECK(test, trl_settings_from_uri(uri_of("127.0.0.1:17438", query), &settings) == TRL_OK);
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_settings_connect(settings, &conn) == TRL_OK);
    trl_conn_free(conn);
    TRL_CHECK(test, trl_settings_connect(settings, &conn) == TRL_OK);
    trl_conn_free(conn);
    uint64_t hits = 0U;
    uint64_t misses = 0U;
    trl_key_cache_lookups(trl_settings_key_cache(settings), &hits, &misses);
    TRL_CHECK(test, hits == 1U && misses == 1U);
    trl_settings_free(settings);
    (void)snprintf(query, sizeof query, "tls_mode=disable&passfile=%s", passfile);
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", query), "28000", "is sent only over TLS"));
    (void)snprintf(query, sizeof query, "tls_mode=disable&channel_binding=require&passfile=%s", passfile);
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", query), "28000", "channel binding is required"));
    (void)snprintf(query, sizeof query, "tls_mode=require&channel_binding=require&passfile=%s", passfile);
    TRL_CHECK(test, connects(uri_of("127.0.0.1:17438", query)));
    TRL_CHECK(test, fails_as(uri_of("127.0.0.1:17438", "tls_mode=require&channel_binding=require"), "28000",
                             "channel binding is required"));
    (void)snprintf(query, sizeof query, "tls_mode=require&channel_binding=disable&passfile=%s", passfile);
    TRL_CHECK(test, connects(uri_of("127.0.0.1:17438", query)));
    (void)remove(passfile);
    static const char wrong[] = "turborel://ann:wrong@127.0.0.1:17438,localhost:17438/?tls_mode=require";
    TRL_CHECK(test, fails_as(wrong, "28000", "no server was kept: 127.0.0.1 (127.0.0.1:17438): "));
    (void)snprintf(query, sizeof query, "tls_mode=disable&passfile=%s", live_path(""));
    TRL_CHECK(test, connects(uri_of("127.0.0.1:17438", query)));
}

/**
 * Verifies: [CLIENT Connecting by settings] with no password, the
 * certificate named is presented over TLS and proves the user; one named
 * without its key is refused; a Unix socket uses no TLS whatever the mode
 * [CLIENT TLS by mode].
 */
static void a_certificate_and_a_socket(trl_test *test) {
    char query[URI_BYTES];
    char roots[PATH_BYTES];
    char chain[PATH_BYTES];
    (void)snprintf(roots, sizeof roots, "%s", live_path("ca.pem"));
    (void)snprintf(chain, sizeof chain, "%s", live_path("client.pem"));
    (void)snprintf(query, sizeof query, "tls_mode=verify-full&tls_roots=%s&tls_certificate=%s&tls_key=%s", roots, chain,
                   live_path("client.key"));
    TRL_CHECK(test, connects(uri_of("localhost:17438", query)));
    (void)snprintf(query, sizeof query, "tls_mode=require&tls_certificate=%s", live_path("client.pem"));
    TRL_CHECK(test, fails_as(uri_of("localhost:17438", query), "08006", "presented with its key"));
    (void)snprintf(query, sizeof query, "tls_mode=require&tls_certificate=%s&tls_key=%s", live_path("none.pem"), chain);
    TRL_CHECK(test, fails_as(uri_of("localhost:17438", query), "08006", "cannot read"));
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    (void)trl_options_set(options, TRL_SETTING_USER, "ann");
    (void)trl_options_set(options, TRL_SETTING_HOST, live_path("turborel.sock"));
    (void)trl_options_set(options, TRL_SETTING_TLS_MODE, "verify-full");
    trl_settings *settings = NULL;
    trl_conn *conn = NULL;
    TRL_CHECK(test, trl_settings_resolve(options, &settings) == TRL_OK);
    (void)trl_options_set(options, TRL_SETTING_TLS_ROOTS, live_path("ca.pem"));
    trl_settings_free(settings);
    TRL_CHECK(test, trl_settings_resolve(options, &settings) == TRL_OK);
    TRL_CHECK(test, trl_settings_connect(settings, &conn) == TRL_OK);
    trl_conn_free(conn);
    trl_settings_free(settings);
    trl_options_free(options);
}

/* A server of the test's own between the client and the real one: the
   first connection closed as it comes, so its TLS handshake fails; the
   second passed to the real server and back. */
typedef struct proxy {
    int listener;
    uint16_t port;
    int accepted;
    pthread_t thread;
} proxy;

/* Passes bytes each way between `a` and `b` until either ends. */
static void pump(int a, int b) {
    struct pollfd fds[2] = {{a, POLLIN, 0}, {b, POLLIN, 0}};
    char buffer[4096];
    for (;;) {
        if (poll(fds, 2U, 10000) <= 0) {
            return;
        }
        for (size_t i = 0U; i < 2U; ++i) {
            if (((unsigned int)fds[i].revents & ((unsigned int)POLLIN | (unsigned int)POLLHUP)) != 0U) {
                ssize_t got = read(fds[i].fd, buffer, sizeof buffer);
                if (got <= 0 || write(fds[1U - i].fd, buffer, (size_t)got) != got) {
                    return;
                }
            }
        }
    }
}

static void *serve(void *context) {
    proxy *p = context;
    for (int n = 0; n < 2; ++n) {
        int client = accept(p->listener, NULL, NULL);
        if (client < 0) {
            return NULL;
        }
        ++p->accepted;
        if (n == 0) {
            (void)close(client);
            continue;
        }
        int upstream = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in address;
        memset(&address, 0, sizeof address);
        address.sin_family = AF_INET;
        address.sin_port = htons((uint16_t)SERVER_PORT);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (upstream >= 0 && connect(upstream, (struct sockaddr *)&address, sizeof address) == 0) {
            pump(client, upstream);
        }
        (void)close(upstream);
        (void)close(client);
    }
    return NULL;
}

/* Starts the proxy on a port the system gives; whether it started. */
static bool proxy_start(proxy *p) {
    memset(p, 0, sizeof *p);
    p->listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof address;
    if (p->listener < 0 || bind(p->listener, (struct sockaddr *)&address, sizeof address) != 0 ||
        listen(p->listener, 4) != 0 || getsockname(p->listener, (struct sockaddr *)&address, &length) != 0) {
        return false;
    }
    p->port = ntohs(address.sin_port);
    return pthread_create(&p->thread, NULL, serve, p) == 0;
}

/**
 * Verifies: [CLIENT TLS by mode] under `prefer`, a server reached whose
 * TLS handshake fails is connected to again over plain TCP.
 */
static void prefer_falls_back_to_plain_tcp(trl_test *test) {
    proxy p;
    bool started = proxy_start(&p);
    TRL_CHECK(test, started);
    if (!started) {
        return;
    }
    char hosts[64];
    (void)snprintf(hosts, sizeof hosts, "127.0.0.1:%u", (unsigned int)p.port);
    TRL_CHECK(test, connects(uri_of(hosts, "tls_mode=prefer")));
    (void)pthread_join(p.thread, NULL);
    (void)close(p.listener);
    TRL_CHECK(test, p.accepted == 2);
}

int main(int argc, char **argv) {
    char cwd[PATH_BYTES];
    char dir[PATH_BYTES + sizeof "/live-settings"];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        return EXIT_FAILURE;
    }
    (void)snprintf(dir, sizeof dir, "%s/live-settings", cwd);
    const char *why = NULL;
    if (!server_start(&live_server, dir, true, &why)) {
        (void)fprintf(stderr, "%s\n", why);
        return getenv("TRL_TEST_SERVER") == NULL ? SKIPPED : EXIT_FAILURE; // NOLINT(concurrency-mt-unsafe)
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_uri_connects_and_names_its_program),
        TRL_TEST_CASE(several_servers_are_tried_in_turn),
        TRL_TEST_CASE(tls_by_mode),
        TRL_TEST_CASE(scram_by_password_file),
        TRL_TEST_CASE(a_certificate_and_a_socket),
        TRL_TEST_CASE(prefer_falls_back_to_plain_tcp),
    };
    int result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    server_stop(&live_server);
    return result;
}
// NOLINTEND(misc-include-cleaner)
