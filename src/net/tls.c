/**
 * @file tls.c
 * @brief TLS over a transport: the handshake, the server's certificate
 * checked against its name, reads and writes through the TLS library to
 * the transport beneath, and the channel SCRAM binds [PROTOCOL Mechanisms].
 */
#include "net/tls.h"

#include "auth/crypto.h"
#include "auth/scram.h"
#include "failure.h"
#include "memory.h"
#include "net/transport.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef TRL_HAVE_TLS

#include <limits.h>
#include <stdio.h>

/* The TLS library's headers declare more than the include checker can
   map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

/* The longest DNS name, and label (RFC 1035). */
#define TRL_DNS_NAME_BYTES 253U
#define TRL_DNS_LABEL_BYTES 63U
#define TRL_NAME_BYTES 256U

/* TLS over a transport: the TLS library's connection, the method of the
   BIO that reaches the transport, and the transport. */
typedef struct tls_state {
    SSL *ssl;
    BIO_METHOD *method;
    trl_transport inner;
    /* Whether the transport beneath failed, so its failure is the one told. */
    bool inner_failed;
    /* Whether the connection failed, and so is closed without TLS's
       closing message. */
    bool broken;
} tls_state;

/* The BIO's write: through to the transport. */
static int bio_write(BIO *bio, const char *data, int length) {
    tls_state *state = BIO_get_data(bio);
    BIO_clear_retry_flags(bio);
    int64_t wrote = state->inner.ops->write(&state->inner, (const uint8_t *)data, (size_t)length);
    if (wrote < 0) {
        state->inner_failed = true;
        return -1;
    }
    return (int)wrote;
}

/* The BIO's read: through to the transport; 0 at its end. */
static int bio_read(BIO *bio, char *data, int length) {
    tls_state *state = BIO_get_data(bio);
    BIO_clear_retry_flags(bio);
    int64_t got = state->inner.ops->read(&state->inner, (uint8_t *)data, (size_t)length);
    if (got < 0) {
        state->inner_failed = true;
        return -1;
    }
    return (int)got;
}

/* The BIO's controls: a flush, which writes nothing more, and no other. */
static long bio_ctrl(BIO *bio, int command, long number, void *pointer) {
    (void)bio;
    (void)number;
    (void)pointer;
    return command == BIO_CTRL_FLUSH ? 1L : 0L;
}

/* Says why the TLS library failed: the transport's failure if it failed
   beneath, else the library's reason, with the certificate's if its
   check failed. */
static void describe(tls_state *state, trl_io_error *error) {
    if (state->inner_failed) {
        *error = state->inner.error;
        return;
    }
    unsigned long code = ERR_get_error();
    const char *reason = code == 0U ? NULL : ERR_reason_error_string(code);
    long verified = SSL_get_verify_result(state->ssl);
    char text[TRL_IO_TEXT_BYTES];
    if (verified != X509_V_OK) {
        (void)snprintf(text, sizeof text, "%s (%s)", reason == NULL ? "certificate verify failed" : reason,
                       X509_verify_cert_error_string(verified));
    } else {
        (void)snprintf(text, sizeof text, "%s",
                       reason == NULL ? "the server closed the connection within TLS" : reason);
    }
    ERR_clear_error();
    trl_io_error_set(error, text, false);
}

/* The most bytes one call passes, as the TLS library counts in an int. */
static int chunk(size_t length) {
    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

static int64_t tls_read(trl_transport *transport, uint8_t *into, size_t capacity) {
    tls_state *state = transport->state;
    ERR_clear_error();
    int got = SSL_read(state->ssl, into, chunk(capacity));
    if (got > 0) {
        return (int64_t)got;
    }
    if (SSL_get_error(state->ssl, got) == SSL_ERROR_ZERO_RETURN) {
        return 0;
    }
    state->broken = true;
    describe(state, &transport->error);
    return -1;
}

static int64_t tls_write(trl_transport *transport, const uint8_t *bytes, size_t length) {
    tls_state *state = transport->state;
    ERR_clear_error();
    int wrote = SSL_write(state->ssl, bytes, chunk(length));
    if (wrote > 0) {
        return (int64_t)wrote;
    }
    state->broken = true;
    describe(state, &transport->error);
    return -1;
}

static bool tls_bound(trl_transport *transport, uint32_t ms) {
    tls_state *state = transport->state;
    if (state->inner.ops->bound(&state->inner, ms)) {
        return true;
    }
    transport->error = state->inner.error;
    return false;
}

/* Frees the TLS library's connection and the transport beneath. */
static void release(tls_state *state) {
    SSL_free(state->ssl);
    BIO_meth_free(state->method);
    trl_transport_close(&state->inner);
    free(state);
}

/* Ends TLS with its closing message, as the server expects of a client
   that ends its stream, unless the connection failed. */
static void tls_close(trl_transport *transport) {
    tls_state *state = transport->state;
    if (!state->broken && SSL_is_init_finished(state->ssl) == 1) {
        (void)SSL_shutdown(state->ssl);
    }
    ERR_clear_error();
    release(state);
}

static const trl_transport_ops tls_ops = {tls_read, tls_write, tls_bound, tls_close};

/* Whether `c` may be in a DNS name's label. */
static bool label_character(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

/* Whether the `length` bytes at `label` are a label: 1 to 63 of letters,
   digits, hyphens and underscores, not beginning or ending with a hyphen. */
static bool is_label(const char *label, size_t length) {
    if (length == 0U || length > TRL_DNS_LABEL_BYTES || label[0] == '-' || label[length - 1U] == '-') {
        return false;
    }
    for (size_t i = 0U; i < length; ++i) {
        if (!label_character(label[i])) {
            return false;
        }
    }
    return true;
}

/* Whether `name` is a DNS name as a certificate names one: labels joined
   by periods, at most 253 bytes, a last period allowed. */
static bool is_dns_name(const char *name) {
    size_t length = strlen(name);
    if (length > 0U && name[length - 1U] == '.') {
        --length;
    }
    if (length == 0U || length > TRL_DNS_NAME_BYTES) {
        return false;
    }
    size_t begun = 0U;
    for (size_t i = 0U; i <= length; ++i) {
        if (i == length || name[i] == '.') {
            if (!is_label(name + begun, i - begun)) {
                return false;
            }
            begun = i + 1U;
        }
    }
    return true;
}

/* Says which name the server's certificate is checked against: an
   address, or a DNS name, also sent as SNI. */
static bool name_server(SSL *ssl, const char *name, trl_failure *failure) {
    X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
    if (X509_VERIFY_PARAM_set1_ip_asc(param, name) == 1) {
        return true;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    char sent[TRL_NAME_BYTES];
    if (!is_dns_name(name) || strlen(name) >= sizeof sent) {
        (void)snprintf(message, sizeof message, "%s is no server's name: invalid dns name", name);
        trl_failure_broken(failure, message);
        return false;
    }
    (void)snprintf(sent, sizeof sent, "%s", name);
    X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (X509_VERIFY_PARAM_set1_host(param, sent, 0U) != 1 || SSL_set_tlsext_host_name(ssl, sent) != 1) {
        trl_failure_no_memory(failure);
        return false;
    }
    return true;
}

/* Makes the TLS library's connection over the transport, its BIO's
   functions ours. */
static tls_state *begin(SSL_CTX *context, trl_transport *inner) {
    tls_state *state = trl_calloc(1U, sizeof *state);
    if (state == NULL) {
        return NULL;
    }
    state->inner = *inner;
    inner->ops = NULL;
    state->ssl = SSL_new(context);
    state->method = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "turborel");
    BIO *bio = NULL;
    if (state->ssl != NULL && state->method != NULL && BIO_meth_set_write(state->method, bio_write) == 1 &&
        BIO_meth_set_read(state->method, bio_read) == 1 && BIO_meth_set_ctrl(state->method, bio_ctrl) == 1) {
        bio = BIO_new(state->method);
    }
    if (bio == NULL) {
        release(state);
        return NULL;
    }
    BIO_set_data(bio, state);
    BIO_set_init(bio, 1);
    SSL_set_bio(state->ssl, bio, bio);
    return state;
}

/* The certificate the server presented, DER, and over TLS 1.3 the bytes
   exported for `tls-exporter`, as turborel-client takes them. */
static bool take_channel(SSL *ssl, trl_tls_channel *channel) {
    memset(channel, 0, sizeof *channel);
    channel->secured = true;
#ifdef OPENSSL_IS_AWSLC
    X509 *peer = SSL_get_peer_certificate(ssl);
#else
    X509 *peer = SSL_get1_peer_certificate(ssl);
#endif
    if (peer != NULL) {
        int length = i2d_X509(peer, NULL);
        channel->certificate = length > 0 ? trl_malloc((size_t)length) : NULL;
        unsigned char *at = channel->certificate;
        if (channel->certificate != NULL && i2d_X509(peer, &at) == length) {
            channel->certificate_length = (size_t)length;
        }
        X509_free(peer);
        if (channel->certificate_length == 0U) {
            trl_tls_channel_free(channel);
            return false;
        }
    }
    if (SSL_version(ssl) == TLS1_3_VERSION) {
        static const char label[] = TRL_SCRAM_EXPORTER_LABEL;
        static const unsigned char empty[1] = {0U};
        channel->has_exporter = SSL_export_keying_material(ssl, channel->exporter, sizeof channel->exporter, label,
                                                           sizeof label - 1U, empty, 0U, 1) == 1;
    }
    return true;
}

/* Shakes hands, failing as turborel-client says it fails. */
static bool handshake(tls_state *state, trl_failure *failure) {
    ERR_clear_error();
    if (SSL_connect(state->ssl) == 1) {
        return true;
    }
    trl_io_error why;
    describe(state, &why);
    char message[TRL_FAILURE_MESSAGE_BYTES];
    (void)snprintf(message, sizeof message, "the TLS handshake failed: %s", why.text);
    trl_failure_broken(failure, message);
    return false;
}

bool trl_tls_secure(void *context, const char *name, trl_transport *transport, trl_tls_channel *channel,
                    trl_failure *failure) {
    memset(channel, 0, sizeof *channel);
    tls_state *state = begin((SSL_CTX *)context, transport);
    if (state == NULL) {
        trl_transport_close(transport);
        trl_failure_no_memory(failure);
        return false;
    }
    if (!name_server(state->ssl, name, failure) || !handshake(state, failure)) {
        release(state);
        return false;
    }
    if (!take_channel(state->ssl, channel)) {
        trl_failure_no_memory(failure);
        release(state);
        return false;
    }
    transport->ops = &tls_ops;
    transport->state = state;
    return true;
}
// NOLINTEND(misc-include-cleaner)

#else

bool trl_tls_secure(void *context, const char *name, trl_transport *transport, trl_tls_channel *channel,
                    trl_failure *failure) {
    (void)context;
    (void)name;
    memset(channel, 0, sizeof *channel);
    trl_transport_close(transport);
    trl_failure_set(failure, TRL_SQLCODE_UNSUPPORTED, TRL_SQLSTATE_UNSUPPORTED,
                    "TLS needs a TLS library, and this library was built without one");
    return false;
}

#endif

void trl_tls_channel_free(trl_tls_channel *channel) {
    free(channel->certificate);
    channel->certificate = NULL;
    channel->certificate_length = 0U;
    trl_cleanse(channel->exporter, sizeof channel->exporter);
    channel->has_exporter = false;
}
