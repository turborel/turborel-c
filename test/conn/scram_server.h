/**
 * @file scram_server.h
 * @brief A SCRAM server of a test's own [PROTOCOL Mechanisms]: it checks
 * the client's proof as RFC 5802 does, and proves itself, or does not, as
 * the test asks.
 */
#ifndef TRL_TEST_CONN_SCRAM_SERVER_H
#define TRL_TEST_CONN_SCRAM_SERVER_H

#include "codec/buffer.h"
#include "script.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief The password the server knows. */
#define PASSWORD "ann's"
/** @brief The longest message of an exchange. */
#define TEXT_BYTES 512U

/** @brief What the server does at its last step. */
typedef enum ending {
    /** @brief Proves it knows the verifier, then takes the empty Answer. */
    ENDING_PROVED = 0,
    /** @brief Signs wrongly. */
    ENDING_WRONG_SIGNATURE,
    /** @brief Sends Ready without proving anything. */
    ENDING_UNPROVED,
} ending;

/** @brief The server's state between steps. */
typedef struct scram_server {
    /** @brief What it does at its last step. */
    ending ending;
    /** @brief client-first-message-bare, as read. */
    char bare[TEXT_BYTES];
    /** @brief server-first-message, as sent. */
    char first[TEXT_BYTES];
    /** @brief Whether the client's proof was right. */
    bool proof_right;
} scram_server;

/** @brief Answers a SCRAM login step by step; the script's data is a scram_server. */
void scram_serving(script *s, uint8_t kind, trl_slice body);

#endif
