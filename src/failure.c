/**
 * @file failure.c
 * @brief The failure a handle keeps (RULES.md D9): its strings held in one
 * block it owns, or, where memory cannot be had for them, the failure of
 * memory not had, which needs none.
 */
#include "failure.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "codec/fields.h"
#include "format.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The SQLSTATE of a connection that does not exist, the one of class 08
   that leaves a connection open [PROTOCOL Error 'E']. */
#define TRL_SQLSTATE_NO_CONNECTION "08003"
/* The characters of a SQLSTATE's class. */
#define TRL_SQLSTATE_CLASS 2U
/* The severities of the driver's own failures [PROTOCOL Errors and notices]. */
#define TRL_SEVERITY_FATAL "FATAL"
#define TRL_SEVERITY_ERROR "ERROR"
/* The strings a failure holds: its message, its rule, and its diagnostics. */
#define TRL_FAILURE_STRINGS (2U + TRL_DIAGNOSTICS)
#define TRL_STRING_MESSAGE 0U
#define TRL_STRING_RULE 1U
#define TRL_STRING_DIAGNOSTICS 2U

/* The strings a failure is made of, each a view, absent if `bytes` is null. */
typedef struct strings {
    trl_slice of[TRL_FAILURE_STRINGS];
} strings;

void trl_failure_init(trl_failure *failure) {
    memset(failure, 0, sizeof *failure);
    failure->message = "";
}

void trl_failure_clear(trl_failure *failure) {
    free(failure->block);
    trl_failure_init(failure);
}

/* The failure of memory not had, kept in strings of the library's own. */
static void set_no_memory(trl_failure *failure) {
    trl_failure_clear(failure);
    failure->sqlcode = TRL_SQLCODE_RESOURCES;
    trl_format(failure->sqlstate, sizeof failure->sqlstate, "%s", TRL_SQLSTATE_RESOURCES);
    failure->message = "memory could not be had";
    failure->rule = TRL_RULE_DESIGN;
    failure->diagnostics[0] = TRL_SEVERITY_ERROR;
}

/* Copies `from`'s strings into one block `failure` owns, and points each
   of its strings there; the message is "" if absent. */
static bool keep(trl_failure *failure, const strings *from) {
    size_t total = 0U;
    for (size_t i = 0U; i < TRL_FAILURE_STRINGS; ++i) {
        total += from->of[i].bytes == NULL ? 0U : from->of[i].length + 1U;
    }
    char *block = trl_malloc(total == 0U ? 1U : total);
    if (block == NULL) {
        return false;
    }
    const char *kept[TRL_FAILURE_STRINGS] = {NULL};
    char *at = block;
    for (size_t i = 0U; i < TRL_FAILURE_STRINGS; ++i) {
        if (from->of[i].bytes != NULL) {
            if (from->of[i].length > 0U) {
                memcpy(at, from->of[i].bytes, from->of[i].length);
            }
            at[from->of[i].length] = '\0';
            kept[i] = at;
            at += from->of[i].length + 1U;
        }
    }
    failure->block = block;
    failure->message = kept[TRL_STRING_MESSAGE] == NULL ? "" : kept[TRL_STRING_MESSAGE];
    failure->rule = kept[TRL_STRING_RULE];
    for (size_t i = 0U; i < TRL_DIAGNOSTICS; ++i) {
        failure->diagnostics[i] = kept[TRL_STRING_DIAGNOSTICS + i];
    }
    return true;
}

/* A view of a null-ended string; absent if it is null. */
static trl_slice text(const char *string) {
    trl_slice slice = {(const uint8_t *)string, string == NULL ? 0U : strlen(string)};
    return slice;
}

/* Sets the SQLCODE and SQLSTATE, and keeps the strings, or else sets the
   failure of memory not had. */
static void set_all(trl_failure *failure, int32_t sqlcode, trl_slice sqlstate, const strings *from) {
    trl_failure_clear(failure);
    if (!keep(failure, from)) {
        set_no_memory(failure);
        return;
    }
    failure->sqlcode = sqlcode;
    size_t length = sqlstate.length < TRL_SQLSTATE_BYTES - 1U ? sqlstate.length : TRL_SQLSTATE_BYTES - 1U;
    if (length > 0U) {
        memcpy(failure->sqlstate, sqlstate.bytes, length);
    }
    failure->sqlstate[length] = '\0';
}

bool trl_failure_ends_connection(const trl_failure *failure) {
    if (failure == NULL) {
        return false;
    }
    bool another = strcmp(failure->sqlstate, TRL_SQLSTATE_NO_CONNECTION) == 0;
    bool connecting = failure->sqlcode == TRL_SQLCODE_CONNECTION;
    return (strncmp(failure->sqlstate, "08", TRL_SQLSTATE_CLASS) == 0 && !another) ||
           (connecting && strncmp(failure->sqlstate, "28", TRL_SQLSTATE_CLASS) == 0);
}

void trl_failure_set(trl_failure *failure, int32_t sqlcode, const char *sqlstate, const char *message) {
    trl_failure probe;
    trl_failure_init(&probe);
    probe.sqlcode = sqlcode;
    trl_format(probe.sqlstate, sizeof probe.sqlstate, "%s", sqlstate);
    strings from = {{{NULL, 0U}}};
    from.of[TRL_STRING_MESSAGE] = text(message);
    from.of[TRL_STRING_RULE] = text(TRL_RULE_DESIGN);
    from.of[TRL_STRING_DIAGNOSTICS] =
        text(trl_failure_ends_connection(&probe) ? TRL_SEVERITY_FATAL : TRL_SEVERITY_ERROR);
    set_all(failure, sqlcode, text(sqlstate), &from);
}

void trl_failure_broken(trl_failure *failure, const char *message) {
    trl_failure_set(failure, TRL_SQLCODE_CONNECTION, TRL_SQLSTATE_CONNECTION_FAILURE, message);
}

void trl_failure_no_memory(trl_failure *failure) {
    set_no_memory(failure);
}

void trl_failure_from_codec(trl_failure *failure, const trl_codec_error *error) {
    if (error->sqlcode == TRL_SQLCODE_RESOURCES) {
        set_no_memory(failure);
        return;
    }
    trl_failure_set(failure, error->sqlcode, error->sqlstate, error->message);
}

void trl_failure_from_wire(trl_failure *failure, const trl_wire_failure *wire) {
    strings from = {{{NULL, 0U}}};
    from.of[TRL_STRING_MESSAGE] = wire->message;
    if (from.of[TRL_STRING_MESSAGE].bytes == NULL) {
        from.of[TRL_STRING_MESSAGE] = text("");
    }
    if (wire->cited) {
        from.of[TRL_STRING_RULE] = wire->rule.bytes == NULL ? text("") : wire->rule;
    }
    for (size_t i = 0U; i < TRL_DIAGNOSTICS; ++i) {
        if (wire->diagnostics.told[i]) {
            trl_slice told = wire->diagnostics.text[i];
            from.of[TRL_STRING_DIAGNOSTICS + i] = told.bytes == NULL ? text("") : told;
        }
    }
    set_all(failure, wire->sqlcode, wire->sqlstate, &from);
    if (failure->sqlcode == wire->sqlcode) {
        failure->placed = wire->placed;
        failure->position = wire->position;
        failure->active = wire->active;
    }
}

void trl_failure_copy(trl_failure *failure, const trl_failure *from) {
    if (failure == from) {
        return;
    }
    strings of = {{{NULL, 0U}}};
    of.of[TRL_STRING_MESSAGE] = text(from->message);
    of.of[TRL_STRING_RULE] = text(from->rule);
    for (size_t i = 0U; i < TRL_DIAGNOSTICS; ++i) {
        of.of[TRL_STRING_DIAGNOSTICS + i] = text(from->diagnostics[i]);
    }
    set_all(failure, from->sqlcode, text(from->sqlstate), &of);
    if (failure->sqlcode == from->sqlcode) {
        failure->placed = from->placed;
        failure->position = from->position;
        failure->active = from->active;
    }
}

int32_t trl_failure_sqlcode(const trl_failure *failure) {
    return failure == NULL ? 0 : failure->sqlcode;
}

const char *trl_failure_sqlstate(const trl_failure *failure) {
    return failure == NULL ? "" : failure->sqlstate;
}

const char *trl_failure_message(const trl_failure *failure) {
    return failure == NULL ? "" : failure->message;
}

const char *trl_failure_rule(const trl_failure *failure) {
    return failure == NULL ? NULL : failure->rule;
}

bool trl_failure_position(const trl_failure *failure, uint64_t *position) {
    if (failure == NULL || !failure->placed) {
        return false;
    }
    if (position != NULL) {
        *position = failure->position;
    }
    return true;
}

bool trl_failure_active(const trl_failure *failure) {
    return failure != NULL && failure->active;
}

const char *trl_failure_diagnostic(const trl_failure *failure, trl_diagnostic which) {
    size_t index = (size_t)which;
    if (failure == NULL || index >= TRL_DIAGNOSTICS) {
        return NULL;
    }
    return failure->diagnostics[index];
}

size_t trl_failure_text(const trl_failure *failure, char *out, size_t capacity) {
    if (failure == NULL || (out == NULL && capacity > 0U)) {
        return 0U;
    }
    char rule[TRL_FAILURE_MESSAGE_BYTES] = "";
    if (failure->rule != NULL) {
        trl_format(rule, sizeof rule, " [%s]", failure->rule);
    }
    char place[TRL_FAILURE_MESSAGE_BYTES] = "";
    if (failure->placed) {
        trl_format(place, sizeof place, " (at character %llu)", (unsigned long long)failure->position);
    }
    int written = snprintf(out, capacity, "SQLCODE %ld (SQLSTATE %s): %s%s%s", (long)failure->sqlcode,
                           failure->sqlstate, failure->message, rule, place);
    return written < 0 ? 0U : (size_t)written;
}
