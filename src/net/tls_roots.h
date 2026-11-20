/**
 * @file tls_roots.h
 * @brief The platform's certificate authorities, as turborel-client takes
 * them [CLIENT Connecting].
 */
#ifndef TRL_NET_TLS_ROOTS_H
#define TRL_NET_TLS_ROOTS_H

#ifdef TRL_HAVE_TLS

// NOLINTBEGIN(misc-include-cleaner)
#include <openssl/x509.h>
// NOLINTEND(misc-include-cleaner)

#include <stddef.h>

/** @brief The bytes of a path a directory's file is read by. */
#define TRL_PATH_BYTES 4096U

/**
 * @brief Adds the platform's certificate authorities to `store`: those of
 * the files SSL_CERT_FILE and SSL_CERT_DIR name, if either is set, else
 * the platform's own; how many; of none, `why` says why, if it can.
 */
size_t trl_tls_roots_add(X509_STORE *store, char *why, size_t capacity);

#endif

#endif
