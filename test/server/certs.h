/**
 * @file certs.h
 * @brief Test certificates, made as the live tests begin, so no secret is
 * ever kept in the repository: an authority, the server's certificate it
 * signs, a client's as ann, and another authority no one trusts.
 */
#ifndef TRL_TEST_SERVER_CERTS_H
#define TRL_TEST_SERVER_CERTS_H

#include <stdbool.h>

/**
 * @brief Writes into `dir`: ca.pem; server.pem and server.key, for
 * localhost and 127.0.0.1; client.pem and client.key, of CN=ann; and
 * other-ca.pem.
 */
bool certs_make(const char *dir);

#endif
