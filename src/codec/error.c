/**
 * @file error.c
 * @brief Codec failures recorded, the first one kept (RULES.md D9).
 */
#include "codec/error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void trl_codec_fail(trl_codec_error *error, int32_t sqlcode, const char *sqlstate, const char *message) {
    if (error == NULL || error->sqlcode != 0) {
        return;
    }
    error->sqlcode = sqlcode;
    (void)snprintf(error->sqlstate, sizeof error->sqlstate, "%s", sqlstate);
    (void)snprintf(error->message, sizeof error->message, "%s", message);
}

void trl_codec_malformed(trl_codec_error *error, const char *what, size_t at, const char *why) {
    char message[TRL_CODEC_MESSAGE_BYTES];
    /* A message cut short by its bound still says what failed first. */
    (void)snprintf(message, sizeof message, "a %s received is malformed at byte %zu: %s", what, at, why);
    trl_codec_fail(error, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_CONNECTION_FAILURE, message);
}

void trl_codec_no_memory(trl_codec_error *error) {
    trl_codec_fail(error, TRL_SQLCODE_RESOURCES, TRL_SQLSTATE_RESOURCES, "memory could not be had");
}
