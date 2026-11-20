/**
 * @file binding.c
 * @brief `tls-server-end-point` (RFC 5929 section 4.1), read from the
 * certificate's DER as turborel's own code reads it: the certificate's
 * TBSCertificate passed over, its signatureAlgorithm's identifier mapped
 * to its hash.
 */
#include "auth/binding.h"

#include "auth/crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* DER's tags of a SEQUENCE and an OBJECT IDENTIFIER, and RSASSA-PSS's
   parameters' context tags of its hash and its mask. */
#define TRL_DER_SEQUENCE 0x30U
#define TRL_DER_OID 0x06U
#define TRL_PSS_HASH 0xa0U
#define TRL_PSS_MASK 0xa1U
/* A long form length: the bit that says it, and the most bytes it takes. */
#define TRL_DER_LONG 0x80U
#define TRL_DER_LENGTH_BYTES 0x7fU
#define TRL_DER_MAX_LENGTH_BYTES 4U
#define TRL_BYTE_BITS 8U
/* An element's tag and first length byte. */
#define TRL_DER_HEAD_BYTES 2U

/* A view of DER. */
typedef struct der {
    const uint8_t *bytes;
    size_t length;
} der;

/* An identifier's bytes, and the hash it maps to. */
typedef struct algorithm {
    const uint8_t *oid;
    size_t length;
    trl_hash hash;
} algorithm;

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

/* The signatures, and the hash each binds by: SHA-256 for MD5 and SHA-1. */
static const algorithm signatures[] = {
    {md5_rsa, sizeof md5_rsa, TRL_HASH_SHA256},           {sha1_rsa, sizeof sha1_rsa, TRL_HASH_SHA256},
    {sha256_rsa, sizeof sha256_rsa, TRL_HASH_SHA256},     {sha384_rsa, sizeof sha384_rsa, TRL_HASH_SHA384},
    {sha512_rsa, sizeof sha512_rsa, TRL_HASH_SHA512},     {sha1_ecdsa, sizeof sha1_ecdsa, TRL_HASH_SHA256},
    {sha256_ecdsa, sizeof sha256_ecdsa, TRL_HASH_SHA256}, {sha384_ecdsa, sizeof sha384_ecdsa, TRL_HASH_SHA384},
    {sha512_ecdsa, sizeof sha512_ecdsa, TRL_HASH_SHA512},
};

/* RSASSA-PSS's hashes, and the hash each binds by. */
static const algorithm hashes[] = {
    {sha1, sizeof sha1, TRL_HASH_SHA256},
    {sha256_hash, sizeof sha256_hash, TRL_HASH_SHA256},
    {sha384_hash, sizeof sha384_hash, TRL_HASH_SHA384},
    {sha512_hash, sizeof sha512_hash, TRL_HASH_SHA512},
};

/* Reads one element of `in`: its tag, its body, and what follows it. */
static bool element(der in, uint8_t *tag, der *body, der *rest) {
    if (in.length < TRL_DER_HEAD_BYTES) {
        return false;
    }
    *tag = in.bytes[0];
    uint8_t first = in.bytes[1];
    size_t at = TRL_DER_HEAD_BYTES;
    size_t length = first;
    if ((first & TRL_DER_LONG) != 0U) {
        size_t count = first & TRL_DER_LENGTH_BYTES;
        if (count == 0U || count > TRL_DER_MAX_LENGTH_BYTES || in.length - at < count) {
            return false;
        }
        length = 0U;
        for (size_t i = 0U; i < count; ++i) {
            length = (length << TRL_BYTE_BITS) | in.bytes[at + i];
        }
        at += count;
    }
    if (in.length - at < length) {
        return false;
    }
    body->bytes = in.bytes + at;
    body->length = length;
    rest->bytes = in.bytes + at + length;
    rest->length = in.length - at - length;
    return true;
}

/* Reads one element of `tag`. */
static bool expect(der in, uint8_t tag, der *body, der *rest) {
    uint8_t found = 0U;
    return element(in, &found, body, rest) && found == tag;
}

/* An AlgorithmIdentifier's OID, and its parameters after it. */
static bool identifier(der in, der *oid, der *parameters) {
    der body;
    der rest;
    return expect(in, TRL_DER_SEQUENCE, &body, &rest) && expect(body, TRL_DER_OID, oid, parameters);
}

/* Whether `oid` is `bytes`. */
static bool is(der oid, const uint8_t *bytes, size_t length) {
    return oid.length == length && memcmp(oid.bytes, bytes, length) == 0;
}

/* The hash `oid` maps to among `table`'s `count`. */
static bool find(der oid, const algorithm *table, size_t count, trl_hash *out) {
    for (size_t i = 0U; i < count; ++i) {
        if (is(oid, table[i].oid, table[i].length)) {
            *out = table[i].hash;
            return true;
        }
    }
    return false;
}

/* RSASSA-PSS's one hash: its hash and its mask's, SHA-1 each by default. */
static bool pss(der parameters, trl_hash *out) {
    der fields;
    der rest;
    if (!expect(parameters, TRL_DER_SEQUENCE, &fields, &rest)) {
        return false;
    }
    der hash = {sha1, sizeof sha1};
    der explicit_hash;
    der after;
    if (expect(fields, TRL_PSS_HASH, &explicit_hash, &after)) {
        der ignored;
        if (!identifier(explicit_hash, &hash, &ignored)) {
            return false;
        }
        fields = after;
    }
    der mask = {sha1, sizeof sha1};
    der explicit_mask;
    if (expect(fields, TRL_PSS_MASK, &explicit_mask, &after)) {
        der function;
        der of;
        der ignored;
        if (!identifier(explicit_mask, &function, &of) || !is(function, mgf1, sizeof mgf1) ||
            !identifier(of, &mask, &ignored)) {
            return false;
        }
    }
    if (hash.length != mask.length || memcmp(hash.bytes, mask.bytes, hash.length) != 0) {
        return false;
    }
    return find(hash, hashes, sizeof hashes / sizeof hashes[0], out);
}

bool trl_binding_hash(const uint8_t *certificate, size_t length, trl_hash *out) {
    der whole = {certificate, length};
    der body;
    der rest;
    der tbs;
    der after;
    der oid;
    der parameters;
    if (!expect(whole, TRL_DER_SEQUENCE, &body, &rest) || !expect(body, TRL_DER_SEQUENCE, &tbs, &after) ||
        !identifier(after, &oid, &parameters)) {
        return false;
    }
    if (is(oid, rsa_pss, sizeof rsa_pss)) {
        return pss(parameters, out);
    }
    return find(oid, signatures, sizeof signatures / sizeof signatures[0], out);
}

bool trl_end_point(const uint8_t *certificate, size_t length, uint8_t *out, size_t *written) {
    trl_hash hash = TRL_HASH_SHA256;
    if (!trl_binding_hash(certificate, length, &hash) || !trl_hash_of(hash, certificate, length, out)) {
        return false;
    }
    *written = trl_hash_bytes(hash);
    return true;
}
