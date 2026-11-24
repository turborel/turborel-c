/**
 * @file socket_posix.c
 * @brief TCP and Unix sockets on Linux, macOS and the other POSIX
 * systems (RULES.md R-PLAT-2): each address tried in turn within the
 * connect timeout, each read and write bounded by the socket's own
 * timeouts, and no SIGPIPE raised by a write to a socket the server
 * closed. A read, a write, a connect or a wait a signal interrupts is
 * made again within what is left of its bound, as turborel-client makes
 * it again (turborel's RULES.md D1047). CMake builds it everywhere but Windows.
 */
#if !defined(_WIN32) && !defined(__APPLE__)
/* POSIX's sockets, which C17 alone does not declare; the feature macro is
   the C library's name, reserved and so spelled. */
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/transport.h"

/* Each platform declares its sockets in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include "format.h"
#include "memory.h"
#include "sync.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

/* A write's flags: no SIGPIPE where the flag says it; macOS says it of
   the socket instead, by SO_NOSIGPIPE. */
#ifdef MSG_NOSIGNAL
#define TRL_SEND_FLAGS MSG_NOSIGNAL
#else
#define TRL_SEND_FLAGS 0
#endif
#define TRL_PORT_TEXT 8U
#define TRL_MS_PER_SECOND 1000U
#define TRL_US_PER_MS 1000U

/* A socket's state: its descriptor, and the bound of each of its reads
   and writes, 0 for none. */
typedef struct socket_state {
    int fd;
    uint32_t bound_ms;
} socket_state;

/* The descriptor of `transport`. */
static int fd_of(const trl_transport *transport) {
    return ((const socket_state *)transport->state)->fd;
}

/* Whether `code` says a wait ran out: SO_RCVTIMEO and SO_SNDTIMEO give
   EAGAIN, or EWOULDBLOCK. */
static bool ran_out(int code) {
#if EAGAIN == EWOULDBLOCK
    return code == EAGAIN;
#else
    return code == EAGAIN || code == EWOULDBLOCK;
#endif
}

/* Records the failure of `code`, a wait run out if it says so. */
static void failed(trl_transport *transport, int code) {
    trl_io_error_os(&transport->error, code);
    transport->error.timed_out = ran_out(code);
}

/* Sets the timeout `option` of `fd`, SO_RCVTIMEO or SO_SNDTIMEO, to `ms`, 0 for none. */
static bool set_timeout(int fd, int option, uint32_t ms) {
    struct timeval limit;
    memset(&limit, 0, sizeof limit);
    limit.tv_sec = (time_t)(ms / TRL_MS_PER_SECOND);
    limit.tv_usec = (suseconds_t)((ms % TRL_MS_PER_SECOND) * TRL_US_PER_MS);
    return setsockopt(fd, SOL_SOCKET, option, &limit, sizeof limit) == 0;
}

/* Sets both timeouts of `fd` to `ms`, 0 for none. */
static bool set_timeouts(int fd, uint32_t ms) {
    return set_timeout(fd, SO_RCVTIMEO, ms) && set_timeout(fd, SO_SNDTIMEO, ms);
}

/* One read into `into`, or else one write of `from`. */
static ssize_t once(int fd, uint8_t *into, const uint8_t *from, size_t length) {
    return into != NULL ? recv(fd, into, length, 0) : send(fd, from, length, TRL_SEND_FLAGS);
}

/* Reads into `into`, or else writes `from`, made again each time a signal
   interrupts it, as turborel-client makes it again (turborel's RULES.md D1047): of
   a bounded socket, within what is left of its bound, which is set to
   that for each try and set back after; once none is left, a wait run
   out. */
static int64_t uninterrupted(trl_transport *transport, uint8_t *into, const uint8_t *from, size_t length) {
    const socket_state *state = transport->state;
    int option = into != NULL ? SO_RCVTIMEO : SO_SNDTIMEO;
    uint64_t began = state->bound_ms == 0U ? 0U : trl_now_ms();
    bool shortened = false;
    ssize_t done = once(state->fd, into, from, length);
    int code = errno;
    while (done < 0 && code == EINTR) {
        uint64_t spent = state->bound_ms == 0U ? 0U : trl_now_ms() - began;
        if (state->bound_ms != 0U && spent >= state->bound_ms) {
            code = EAGAIN;
            break;
        }
        if (state->bound_ms != 0U && !set_timeout(state->fd, option, (uint32_t)(state->bound_ms - spent))) {
            code = errno;
            break;
        }
        shortened = state->bound_ms != 0U;
        done = once(state->fd, into, from, length);
        code = errno;
    }
    if (shortened) {
        (void)set_timeout(state->fd, option, state->bound_ms);
    }
    if (done < 0) {
        failed(transport, code);
        return -1;
    }
    return (int64_t)done;
}

static int64_t socket_read(trl_transport *transport, uint8_t *into, size_t capacity) {
    return uninterrupted(transport, into, NULL, capacity);
}

static int64_t socket_write(trl_transport *transport, const uint8_t *bytes, size_t length) {
    return uninterrupted(transport, NULL, bytes, length);
}

/* Bounds each read and write; a socket whose peer is gone, which some
   systems refuse to bound with EINVAL, is left as it is, as
   turborel-client leaves it, its next read giving its end. */
static bool socket_bound(trl_transport *transport, uint32_t ms) {
    if (set_timeouts(fd_of(transport), ms)) {
        ((socket_state *)transport->state)->bound_ms = ms;
        return true;
    }
    int code = errno;
    struct sockaddr_storage peer;
    socklen_t length = sizeof peer;
    if (code == EINVAL && getpeername(fd_of(transport), (struct sockaddr *)&peer, &length) != 0) {
        return true;
    }
    failed(transport, code);
    return false;
}

static void socket_close(trl_transport *transport) {
    (void)close(fd_of(transport));
    free(transport->state);
}

/* The address the socket reached, as numbers: an IP address's, not a
   Unix socket's. */
static bool socket_peer(const trl_transport *transport, char *out, size_t capacity) {
    struct sockaddr_storage peer;
    socklen_t length = sizeof peer;
    if (getpeername(fd_of(transport), (struct sockaddr *)&peer, &length) != 0 ||
        (peer.ss_family != AF_INET && peer.ss_family != AF_INET6)) {
        return false;
    }
    return getnameinfo((const struct sockaddr *)&peer, length, out, (socklen_t)capacity, NULL, 0, NI_NUMERICHOST) == 0;
}

static const trl_transport_ops socket_ops = {socket_read, socket_write, socket_bound, socket_close, socket_peer};

/* Makes `out` a transport of `fd`, which it then owns, each read and
   write bounded by `bound_ms`. */
static bool adopt(int fd, uint32_t bound_ms, trl_transport *out) {
    socket_state *state = trl_malloc(sizeof *state);
    if (state == NULL) {
        (void)close(fd);
        trl_io_error_set(&out->error, "memory could not be had", false);
        return false;
    }
    state->fd = fd;
    state->bound_ms = bound_ms;
    out->ops = &socket_ops;
    out->state = state;
    return true;
}

/* Sets what each socket of ours needs: close on exec, and on macOS no
   SIGPIPE. */
static void prepare(int fd) {
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int on = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

/* Waits for a connect begun without blocking, at most `ms`, waited for
   again within what is left of it each time a signal interrupts it
   (turborel's RULES.md D1047); the error it came to, ETIMEDOUT if the wait ran out. */
static int finish_connect(int fd, uint32_t ms) {
    struct pollfd watched = {fd, POLLOUT, 0};
    uint64_t began = trl_now_ms();
    int ready = poll(&watched, 1U, ms == 0U ? -1 : (int)ms);
    while (ready < 0 && errno == EINTR) {
        uint64_t spent = trl_now_ms() - began;
        int left = spent >= ms ? 0 : (int)(ms - spent);
        ready = poll(&watched, 1U, ms == 0U ? -1 : left);
    }
    if (ready < 0) {
        return errno;
    }
    if (ready == 0) {
        return ETIMEDOUT;
    }
    int code = 0;
    socklen_t length = sizeof code;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &length) != 0) {
        return errno;
    }
    return code;
}

/* Connects `fd` to `address` within `ms`, as Rust's connect_timeout does:
   without blocking, then waiting for it. */
static int connect_within(int fd, const struct sockaddr *address, socklen_t length, uint32_t ms) {
    int flags = fcntl(fd, F_GETFL, 0);
    /* POSIX gives a descriptor's flags as an int. */
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { // NOLINT(bugprone-signed-bitwise)
        return errno;
    }
    int code = 0;
    /* A connect a signal interrupts goes on by itself, as POSIX gives it,
       and is waited for as one begun. */
    if (connect(fd, address, length) != 0) {
        code = errno == EINPROGRESS || errno == EINTR ? finish_connect(fd, ms) : errno;
    }
    if (code == 0 && fcntl(fd, F_SETFL, flags) < 0) {
        code = errno;
    }
    return code;
}

/* Tries one address; the error it came to, 0 if connected. */
static int try_address(const struct addrinfo *at, uint32_t within_ms, int *out) {
    int fd = socket(at->ai_family, at->ai_socktype, at->ai_protocol);
    if (fd < 0) {
        return errno;
    }
    prepare(fd);
    int code = connect_within(fd, at->ai_addr, at->ai_addrlen, within_ms);
    if (code != 0) {
        (void)close(fd);
        return code;
    }
    *out = fd;
    return 0;
}

/* Records a connect that failed by `code`: a wait run out said as Rust's
   connect_timeout says it. */
static void connect_failed(trl_transport *out, int code) {
    if (code == ETIMEDOUT) {
        trl_io_error_set(&out->error, "connection timed out", true);
        return;
    }
    trl_io_error_os(&out->error, code);
}

/* Sets up a socket connected: no delay, and its timeouts. */
static bool set_up(int fd, uint32_t within_ms, trl_transport *out) {
    int on = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on) != 0 || !set_timeouts(fd, within_ms)) {
        int code = errno;
        (void)close(fd);
        trl_io_error_os(&out->error, code);
        return false;
    }
    return adopt(fd, within_ms, out);
}

bool trl_tcp_open(const char *host, uint16_t port, uint32_t within_ms, trl_transport *out) {
    char service[TRL_PORT_TEXT];
    trl_format(service, sizeof service, "%u", (unsigned int)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *found = NULL;
    int resolved = getaddrinfo(host, service, &hints, &found);
    if (resolved != 0) {
        char text[TRL_IO_TEXT_BYTES];
        trl_format(text, sizeof text, "failed to lookup address information: %s", gai_strerror(resolved));
        trl_io_error_set(&out->error, text, false);
        return false;
    }
    int code = -1;
    int fd = -1;
    for (const struct addrinfo *at = found; at != NULL && code != 0; at = at->ai_next) {
        code = try_address(at, within_ms, &fd);
    }
    freeaddrinfo(found);
    if (code == -1) {
        trl_io_error_set(&out->error, "no address to connect to", false);
        return false;
    }
    if (code != 0) {
        connect_failed(out, code);
        return false;
    }
    return set_up(fd, within_ms, out);
}

/* Each address `found` lists, numeric and shown, into a list of the
   caller's. */
static bool listed(const struct addrinfo *found, trl_address **out, size_t *count) {
    size_t n = 0U;
    for (const struct addrinfo *at = found; at != NULL; at = at->ai_next) {
        ++n;
    }
    *out = trl_calloc(n == 0U ? 1U : n, sizeof **out);
    if (*out == NULL) {
        return false;
    }
    for (const struct addrinfo *at = found; at != NULL; at = at->ai_next) {
        trl_address *address = &(*out)[*count];
        char port[TRL_PORT_TEXT];
        if (getnameinfo(at->ai_addr, at->ai_addrlen, address->host, sizeof address->host, port, sizeof port,
                        NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
            continue;
        }
        bool six = at->ai_family == AF_INET6;
        trl_format(address->shown, sizeof address->shown, six ? "[%s]:%s" : "%s:%s", address->host, port);
        ++*count;
    }
    return true;
}

bool trl_tcp_resolve(const char *host, uint16_t port, trl_address **out, size_t *count, trl_io_error *error) {
    *out = NULL;
    *count = 0U;
    char service[TRL_PORT_TEXT];
    trl_format(service, sizeof service, "%u", (unsigned int)port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo *found = NULL;
    int resolved = getaddrinfo(host, service, &hints, &found);
    if (resolved != 0) {
        char text[TRL_IO_TEXT_BYTES];
        trl_format(text, sizeof text, "failed to lookup address information: %s", gai_strerror(resolved));
        trl_io_error_set(error, text, false);
        return false;
    }
    bool made = listed(found, out, count);
    freeaddrinfo(found);
    if (!made || *count == 0U) {
        free(*out);
        *out = NULL;
        trl_io_error_set(error, made ? "no address to connect to" : "memory could not be had for the addresses", false);
        return false;
    }
    return true;
}

bool trl_local_open(const char *path, uint32_t within_ms, trl_transport *out) {
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    size_t length = strlen(path);
    if (length >= sizeof address.sun_path) {
        trl_io_error_set(&out->error, "path must be shorter than SUN_LEN", false);
        return false;
    }
    memcpy(address.sun_path, path, length + 1U);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        trl_io_error_os(&out->error, errno);
        return false;
    }
    prepare(fd);
    int connected = 0;
    do {
        connected = connect(fd, (const struct sockaddr *)&address, sizeof address);
    } while (connected != 0 && errno == EINTR);
    if (connected != 0 || !set_timeouts(fd, within_ms)) {
        int code = errno;
        (void)close(fd);
        trl_io_error_os(&out->error, code);
        return false;
    }
    return adopt(fd, within_ms, out);
}
// NOLINTEND(misc-include-cleaner)
