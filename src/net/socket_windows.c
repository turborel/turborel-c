/**
 * @file socket_windows.c
 * @brief TCP sockets and named pipes on Windows (RULES.md R-PLAT-2):
 * each address tried in turn within the connect timeout, each read and
 * write of a socket bounded by its own timeouts, and a named pipe waited
 * for while busy, within the connect timeout, its reads and writes not
 * bounded, as turborel-client's are not. CMake builds it on Windows
 * alone.
 */
#include "net/transport.h"

/* Windows declares its sockets and pipes in headers of its own, which the
   include checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#define WIN32_LEAN_AND_MEAN
#include "format.h"
#include "memory.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define TRL_PORT_TEXT 8U
#define TRL_MS_PER_SECOND 1000U
#define TRL_US_PER_MS 1000U
/* Winsock's version asked for, 2.2. */
#define TRL_WINSOCK_VERSION 2U
/* How long to wait between tries of a pipe every instance of which is busy. */
#define TRL_BUSY_RETRY_MS 10U

/* A socket's state. */
typedef struct socket_state {
    SOCKET socket;
} socket_state;

/* A pipe's state. */
typedef struct pipe_state {
    HANDLE handle;
} pipe_state;

/* The most bytes one call reads or writes, as Winsock counts in an int
   and pipes in a DWORD. */
static int chunk(size_t length) {
    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

static SOCKET socket_of(const trl_transport *transport) {
    return ((const socket_state *)transport->state)->socket;
}

/* Records Winsock's last failure, a wait run out if it says so. */
static void failed(trl_transport *transport) {
    int code = WSAGetLastError();
    trl_io_error_os(&transport->error, code);
    transport->error.timed_out = code == WSAETIMEDOUT;
}

static int64_t socket_read(trl_transport *transport, uint8_t *into, size_t capacity) {
    int got = recv(socket_of(transport), (char *)into, chunk(capacity), 0);
    if (got == SOCKET_ERROR) {
        failed(transport);
        return -1;
    }
    return (int64_t)got;
}

static int64_t socket_write(trl_transport *transport, const uint8_t *bytes, size_t length) {
    int sent = send(socket_of(transport), (const char *)bytes, chunk(length), 0);
    if (sent == SOCKET_ERROR) {
        failed(transport);
        return -1;
    }
    return (int64_t)sent;
}

/* Sets both timeouts of `socket` to `ms`, 0 for none. */
static bool set_timeouts(SOCKET socket, uint32_t ms) {
    DWORD limit = (DWORD)ms;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&limit, (int)sizeof limit) == 0 &&
           setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&limit, (int)sizeof limit) == 0;
}

static bool socket_bound(trl_transport *transport, uint32_t ms) {
    if (set_timeouts(socket_of(transport), ms)) {
        return true;
    }
    failed(transport);
    return false;
}

static void socket_close(trl_transport *transport) {
    (void)closesocket(socket_of(transport));
    (void)WSACleanup();
    free(transport->state);
}

static const trl_transport_ops socket_ops = {socket_read, socket_write, socket_bound, socket_close};

/* Waits for a connect begun without blocking, at most `ms`; the error it
   came to, WSAETIMEDOUT if the wait ran out. */
static int finish_connect(SOCKET socket, uint32_t ms) {
    fd_set writable;
    fd_set failing;
    FD_ZERO(&writable);
    FD_ZERO(&failing);
    FD_SET(socket, &writable);
    FD_SET(socket, &failing);
    struct timeval limit = {(long)(ms / TRL_MS_PER_SECOND), (long)((ms % TRL_MS_PER_SECOND) * TRL_US_PER_MS)};
    int ready = select(0, NULL, &writable, &failing, ms == 0U ? NULL : &limit);
    if (ready == SOCKET_ERROR) {
        return WSAGetLastError();
    }
    if (ready == 0) {
        return WSAETIMEDOUT;
    }
    int code = 0;
    int length = (int)sizeof code;
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&code, &length) != 0) {
        return WSAGetLastError();
    }
    return code;
}

/* Connects `socket` to `address` within `ms`, without blocking, then
   waiting for it. */
static int connect_within(SOCKET socket, const struct sockaddr *address, int length, uint32_t ms) {
    u_long on = 1U;
    if (ioctlsocket(socket, FIONBIO, &on) != 0) {
        return WSAGetLastError();
    }
    int code = 0;
    if (connect(socket, address, length) != 0) {
        code = WSAGetLastError();
        code = code == WSAEWOULDBLOCK ? finish_connect(socket, ms) : code;
    }
    u_long off = 0U;
    if (code == 0 && ioctlsocket(socket, FIONBIO, &off) != 0) {
        code = WSAGetLastError();
    }
    return code;
}

/* Tries one address; the error it came to, 0 if connected. */
static int try_address(const ADDRINFOA *at, uint32_t within_ms, SOCKET *out) {
    SOCKET made = socket(at->ai_family, at->ai_socktype, at->ai_protocol);
    if (made == INVALID_SOCKET) {
        return WSAGetLastError();
    }
    int code = connect_within(made, at->ai_addr, (int)at->ai_addrlen, within_ms);
    if (code != 0) {
        (void)closesocket(made);
        return code;
    }
    *out = made;
    return 0;
}

/* Sets up a socket connected, which `out` then owns. */
static bool set_up(SOCKET socket, uint32_t within_ms, trl_transport *out) {
    BOOL on = TRUE;
    socket_state *state = trl_malloc(sizeof *state);
    if (state == NULL || setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, (int)sizeof on) != 0 ||
        !set_timeouts(socket, within_ms)) {
        if (state == NULL) {
            trl_io_error_set(&out->error, "memory could not be had", false);
        } else {
            trl_io_error_os(&out->error, WSAGetLastError());
        }
        free(state);
        (void)closesocket(socket);
        (void)WSACleanup();
        return false;
    }
    state->socket = socket;
    out->ops = &socket_ops;
    out->state = state;
    return true;
}

/* Resolves `host` and `port`; on failure, says why as the system says it. */
static bool resolve(const char *host, uint16_t port, ADDRINFOA **found, trl_transport *out) {
    char service[TRL_PORT_TEXT];
    trl_format(service, sizeof service, "%u", (unsigned int)port);
    ADDRINFOA hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    int resolved = getaddrinfo(host, service, &hints, found);
    if (resolved == 0) {
        return true;
    }
    trl_io_error error;
    trl_io_error_os(&error, resolved);
    char text[TRL_IO_TEXT_BYTES];
    trl_format(text, sizeof text, "failed to lookup address information: %s", error.text);
    trl_io_error_set(&out->error, text, false);
    return false;
}

bool trl_tcp_open(const char *host, uint16_t port, uint32_t within_ms, trl_transport *out) {
    WSADATA data;
    int started = WSAStartup(MAKEWORD(TRL_WINSOCK_VERSION, TRL_WINSOCK_VERSION), &data);
    if (started != 0) {
        trl_io_error_os(&out->error, started);
        return false;
    }
    ADDRINFOA *found = NULL;
    if (!resolve(host, port, &found, out)) {
        (void)WSACleanup();
        return false;
    }
    int code = -1;
    SOCKET socket = INVALID_SOCKET;
    for (const ADDRINFOA *at = found; at != NULL && code != 0; at = at->ai_next) {
        code = try_address(at, within_ms, &socket);
    }
    freeaddrinfo(found);
    if (code != 0) {
        if (code == -1) {
            trl_io_error_set(&out->error, "no address to connect to", false);
        } else if (code == WSAETIMEDOUT) {
            trl_io_error_set(&out->error, "connection timed out", true);
        } else {
            trl_io_error_os(&out->error, code);
        }
        (void)WSACleanup();
        return false;
    }
    return set_up(socket, within_ms, out);
}

static HANDLE handle_of(const trl_transport *transport) {
    return ((const pipe_state *)transport->state)->handle;
}

/* A pipe whose server closed reads as at its end, as Rust reads it. */
static int64_t pipe_read(trl_transport *transport, uint8_t *into, size_t capacity) {
    DWORD got = 0U;
    if (!ReadFile(handle_of(transport), into, (DWORD)chunk(capacity), &got, NULL)) {
        DWORD code = GetLastError();
        if (code == ERROR_BROKEN_PIPE) {
            return 0;
        }
        trl_io_error_os(&transport->error, (int)code);
        return -1;
    }
    return (int64_t)got;
}

static int64_t pipe_write(trl_transport *transport, const uint8_t *bytes, size_t length) {
    DWORD sent = 0U;
    if (!WriteFile(handle_of(transport), bytes, (DWORD)chunk(length), &sent, NULL)) {
        trl_io_error_os(&transport->error, (int)GetLastError());
        return -1;
    }
    return (int64_t)sent;
}

/* A pipe is bounded only as it is opened; its reads and writes wait as
   long as they must. */
static bool pipe_bound(trl_transport *transport, uint32_t ms) {
    (void)transport;
    (void)ms;
    return true;
}

static void pipe_close(trl_transport *transport) {
    (void)CloseHandle(handle_of(transport));
    free(transport->state);
}

static const trl_transport_ops pipe_ops = {pipe_read, pipe_write, pipe_bound, pipe_close};

/* Opens the pipe, trying again while every instance is busy, within
   `within_ms`; INVALID_HANDLE_VALUE if it cannot. */
static HANDLE open_pipe(const wchar_t *name, uint32_t within_ms, trl_transport *out) {
    ULONGLONG began = GetTickCount64();
    for (;;) {
        HANDLE handle = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0U, NULL, OPEN_EXISTING,
                                    SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            return handle;
        }
        DWORD code = GetLastError();
        trl_io_error_os(&out->error, (int)code);
        if (code != ERROR_PIPE_BUSY) {
            return INVALID_HANDLE_VALUE;
        }
        if (within_ms != 0U && GetTickCount64() - began >= within_ms) {
            char text[TRL_IO_TEXT_BYTES];
            trl_format(text, sizeof text, "every instance of the pipe is busy (%s)", out->error.text);
            trl_io_error_set(&out->error, text, true);
            return INVALID_HANDLE_VALUE;
        }
        Sleep(TRL_BUSY_RETRY_MS);
    }
}

bool trl_local_open(const char *path, uint32_t within_ms, trl_transport *out) {
    int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *name = wide > 0 ? trl_malloc((size_t)wide * sizeof(wchar_t)) : NULL;
    if (name == NULL || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, name, wide) != wide) {
        free(name);
        trl_io_error_set(&out->error, "the pipe's name is not UTF-8, or memory could not be had", false);
        return false;
    }
    HANDLE handle = open_pipe(name, within_ms, out);
    free(name);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    pipe_state *state = trl_malloc(sizeof *state);
    if (state == NULL) {
        (void)CloseHandle(handle);
        trl_io_error_set(&out->error, "memory could not be had", false);
        return false;
    }
    state->handle = handle;
    out->ops = &pipe_ops;
    out->state = state;
    return true;
}
// NOLINTEND(misc-include-cleaner)
