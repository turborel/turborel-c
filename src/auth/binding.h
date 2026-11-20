/**
 * @file binding.h
 * @brief SCRAM's channel binding `tls-server-end-point` (RFC 5929): the
 * hash of the server's certificate, by the hash its signature uses.
 */
#ifndef TRL_AUTH_BINDING_H
#define TRL_AUTH_BINDING_H

#include "auth/crypto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief The hash a certificate, DER, is bound by: that of its signature,
 * SHA-256 for MD5 and SHA-1; of RSASSA-PSS, its one hash, its mask's the
 * same.
 *
 * @return Whether it has one: none of a signature of no hash, as
 * Ed25519's, or of two, or of a certificate malformed.
 */
bool trl_binding_hash(const uint8_t *certificate, size_t length, trl_hash *out);

/**
 * @brief The binding of a certificate, DER: its hash by
 * trl_binding_hash(), into `out`, of TRL_HASH_MAX_BYTES; `written` gets
 * its bytes.
 */
bool trl_end_point(const uint8_t *certificate, size_t length, uint8_t *out, size_t *written);

#endif
