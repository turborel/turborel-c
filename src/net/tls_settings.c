/**
 * @file tls_settings.c
 * @brief TLS settings [CLIENT Connecting]: the authorities trusted, of a
 * PEM file, the platform's or both, and the certificate presented; of TLS
 * 1.2 and 1.3, the server's certificate and name checked, as
 * turborel-client's TLS checks them.
 */
#include "codec/error.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "net/tls.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef TRL_HAVE_TLS

/* The TLS library's and the platforms' headers declare more than the
   include checker can map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <wincrypt.h>
#include <windows.h>
#endif

struct trl_tls {
    SSL_CTX *context;
    trl_failure failure;
    /* Whether any authority is trusted, as a target needs. */
    bool trusts;
    /* Whether a target holds the context, which then changes no more. */
    bool shared;
};

/* Says what the TLS library's last error was, its queue then emptied. */
static void library_said(char *out, size_t capacity) {
    unsigned long code = ERR_get_error();
    const char *reason = code == 0U ? NULL : ERR_reason_error_string(code);
    trl_format(out, capacity, "%s", reason == NULL ? "the TLS library gave no reason" : reason);
    ERR_clear_error();
}

/* Fails as turborel-client does: "what path: why", SQLSTATE 08006. */
static trl_status failed(trl_tls *tls, const char *what, const char *path) {
    char why[TRL_FAILURE_MESSAGE_BYTES];
    library_said(why, sizeof why);
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "%s %s: %s", what, path, why);
    trl_failure_broken(&tls->failure, message);
    return TRL_FAILED;
}

trl_status trl_tls_new(trl_tls **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    trl_tls *tls = trl_calloc(1U, sizeof *tls);
    if (tls == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&tls->failure);
    *out = tls;
    ERR_clear_error();
    tls->context = SSL_CTX_new(TLS_client_method());
    if (tls->context == NULL || SSL_CTX_set_min_proto_version(tls->context, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(tls->context, TLS1_3_VERSION) != 1) {
        return failed(tls, "cannot make TLS", "settings");
    }
    SSL_CTX_set_verify(tls->context, SSL_VERIFY_PEER, NULL);
    return TRL_OK;
}

/* Whether the settings may change: made, and held by no target. */
static trl_status changeable(trl_tls *tls) {
    if (tls->context == NULL) {
        return TRL_FAILED;
    }
    if (tls->shared) {
        trl_failure_set(&tls->failure, TRL_SQLCODE_API, TRL_SQLSTATE_API, "TLS settings a target holds change no more");
        return TRL_FAILED;
    }
    return TRL_OK;
}

/* Adds each certificate of a PEM file to the store; how many. */
static size_t add_each(X509_STORE *store, BIO *file) {
    size_t added = 0U;
    X509 *certificate = NULL;
    while ((certificate = PEM_read_bio_X509(file, NULL, NULL, NULL)) != NULL) {
        if (X509_STORE_add_cert(store, certificate) == 1) {
            ++added;
        }
        X509_free(certificate);
    }
    return added;
}

trl_status trl_tls_trust_file(trl_tls *tls, const char *path) {
    if (tls == NULL || path == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status open = changeable(tls);
    if (open != TRL_OK) {
        return open;
    }
    ERR_clear_error();
    BIO *file = BIO_new_file(path, "r");
    if (file == NULL) {
        return failed(tls, "cannot read", path);
    }
    size_t added = add_each(SSL_CTX_get_cert_store(tls->context), file);
    BIO_free(file);
    ERR_clear_error();
    if (added == 0U) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "%s holds no certificate", path);
        trl_failure_broken(&tls->failure, message);
        return TRL_FAILED;
    }
    tls->trusts = true;
    return TRL_OK;
}

#if defined(__APPLE__) || defined(_WIN32)
/* Adds one certificate, DER, to the store; whether it was. */
static bool add_der(X509_STORE *store, const uint8_t *der, size_t length) {
    const unsigned char *at = der;
    X509 *certificate = d2i_X509(NULL, &at, (long)length);
    bool added = certificate != NULL && X509_STORE_add_cert(store, certificate) == 1;
    X509_free(certificate);
    return added;
}

#endif

#ifdef __APPLE__
/* The system's anchors, of the keychain's trust settings. */
static size_t add_platform(X509_STORE *store) {
    CFArrayRef anchors = NULL;
    if (SecTrustCopyAnchorCertificates(&anchors) != errSecSuccess || anchors == NULL) {
        return 0U;
    }
    size_t added = 0U;
    for (CFIndex i = 0; i < CFArrayGetCount(anchors); ++i) {
        /* An array of Core Foundation hands out its items as const void *. */
        union {
            const void *item;
            SecCertificateRef certificate;
        } at = {CFArrayGetValueAtIndex(anchors, i)};
        SecCertificateRef certificate = at.certificate;
        CFDataRef der = SecCertificateCopyData(certificate);
        if (der != NULL) {
            added += add_der(store, CFDataGetBytePtr(der), (size_t)CFDataGetLength(der)) ? 1U : 0U;
            CFRelease(der);
        }
    }
    CFRelease(anchors);
    return added;
}
#elif defined(_WIN32)
/* The system's store of root authorities. */
static size_t add_platform(X509_STORE *store) {
    HCERTSTORE roots = CertOpenSystemStoreW(0, L"ROOT");
    if (roots == NULL) {
        return 0U;
    }
    size_t added = 0U;
    PCCERT_CONTEXT at = NULL;
    while ((at = CertEnumCertificatesInStore(roots, at)) != NULL) {
        added += add_der(store, at->pbCertEncoded, at->cbCertEncoded) ? 1U : 0U;
    }
    (void)CertCloseStore(roots, 0U);
    return added;
}
#else
/* The TLS library's own paths are the platform's store. */
static size_t add_platform(X509_STORE *store) {
    (void)store;
    return 0U;
}
#endif

/* Whether SSL_CERT_FILE or SSL_CERT_DIR is set and not empty. */
static bool named_by_environment(void) {
    /* Read once, on the thread that asks for the platform's authorities;
       the library sets no variable. */
    const char *file = getenv("SSL_CERT_FILE"); // NOLINT(concurrency-mt-unsafe)
    const char *dir = getenv("SSL_CERT_DIR");   // NOLINT(concurrency-mt-unsafe)
    return (file != NULL && file[0] != '\0') || (dir != NULL && dir[0] != '\0');
}

trl_status trl_tls_trust_platform(trl_tls *tls) {
    if (tls == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status open = changeable(tls);
    if (open != TRL_OK) {
        return open;
    }
    ERR_clear_error();
    bool environment = named_by_environment();
    size_t added = environment ? 0U : add_platform(SSL_CTX_get_cert_store(tls->context));
    bool paths = (environment || added == 0U) && SSL_CTX_set_default_verify_paths(tls->context) == 1;
    ERR_clear_error();
    if (added == 0U && !paths) {
        trl_failure_broken(&tls->failure, "cannot trust the platform's certificate authorities: it holds none");
        return TRL_FAILED;
    }
    tls->trusts = true;
    return TRL_OK;
}

trl_status trl_tls_present(trl_tls *tls, const char *chain, const char *key) {
    if (tls == NULL || chain == NULL || key == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status open = changeable(tls);
    if (open != TRL_OK) {
        return open;
    }
    ERR_clear_error();
    if (SSL_CTX_use_certificate_chain_file(tls->context, chain) != 1) {
        return failed(tls, "cannot read", chain);
    }
    if (SSL_CTX_use_PrivateKey_file(tls->context, key, SSL_FILETYPE_PEM) != 1) {
        return failed(tls, "cannot read", key);
    }
    if (SSL_CTX_check_private_key(tls->context) != 1) {
        return failed(tls, "cannot present", chain);
    }
    return TRL_OK;
}

bool trl_tls_at_most_tls12(trl_tls *tls) {
    return changeable(tls) == TRL_OK && SSL_CTX_set_max_proto_version(tls->context, TLS1_2_VERSION) == 1;
}

void *trl_tls_hold(trl_tls *tls, trl_failure *failure) {
    if (tls->context == NULL) {
        trl_failure_copy(failure, &tls->failure);
        return NULL;
    }
    if (!tls->trusts) {
        trl_failure_broken(failure, "no certificate authority is trusted: name a file of them, or the platform's");
        return NULL;
    }
    if (SSL_CTX_up_ref(tls->context) != 1) {
        trl_failure_no_memory(failure);
        return NULL;
    }
    tls->shared = true;
    return tls->context;
}

void *trl_tls_context_hold(void *context) {
    return SSL_CTX_up_ref((SSL_CTX *)context) == 1 ? context : NULL;
}

void trl_tls_release(void *context) {
    SSL_CTX_free((SSL_CTX *)context);
}

void trl_tls_free(trl_tls *tls) {
    if (tls == NULL) {
        return;
    }
    SSL_CTX_free(tls->context);
    trl_failure_clear(&tls->failure);
    free(tls);
}
// NOLINTEND(misc-include-cleaner)

#else

struct trl_tls {
    trl_failure failure;
};

trl_status trl_tls_new(trl_tls **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = trl_calloc(1U, sizeof **out);
    if (*out == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_failure_init(&(*out)->failure);
    trl_failure_set(&(*out)->failure, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                    "TLS needs a TLS library, and this library was built without one");
    return TRL_FAILED;
}

trl_status trl_tls_trust_file(trl_tls *tls, const char *path) {
    return tls == NULL || path == NULL ? TRL_INVALID_ARGUMENT : TRL_FAILED;
}

trl_status trl_tls_trust_platform(trl_tls *tls) {
    return tls == NULL ? TRL_INVALID_ARGUMENT : TRL_FAILED;
}

trl_status trl_tls_present(trl_tls *tls, const char *chain, const char *key) {
    return tls == NULL || chain == NULL || key == NULL ? TRL_INVALID_ARGUMENT : TRL_FAILED;
}

bool trl_tls_at_most_tls12(trl_tls *tls) {
    (void)tls;
    return false;
}

void *trl_tls_hold(trl_tls *tls, trl_failure *failure) {
    trl_failure_copy(failure, &tls->failure);
    return NULL;
}

void *trl_tls_context_hold(void *context) {
    return context;
}

void trl_tls_release(void *context) {
    (void)context;
}

void trl_tls_free(trl_tls *tls) {
    if (tls != NULL) {
        trl_failure_clear(&tls->failure);
        free(tls);
    }
}

#endif

const trl_failure *trl_tls_failure(const trl_tls *tls) {
    return tls == NULL ? NULL : &tls->failure;
}
