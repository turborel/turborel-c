/**
 * @file fake.h
 * @brief A server of a test's own over a real socket, on a thread of its
 * own: it answers a login with Ready, or never answers, or closes at
 * once, and takes a Cancel.
 */
#ifndef TRL_TEST_NET_FAKE_H
#define TRL_TEST_NET_FAKE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The longest path of a fake's Unix socket. */
#define FAKE_PATH_BYTES 104U

/** @brief What a fake does with each connection it takes. */
typedef enum fake_mode {
    /** @brief Reads Hello, sends Ready, reads to the end; a Cancel it reads and closes. */
    FAKE_READY = 0,
    /** @brief As FAKE_READY, but a Cancel is held open, unanswered. */
    FAKE_READY_HOLDING,
    /** @brief Sends nothing, and reads to the end. */
    FAKE_SILENT,
    /** @brief Closes at once. */
    FAKE_CLOSE,
} fake_mode;

/** @brief A fake server. */
typedef struct fake {
    /** @brief What it does. */
    fake_mode mode;
    /** @brief Its listening socket. */
    int listener;
    /** @brief Its TCP port, or 0 of a Unix socket. */
    uint16_t port;
    /** @brief Its Unix socket's path, if it listens at one. */
    char path[FAKE_PATH_BYTES];
    /** @brief How many connections it takes before it ends. */
    int connections;
    /** @brief The kinds of the first message of each, in order. */
    uint8_t first[4];
    /** @brief The first Hello's body, as much as it holds. */
    uint8_t hello[256];
    /** @brief Its bytes kept. */
    size_t hello_length;
    /** @brief The TLS library's server context, of a fake over TLS; else null. */
    void *tls;
    /** @brief A pipe whose writing tells the fake to stop waiting. */
    int stop[2];
    /** @brief Its thread. */
    pthread_t thread;
} fake;

/** @brief Starts a fake on TCP at an ephemeral port of 127.0.0.1, or at `path` if not null. */
bool fake_start(fake *f, fake_mode mode, int connections, const char *path);

/**
 * @brief Starts a fake over TLS on TCP, presenting `dir`/server.pem and
 * server.key, of TLS 1.2 at most if `tls12`; built with the TLS library alone.
 */
bool fake_start_tls(fake *f, fake_mode mode, int connections, const char *dir, bool tls12);

/** @brief Waits for the fake's thread to end, and closes it. */
void fake_stop(fake *f);

/** @brief A TCP port nothing listens at. */
uint16_t fake_closed_port(void);

#endif
