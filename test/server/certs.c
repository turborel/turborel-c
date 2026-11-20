/**
 * @file certs.c
 * @brief Test certificates of ECDSA P-256 with SHA-256, made by the TLS
 * library, each signed by the test's own authority.
 */
#include "certs.h"

/* The platform's and the TLS library's headers declare more than the
   include checker can map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include "format.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define PATH_BYTES 1024U
#define ONE_HOUR 3600L
#define ONE_DAY 86400L

/* A key pair of P-256. */
static EVP_PKEY *new_key(void) {
    EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    EVP_PKEY *key = NULL;
    if (context == NULL || EVP_PKEY_keygen_init(context) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context, NID_X9_62_prime256v1) != 1 ||
        EVP_PKEY_keygen(context, &key) != 1) {
        key = NULL;
    }
    EVP_PKEY_CTX_free(context);
    return key;
}

/* Adds the extension `nid` of `value`, of `cert` issued by `issuer`. */
static bool extend(X509 *cert, X509 *issuer, int nid, const char *value) {
    X509V3_CTX context;
    X509V3_set_ctx(&context, issuer, cert, NULL, NULL, 0);
    char text[PATH_BYTES];
    trl_format(text, sizeof text, "%s", value);
    X509_EXTENSION *extension = X509V3_EXT_conf_nid(NULL, &context, nid, text);
    bool added = extension != NULL && X509_add_ext(cert, extension, -1) == 1;
    X509_EXTENSION_free(extension);
    return added;
}

/* A certificate of `name` and `key`, not yet signed, issued by `issuer`,
   or by itself if `issuer` is null. */
static X509 *new_cert(const char *name, long serial, EVP_PKEY *key, X509 *issuer) {
    X509 *cert = X509_new();
    X509_NAME *subject = X509_NAME_new();
    bool made = cert != NULL && subject != NULL &&
                X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (const unsigned char *)name, -1, -1, 0) == 1 &&
                X509_set_version(cert, 2) == 1 && ASN1_INTEGER_set(X509_get_serialNumber(cert), serial) == 1 &&
                X509_gmtime_adj(X509_getm_notBefore(cert), -ONE_HOUR) != NULL &&
                X509_gmtime_adj(X509_getm_notAfter(cert), ONE_DAY) != NULL && X509_set_pubkey(cert, key) == 1 &&
                X509_set_subject_name(cert, subject) == 1 &&
                X509_set_issuer_name(cert, issuer == NULL ? subject : X509_get_subject_name(issuer)) == 1;
    X509_NAME_free(subject);
    if (!made) {
        X509_free(cert);
        return NULL;
    }
    return cert;
}

/* Signs `cert` with `key`. */
static bool sign(X509 *cert, EVP_PKEY *key) {
    return X509_sign(cert, key, EVP_sha256()) > 0;
}

/* Writes `cert`, and `key` if not null, to `dir`/`name`.pem and .key. */
static bool write_out(const char *dir, const char *name, X509 *cert, EVP_PKEY *key) {
    char path[PATH_BYTES];
    trl_format(path, sizeof path, "%s/%s.pem", dir, name);
    BIO *file = BIO_new_file(path, "w");
    bool written = file != NULL && PEM_write_bio_X509(file, cert) == 1;
    BIO_free(file);
    if (written && key != NULL) {
        trl_format(path, sizeof path, "%s/%s.key", dir, name);
        file = BIO_new_file(path, "w");
        written = file != NULL && PEM_write_bio_PrivateKey(file, key, NULL, NULL, 0, NULL, NULL) == 1;
        BIO_free(file);
    }
    return written;
}

/* An authority of `name`, its certificate and key. */
static bool authority(const char *name, long serial, X509 **cert, EVP_PKEY **key) {
    *key = new_key();
    *cert = *key == NULL ? NULL : new_cert(name, serial, *key, NULL);
    return *cert != NULL && extend(*cert, *cert, NID_basic_constraints, "critical,CA:TRUE") &&
           extend(*cert, *cert, NID_key_usage, "critical,keyCertSign,cRLSign") &&
           extend(*cert, *cert, NID_subject_key_identifier, "hash") && sign(*cert, *key);
}

/* An authority: its certificate and key. */
typedef struct signer {
    X509 *cert;
    EVP_PKEY *key;
} signer;

/* What a certificate the authority signs is: its file, its name, its
   usage, and its other names, if any. */
typedef struct leaf_of {
    const char *file;
    const char *name;
    const char *usage;
    const char *names;
} leaf_of;

/* A certificate the authority signs, written to `dir`. */
static bool leaf(const char *dir, const leaf_of *of, const signer *ca) {
    static long serial = 10L;
    const char *file = of->file;
    const char *name = of->name;
    const char *usage = of->usage;
    const char *names = of->names;
    X509 *ca_cert = ca->cert;
    EVP_PKEY *ca_key = ca->key;
    EVP_PKEY *key = new_key();
    X509 *cert = key == NULL ? NULL : new_cert(name, ++serial, key, ca_cert);
    bool made = cert != NULL && extend(cert, ca_cert, NID_basic_constraints, "CA:FALSE") &&
                extend(cert, ca_cert, NID_key_usage, "critical,digitalSignature") &&
                extend(cert, ca_cert, NID_ext_key_usage, usage) &&
                extend(cert, ca_cert, NID_authority_key_identifier, "keyid") &&
                (names == NULL || extend(cert, ca_cert, NID_subject_alt_name, names)) && sign(cert, ca_key) &&
                write_out(dir, file, cert, key);
    X509_free(cert);
    EVP_PKEY_free(key);
    return made;
}

bool certs_make(const char *dir) {
    X509 *ca = NULL;
    EVP_PKEY *ca_key = NULL;
    X509 *other = NULL;
    EVP_PKEY *other_key = NULL;
    static const leaf_of server = {"server", "localhost", "serverAuth", "DNS:localhost,IP:127.0.0.1"};
    static const leaf_of client = {"client", "ann", "clientAuth", NULL};
    bool trusted = authority("turborel-c test authority", 1L, &ca, &ca_key) && write_out(dir, "ca", ca, NULL);
    signer signs = {ca, ca_key};
    bool made = trusted && leaf(dir, &server, &signs) && leaf(dir, &client, &signs) &&
                authority("turborel-c untrusted authority", 2L, &other, &other_key) &&
                write_out(dir, "other-ca", other, NULL);
    X509_free(ca);
    EVP_PKEY_free(ca_key);
    X509_free(other);
    EVP_PKEY_free(other_key);
    return made;
}
// NOLINTEND(misc-include-cleaner)
