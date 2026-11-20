/**
 * @file fake.c
 * @brief A server of a test's own over a real socket, on POSIX systems.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "fake.h"

#include "codec/buffer.h"
#include "codec/frame.h"
#include "codec/kinds.h"
#include "script.h"

/* The platform's headers declare more than the include checker can map
   to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <arpa/inet.h> // IWYU pragma: keep
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef TRL_HAVE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#define BACKLOG 4
#define CHUNK 512U
#define PATH_BYTES 1024U

/* One connection the fake took: its socket, and its TLS if over TLS. */
typedef struct peer {
    int fd;
    void *ssl;
} peer;

/* Reads from the peer, through TLS if over it. */
static ssize_t peer_read(const peer *p, void *into, size_t capacity) {
#ifdef TRL_HAVE_TLS
    if (p->ssl != NULL) {
        int got = SSL_read((SSL *)p->ssl, into, (int)capacity);
        return got > 0 ? (ssize_t)got : 0;
    }
#endif
    return read(p->fd, into, capacity);
}

/* Writes to the peer, through TLS if over it. */
static void put(const peer *p, const void *bytes, size_t length) {
#ifdef TRL_HAVE_TLS
    if (p->ssl != NULL) {
        (void)SSL_write((SSL *)p->ssl, bytes, (int)length);
        return;
    }
#endif
    ssize_t wrote = write(p->fd, bytes, length);
    (void)wrote;
}

/* Reads one whole frame's header and body, keeping a Hello's body in
   `f`; its kind, or 0 at the end. */
static uint8_t read_frame(fake *f, const peer *p) {
    uint8_t head[TRL_FRAME_HEADER_BYTES];
    size_t had = 0U;
    while (had < sizeof head) {
        ssize_t got = peer_read(p, head + had, sizeof head - had);
        if (got <= 0) {
            return 0U;
        }
        had += (size_t)got;
    }
    trl_frame_header header = {0U, 0U};
    (void)trl_frame_header_decode(head, sizeof head, UINT32_MAX, &header);
    uint8_t chunk[CHUNK];
    bool keeping = header.kind == TRL_KIND_HELLO && f->hello_length == 0U;
    for (size_t left = header.length; left > 0U;) {
        ssize_t got = peer_read(p, chunk, left < sizeof chunk ? left : sizeof chunk);
        if (got <= 0) {
            return 0U;
        }
        size_t room = sizeof f->hello - f->hello_length;
        size_t kept = (size_t)got < room ? (size_t)got : room;
        if (keeping && kept > 0U) {
            memcpy(f->hello + f->hello_length, chunk, kept);
            f->hello_length += kept;
        }
        left -= (size_t)got;
    }
    return header.kind;
}

/* Reads until the client closes. */
static void drain(const peer *p) {
    uint8_t chunk[CHUNK];
    while (peer_read(p, chunk, sizeof chunk) > 0) {
    }
}

/* Answers a login with Ready of `ping`, then reads until Terminate or the end. */
static void answer(fake *f, const peer *p) {
    static const char *const granted[] = {"ping"};
    script s;
    script_init(&s, NULL, NULL);
    script_ready(&s, "ANN", granted, 1U);
    put(p, s.to_client.data, s.to_client.length);
    script_free(&s);
    uint8_t kind = 0U;
    do {
        kind = read_frame(f, p);
        if (kind == TRL_KIND_PING) {
            static const uint8_t pong[] = {TRL_KIND_PONG, 0, 0, 0, 0};
            put(p, pong, sizeof pong);
        }
    } while (kind != 0U && kind != TRL_KIND_TERMINATE);
}

/* Begins TLS with the peer, over a fake of TLS; false if it fails. */
static bool secure(fake *f, peer *p) {
#ifdef TRL_HAVE_TLS
    if (f->tls != NULL) {
        p->ssl = SSL_new((SSL_CTX *)f->tls);
        if (p->ssl == NULL || SSL_set_fd((SSL *)p->ssl, p->fd) != 1 || SSL_accept((SSL *)p->ssl) != 1) {
            ERR_clear_error();
            return false;
        }
    }
#else
    (void)f;
    (void)p;
#endif
    return true;
}

/* Ends TLS with the peer, if over it, and closes it. */
static void end(peer *p) {
#ifdef TRL_HAVE_TLS
    if (p->ssl != NULL) {
        (void)SSL_shutdown((SSL *)p->ssl);
        SSL_free((SSL *)p->ssl);
        ERR_clear_error();
    }
#endif
    (void)close(p->fd);
}

/* Takes one connection as the mode says. */
static void take(fake *f, int i, peer *p) {
    if (f->mode == FAKE_READY || f->mode == FAKE_READY_HOLDING) {
        uint8_t kind = read_frame(f, p);
        f->first[i] = kind;
        if (kind == TRL_KIND_HELLO) {
            answer(f, p);
        } else if (f->mode == FAKE_READY_HOLDING) {
            drain(p);
        }
    } else if (f->mode == FAKE_SILENT) {
        drain(p);
    }
}

/* Takes each connection as the mode says. */
static void *serve(void *argument) {
    fake *f = argument;
    for (int i = 0; i < f->connections; ++i) {
        struct pollfd waiting[2] = {{f->listener, POLLIN, 0}, {f->stop[0], POLLIN, 0}};
        /* poll's events are a short, as POSIX gives them. */
        if (poll(waiting, 2U, -1) < 1 || (waiting[0].revents & POLLIN) == 0) { // NOLINT(bugprone-signed-bitwise)
            break;
        }
        peer p = {accept(f->listener, NULL, NULL), NULL};
        if (p.fd < 0) {
            break;
        }
        if (secure(f, &p)) {
            take(f, i, &p);
        }
        end(&p);
    }
    return NULL;
}

/* Listens on TCP at 127.0.0.1, at an ephemeral port. */
static bool listen_tcp(fake *f) {
    f->listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof address;
    if (f->listener < 0 || bind(f->listener, (const struct sockaddr *)&address, sizeof address) != 0 ||
        listen(f->listener, BACKLOG) != 0 || getsockname(f->listener, (struct sockaddr *)&address, &length) != 0) {
        return false;
    }
    f->port = ntohs(address.sin_port);
    return true;
}

/* Listens at a Unix socket. */
static bool listen_local(fake *f, const char *path) {
    (void)snprintf(f->path, sizeof f->path, "%s", path);
    (void)unlink(path);
    f->listener = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    (void)snprintf(address.sun_path, sizeof address.sun_path, "%s", path);
    return f->listener >= 0 && bind(f->listener, (const struct sockaddr *)&address, sizeof address) == 0 &&
           listen(f->listener, BACKLOG) == 0;
}

/* The fake writes to clients that may have closed: such a write fails,
   rather than ending the test by SIGPIPE. */
static void ignore_sigpipe(void) {
    struct sigaction ignore;
    memset(&ignore, 0, sizeof ignore);
    ignore.sa_handler = SIG_IGN;
    (void)sigaction(SIGPIPE, &ignore, NULL);
}

bool fake_start(fake *f, fake_mode mode, int connections, const char *path) {
    ignore_sigpipe();
    memset(f, 0, sizeof *f);
    f->mode = mode;
    f->connections = connections;
    bool listening = path == NULL ? listen_tcp(f) : listen_local(f, path);
    return listening && pipe(f->stop) == 0 && pthread_create(&f->thread, NULL, serve, f) == 0;
}

/* Tells the fake's thread to stop waiting for a connection. */
static void put_stop(const fake *f) {
    ssize_t wrote = write(f->stop[1], "x", 1U);
    (void)wrote;
}

#ifdef TRL_HAVE_TLS
bool fake_start_tls(fake *f, fake_mode mode, int connections, const char *dir, bool tls12) {
    ignore_sigpipe();
    memset(f, 0, sizeof *f);
    f->mode = mode;
    f->connections = connections;
    char chain[PATH_BYTES];
    char key[PATH_BYTES];
    (void)snprintf(chain, sizeof chain, "%s/server.pem", dir);
    (void)snprintf(key, sizeof key, "%s/server.key", dir);
    SSL_CTX *context = SSL_CTX_new(TLS_server_method());
    f->tls = context;
    if (context == NULL || SSL_CTX_use_certificate_chain_file(context, chain) != 1 ||
        SSL_CTX_use_PrivateKey_file(context, key, SSL_FILETYPE_PEM) != 1 ||
        (tls12 && SSL_CTX_set_max_proto_version(context, TLS1_2_VERSION) != 1)) {
        return false;
    }
    return listen_tcp(f) && pipe(f->stop) == 0 && pthread_create(&f->thread, NULL, serve, f) == 0;
}
#endif

void fake_stop(fake *f) {
    /* A connection the test never made is waited for no longer. */
    put_stop(f);
    (void)pthread_join(f->thread, NULL);
    (void)close(f->stop[0]);
    (void)close(f->stop[1]);
#ifdef TRL_HAVE_TLS
    SSL_CTX_free((SSL_CTX *)f->tls);
#endif
    (void)close(f->listener);
    if (f->path[0] != '\0') {
        (void)unlink(f->path);
    }
}

uint16_t fake_closed_port(void) {
    fake f;
    memset(&f, 0, sizeof f);
    if (!listen_tcp(&f)) {
        return 1U;
    }
    (void)close(f.listener);
    return f.port;
}
// NOLINTEND(misc-include-cleaner)
