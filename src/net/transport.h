/**
 * @file transport.h
 * @brief What a connection reads and writes through: a socket, a named
 * pipe, TLS over a socket, or a stream the program opened. Each reads,
 * writes, may bound how long each read or write waits, and closes; a
 * failure says what the system said, and whether the wait ran out.
 */
#ifndef TRL_NET_TRANSPORT_H
#define TRL_NET_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The longest text of what the system said, its null included. */
#define TRL_IO_TEXT_BYTES 256U

/** @brief What the last read, write or open that failed came to. */
typedef struct trl_io_error {
    /** @brief Whether a wait ran out. */
    bool timed_out;
    /** @brief What the system said, as "Connection refused (os error 61)". */
    char text[TRL_IO_TEXT_BYTES];
} trl_io_error;

typedef struct trl_transport trl_transport;

/** @brief A transport's functions. */
typedef struct trl_transport_ops {
    /** @brief Reads at most `capacity` bytes: how many, 0 at the end, -1 if it failed. */
    int64_t (*read)(trl_transport *transport, uint8_t *into, size_t capacity);
    /** @brief Writes up to `length` bytes: how many, at least 1, or -1 if it failed. */
    int64_t (*write)(trl_transport *transport, const uint8_t *bytes, size_t length);
    /** @brief Bounds each read and write by `ms`, 0 for none; null if its maker bounds it. */
    bool (*bound)(trl_transport *transport, uint32_t ms);
    /** @brief Closes it and frees its state. */
    void (*close)(trl_transport *transport);
    /** @brief The address it reached, as numbers, into `out`; whether it has one. Null of one with none. */
    bool (*peer)(const trl_transport *transport, char *out, size_t capacity);
} trl_transport_ops;

/** @brief A transport: its functions, its state, and its last failure. */
struct trl_transport {
    /** @brief Its functions; null once closed. */
    const trl_transport_ops *ops;
    /** @brief Its state, of its kind. */
    void *state;
    /** @brief Its last failure. */
    trl_io_error error;
};

/** @brief The bytes of an address as numbers, its null included, as of IPv6 with a zone. */
#define TRL_ADDRESS_BYTES 64U

/** @brief The address a transport over TCP reached, as numbers, into `out`; whether it has one. */
bool trl_transport_peer(const trl_transport *transport, char *out, size_t capacity);

/** @brief Writes all `length` bytes, or fails. */
bool trl_transport_write_all(trl_transport *transport, const uint8_t *bytes, size_t length);

/** @brief Closes a transport, if open. */
void trl_transport_close(trl_transport *transport);

/** @brief Sets `error`'s text, as the system's error `code` says it, with no wait run out. */
void trl_io_error_os(trl_io_error *error, int code);

/** @brief Sets `error`'s text to `text`, and whether a wait ran out. */
void trl_io_error_set(trl_io_error *error, const char *text, bool timed_out);

/**
 * @brief Opens TCP to each address `host` resolves to at `port` in turn,
 * each within `within_ms` (0 for no limit), its reads and writes bounded
 * by it too.
 */
bool trl_tcp_open(const char *host, uint16_t port, uint32_t within_ms, trl_transport *out);

/**
 * @brief Opens a Unix socket, or on Windows a named pipe, waiting for a
 * busy pipe at most `within_ms`; a socket's reads and writes are bounded
 * by it too.
 */
bool trl_local_open(const char *path, uint32_t within_ms, trl_transport *out);

/** @brief Room for an address's text, and for it shown with its port. */
#define TRL_ADDRESS_BYTES 64U
#define TRL_SHOWN_BYTES 80U

/** @brief An address a name resolves to: as it is connected to, and as it is shown, as "[::1]:7422". */
typedef struct trl_address {
    /** @brief The address, numeric, as "127.0.0.1" or "::1". */
    char host[TRL_ADDRESS_BYTES];
    /** @brief It and its port, as Rust shows a socket address. */
    char shown[TRL_SHOWN_BYTES];
} trl_address;

/**
 * @brief The addresses `host` resolves to at `port`, in the order the
 * system gives them, into `out`, the caller's to free, and how many;
 * false, with why, if none.
 */
bool trl_tcp_resolve(const char *host, uint16_t port, trl_address **out, size_t *count, trl_io_error *error);

#endif
