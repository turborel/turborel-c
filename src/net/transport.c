/**
 * @file transport.c
 * @brief What every transport shares: writing all of a message, closing,
 * and saying what the system said as turborel-client says it, as
 * "Connection refused (os error 61)".
 */
#if !defined(_WIN32) && !defined(__APPLE__)
/* POSIX's strerror_r, of XSI's form, which C17 alone does not declare;
   the feature macro is the C library's name, reserved and so spelled. */
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "net/transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

bool trl_transport_write_all(trl_transport *transport, const uint8_t *bytes, size_t length) {
    size_t sent = 0U;
    while (sent < length) {
        int64_t wrote = transport->ops->write(transport, bytes + sent, length - sent);
        if (wrote <= 0) {
            if (wrote == 0) {
                trl_io_error_set(&transport->error, "failed to write whole buffer", false);
            }
            return false;
        }
        sent += (size_t)wrote;
    }
    return true;
}

void trl_transport_close(trl_transport *transport) {
    if (transport->ops != NULL) {
        transport->ops->close(transport);
        transport->ops = NULL;
        transport->state = NULL;
    }
}

void trl_io_error_set(trl_io_error *error, const char *text, bool timed_out) {
    error->timed_out = timed_out;
    (void)snprintf(error->text, sizeof error->text, "%s", text);
}

#ifdef _WIN32

/* The system's message, its line end trimmed, as the standard library of
   Rust trims it. */
void trl_io_error_os(trl_io_error *error, int code) {
    char said[TRL_IO_TEXT_BYTES] = "";
    DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code, 0,
                                  said, (DWORD)sizeof said, NULL);
    while (length > 0U && (said[length - 1U] == '\n' || said[length - 1U] == '\r')) {
        said[--length] = '\0';
    }
    error->timed_out = false;
    (void)snprintf(error->text, sizeof error->text, "%s (os error %d)", length > 0U ? said : "Unknown error", code);
}

#else

void trl_io_error_os(trl_io_error *error, int code) {
    char said[TRL_IO_TEXT_BYTES] = "";
    /* POSIX's, of XSI's form, declared by <string.h> under the feature macro. */
    if (strerror_r(code, said, sizeof said) != 0) { // NOLINT(misc-include-cleaner)
        (void)snprintf(said, sizeof said, "Unknown error");
    }
    error->timed_out = false;
    (void)snprintf(error->text, sizeof error->text, "%s (os error %d)", said, code);
}

#endif
