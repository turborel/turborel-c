/**
 * @file binding_test.c
 * @brief `tls-server-end-point` [PROTOCOL Mechanisms]: the hash a
 * certificate's signature names chooses the binding's hash, as turborel's
 * own code chooses it.
 */
#include "auth/binding.h"
#include "auth/crypto.h"
#include "harness.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* DER's tags, and RSASSA-PSS's parameters' context tags. */
#define SEQUENCE 0x30U
#define OID 0x06U
#define PSS_HASH 0xa0U
#define PSS_MASK 0xa1U
#define BIT_STRING 0x03U
#define NULL_TAG 0x05U
#define ROOM 256U

/* Bytes being built. */
typedef struct der {
    uint8_t bytes[ROOM];
    size_t length;
} der;

static const uint8_t md5_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x04};
static const uint8_t sha1_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05};
static const uint8_t mgf1[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08};
static const uint8_t rsa_pss[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a};
static const uint8_t sha256_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};
static const uint8_t sha384_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c};
static const uint8_t sha512_rsa[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d};
static const uint8_t sha1_ecdsa[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x01};
static const uint8_t sha256_ecdsa[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
static const uint8_t sha384_ecdsa[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03};
static const uint8_t sha512_ecdsa[] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x04};
static const uint8_t sha1[] = {0x2b, 0x0e, 0x03, 0x02, 0x1a};
static const uint8_t sha256_hash[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
static const uint8_t sha384_hash[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02};
static const uint8_t sha512_hash[] = {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03};
static const uint8_t ed25519[] = {0x2b, 0x65, 0x70};

/* An element of `tag` whose body is `body`, short form. */
static der tlv(uint8_t tag, const uint8_t *body, size_t length) {
    der out = {{0}, 0U};
    out.bytes[0] = tag;
    out.bytes[1] = (uint8_t)length;
    if (length > 0U) {
        memcpy(out.bytes + 2U, body, length);
    }
    out.length = length + 2U;
    return out;
}

/* Two pieces of DER, one after the other. */
static der join(der a, der b) {
    memcpy(a.bytes + a.length, b.bytes, b.length);
    a.length += b.length;
    return a;
}

/* An AlgorithmIdentifier of `oid` and `parameters`. */
static der algorithm(const uint8_t *oid, size_t length, der parameters) {
    der body = join(tlv(OID, oid, length), parameters);
    return tlv(SEQUENCE, body.bytes, body.length);
}

/* No DER at all. */
static der nothing(void) {
    der out = {{0}, 0U};
    return out;
}

/* A certificate of `signature`: a TBSCertificate passed over, the
   signature's algorithm, and its bits. */
static der certificate(der signature) {
    static const uint8_t version[] = {0x02, 1, 7};
    static const uint8_t bits[] = {0};
    der body = join(join(tlv(SEQUENCE, version, sizeof version), signature), tlv(BIT_STRING, bits, sizeof bits));
    return tlv(SEQUENCE, body.bytes, body.length);
}

/* RSASSA-PSS's parameters of a hash and a mask's hash, each if given. */
static der pss_of(const uint8_t *hash, size_t hash_length, const uint8_t *mask, size_t mask_length) {
    der fields = nothing();
    der parameters = tlv(NULL_TAG, NULL, 0U);
    if (hash != NULL) {
        der named = algorithm(hash, hash_length, parameters);
        fields = join(fields, tlv(PSS_HASH, named.bytes, named.length));
    }
    if (mask != NULL) {
        der named = algorithm(mgf1, sizeof mgf1, algorithm(mask, mask_length, parameters));
        fields = join(fields, tlv(PSS_MASK, named.bytes, named.length));
    }
    return algorithm(rsa_pss, sizeof rsa_pss, tlv(SEQUENCE, fields.bytes, fields.length));
}

/* Whether `cert` is bound by `hash`, its binding that hash of it. */
static bool bound_by(der cert, trl_hash hash) {
    trl_hash found = TRL_HASH_SHA256;
    if (!trl_binding_hash(cert.bytes, cert.length, &found) || found != hash) {
        return false;
    }
    if (!trl_crypto_built()) {
        return true;
    }
    uint8_t binding[TRL_HASH_MAX_BYTES];
    uint8_t expected[TRL_HASH_MAX_BYTES];
    size_t length = 0U;
    return trl_end_point(cert.bytes, cert.length, binding, &length) && length == trl_hash_bytes(hash) &&
           trl_hash_of(hash, cert.bytes, cert.length, expected) && memcmp(binding, expected, length) == 0;
}

/* Whether `cert` has no binding. */
static bool unbound(der cert) {
    trl_hash found = TRL_HASH_SHA256;
    return !trl_binding_hash(cert.bytes, cert.length, &found);
}

/**
 * Verifies: [PROTOCOL Mechanisms] the signature's hash chooses the
 * binding's, SHA-256 for MD5 and SHA-1, of RSA and ECDSA; ECDSA's absent
 * parameters taken.
 */
static void the_signature_hash_chooses_the_binding_hash(trl_test *test) {
    der null = tlv(NULL_TAG, NULL, 0U);
    TRL_CHECK(test, bound_by(certificate(algorithm(md5_rsa, sizeof md5_rsa, null)), TRL_HASH_SHA256));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha1_rsa, sizeof sha1_rsa, null)), TRL_HASH_SHA256));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha1_ecdsa, sizeof sha1_ecdsa, null)), TRL_HASH_SHA256));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha256_rsa, sizeof sha256_rsa, null)), TRL_HASH_SHA256));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha256_ecdsa, sizeof sha256_ecdsa, null)), TRL_HASH_SHA256));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha384_rsa, sizeof sha384_rsa, null)), TRL_HASH_SHA384));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha384_ecdsa, sizeof sha384_ecdsa, null)), TRL_HASH_SHA384));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha512_rsa, sizeof sha512_rsa, null)), TRL_HASH_SHA512));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha512_ecdsa, sizeof sha512_ecdsa, null)), TRL_HASH_SHA512));
    TRL_CHECK(test, bound_by(certificate(algorithm(sha384_ecdsa, sizeof sha384_ecdsa, nothing())), TRL_HASH_SHA384));
}

/**
 * Verifies: [PROTOCOL Mechanisms] RSASSA-PSS is bound by its one hash,
 * SHA-1 by default, its mask's the same; two hashes, a mask not MGF1, a
 * hash not known or no parameters bind nothing.
 */
static void rsassa_pss_is_bound_by_its_one_hash(trl_test *test) {
    TRL_CHECK(test, bound_by(certificate(pss_of(sha384_hash, sizeof sha384_hash, sha384_hash, sizeof sha384_hash)),
                             TRL_HASH_SHA384));
    TRL_CHECK(test, bound_by(certificate(pss_of(NULL, 0U, NULL, 0U)), TRL_HASH_SHA256));
    TRL_CHECK(test, bound_by(certificate(pss_of(sha1, sizeof sha1, NULL, 0U)), TRL_HASH_SHA256));
    TRL_CHECK(test, unbound(certificate(pss_of(sha256_hash, sizeof sha256_hash, NULL, 0U))));
    TRL_CHECK(test, unbound(certificate(pss_of(sha512_hash, sizeof sha512_hash, sha256_hash, sizeof sha256_hash))));
    TRL_CHECK(test, unbound(certificate(pss_of(mgf1, sizeof mgf1, mgf1, sizeof mgf1))));
    der named = algorithm(sha256_hash, sizeof sha256_hash, nothing());
    der hash = tlv(PSS_HASH, named.bytes, named.length);
    der other = algorithm(sha1_rsa, sizeof sha1_rsa, algorithm(sha256_hash, sizeof sha256_hash, nothing()));
    der fields = join(hash, tlv(PSS_MASK, other.bytes, other.length));
    TRL_CHECK(test,
              unbound(certificate(algorithm(rsa_pss, sizeof rsa_pss, tlv(SEQUENCE, fields.bytes, fields.length)))));
    TRL_CHECK(test, unbound(certificate(algorithm(rsa_pss, sizeof rsa_pss, nothing()))));
}

/**
 * Verifies: [PROTOCOL Mechanisms] a signature of no hash, as Ed25519's,
 * a certificate cut anywhere, or one of no algorithm, binds nothing.
 */
static void no_binding_without_one_known_hash(trl_test *test) {
    TRL_CHECK(test, unbound(certificate(algorithm(ed25519, sizeof ed25519, nothing()))));
    der whole = certificate(algorithm(sha256_ecdsa, sizeof sha256_ecdsa, nothing()));
    for (size_t cut = 0U; cut < whole.length; ++cut) {
        der part = whole;
        part.length = cut;
        TRL_CHECK(test, unbound(part));
    }
    static const uint8_t null_body[] = {NULL_TAG, 0};
    TRL_CHECK(test, unbound(certificate(tlv(SEQUENCE, null_body, sizeof null_body))));
    static const uint8_t long_form[] = {SEQUENCE, 0x85, 0, 0, 0, 0, 1};
    static const uint8_t no_length[] = {SEQUENCE, 0x80, 0};
    static const uint8_t cut_length[] = {SEQUENCE, 0x82, 1};
    trl_hash found = TRL_HASH_SHA256;
    TRL_CHECK(test, !trl_binding_hash(long_form, sizeof long_form, &found));
    TRL_CHECK(test, !trl_binding_hash(no_length, sizeof no_length, &found));
    TRL_CHECK(test, !trl_binding_hash(cut_length, sizeof cut_length, &found));
    der bare = tlv(PSS_HASH, null_body, sizeof null_body);
    der wrong = algorithm(rsa_pss, sizeof rsa_pss, tlv(SEQUENCE, bare.bytes, bare.length));
    TRL_CHECK(test, unbound(certificate(wrong)));
    der mask = tlv(PSS_MASK, null_body, sizeof null_body);
    der wrong_mask = algorithm(rsa_pss, sizeof rsa_pss, tlv(SEQUENCE, mask.bytes, mask.length));
    TRL_CHECK(test, unbound(certificate(wrong_mask)));
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(the_signature_hash_chooses_the_binding_hash),
        TRL_TEST_CASE(rsassa_pss_is_bound_by_its_one_hash),
        TRL_TEST_CASE(no_binding_without_one_known_hash),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
