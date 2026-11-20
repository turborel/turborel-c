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
#include "net/tls_roots.h"
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

struct trl_tls {
    SSL_CTX *context;
    trl_failure failure;
    /* Whether any authority is trusted, as a target needs. */
    bool trusts;
    /* Whether a target holds the context, which then changes no more. */
    bool shared;
    /* How the server's certificate is checked. */
    trl_tls_checks checks;
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

trl_status trl_tls_trust_platform(trl_tls *tls) {
    if (tls == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_status open = changeable(tls);
    if (open != TRL_OK) {
        return open;
    }
    ERR_clear_error();
    char why[TRL_FAILURE_MESSAGE_BYTES];
    size_t added = trl_tls_roots_add(SSL_CTX_get_cert_store(tls->context), why, sizeof why);
    ERR_clear_error();
    if (added == 0U) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "cannot trust the platform's certificate authorities: %s",
                   why[0] != '\0' ? why : "it holds none");
        trl_failure_broken(&tls->failure, message);
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

/* Takes a certificate whose only fault is its name, as `verify-ca` does. */
static int of_any_name(int verified, X509_STORE_CTX *store) {
    int error = X509_STORE_CTX_get_error(store);
    return verified == 1 || error == X509_V_ERR_HOSTNAME_MISMATCH || error == X509_V_ERR_IP_ADDRESS_MISMATCH ? 1 : 0;
}

void trl_tls_set_checks(trl_tls *tls, trl_tls_checks checks) {
    if (changeable(tls) != TRL_OK) {
        return;
    }
    tls->checks = checks;
    if (checks == TRL_CHECKS_NONE) {
        SSL_CTX_set_verify(tls->context, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(tls->context, SSL_VERIFY_PEER, checks == TRL_CHECKS_AUTHORITY ? of_any_name : NULL);
    }
}

void *trl_tls_hold(trl_tls *tls, trl_failure *failure) {
    if (tls->context == NULL) {
        trl_failure_copy(failure, &tls->failure);
        return NULL;
    }
    if (!tls->trusts && tls->checks != TRL_CHECKS_NONE) {
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

void trl_tls_set_checks(trl_tls *tls, trl_tls_checks checks) {
    (void)tls;
    (void)checks;
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
