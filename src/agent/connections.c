/**
 * @file connections.c
 * @brief The SQL-agent's connection statements [CLIENT The SQL-agent]:
 * CONNECT, SET CONNECTION and DISCONNECT [ISO/IEC 9075:1992 §15], as
 * turborel-client's agent runs them. No transaction spans servers, so
 * none of them leaves an active transaction.
 */
#include "agent/agent.h"
#include "agent/statement.h"
#include "bulk/names.h"
#include "codec/buffer.h"
#include "codec/error.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "stmt/result.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The SQLCODEs of the statements' failures but a connection's. */
#define TRL_SQLCODE_SYNTAX_RULE (-103)
#define TRL_SQLCODE_TRANSACTION_STATE (-917)
/* Their SQLSTATEs. */
#define TRL_SQLSTATE_SYNTAX "2A000"
#define TRL_SQLSTATE_FAILURE "08006"
#define TRL_SQLSTATE_UNABLE "08001"
#define TRL_SQLSTATE_REJECTED "08004"
#define TRL_SQLSTATE_IN_USE "08002"
#define TRL_SQLSTATE_MISSING "08003"
#define TRL_SQLSTATE_MULTIPLE "0A001"
#define TRL_SQLSTATE_CONNECTION_NAME "2E000"
#define TRL_SQLSTATE_TRANSACTION_STATE "25000"
#define TRL_SQLSTATE_DISCONNECT_ERROR "01002"
#define TRL_SQLSTATE_DONE "00000"
/* The rules of CONNECT. */
#define TRL_RULE_CONNECT "ISO/IEC 9075:1992 §15.1 GR"

/* Refuses to leave the current connection's active transaction: none
   spans servers [§15.1 GR1, §15.2 GR1]. */
static bool not_in_transaction(trl_agent *agent) {
    trl_conn *current = trl_agent_current(agent);
    if (current != NULL && trl_conn_in_transaction(current)) {
        (void)trl_agent_exception(agent, TRL_SQLSTATE_MULTIPLE, "ISO/IEC 9075:1992 §15.1 GR1, §15.2 GR1",
                                  "the current connection's transaction is active, and none spans servers");
        return false;
    }
    return true;
}

/* The character string `value` is, its spaces trimmed [§15.1 GR2, GR6],
   a literal here, there being no parameters; null-ended in place. */
static const char *text_of(trl_agent *agent, trl_agent_text *value) {
    if (value->kind != TRL_VALUE_TEXT) {
        trl_failure_set(&agent->failure, TRL_SQLCODE_SYNTAX_RULE, TRL_SQLSTATE_SYNTAX,
                        "a client's connection statement names by character string literals");
        return NULL;
    }
    if (value->text.state != TRL_BUFFER_OK) {
        (void)trl_agent_no_memory(agent);
        return NULL;
    }
    char *text = (char *)value->text.data;
    size_t length = strlen(text);
    while (length > 0U && text[length - 1U] == ' ') {
        text[--length] = '\0';
    }
    size_t lead = strspn(text, " ");
    return text + lead;
}

/* `text` as an <identifier>, if it is one [§15.1 GR3, GR6]: a regular
   identifier in upper case, or a delimited one's body, a copy the
   caller's to free; null if it is none, or memory cannot be had. */
static char *identifier(const char *text, bool *none) {
    trl_buffer name;
    trl_buffer_init(&name);
    bool is = trl_identifier_of(text, &name);
    *none = !is && name.state == TRL_BUFFER_OK;
    char *copy = is ? trl_malloc(name.length) : NULL;
    if (copy != NULL) {
        memcpy(copy, name.data, name.length);
    }
    trl_buffer_free(&name);
    return copy;
}

/* A copy of `text`; null if memory cannot be had. */
static char *copied(const char *text) {
    size_t length = strlen(text) + 1U;
    char *copy = trl_malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

/* The connection `connect` asks for, as the user its text names, its
   name `identified`; the agent's and current after [§15.1 GR8-GR11]. */
static trl_status connected_as(trl_agent *agent, const char *server, const char *user, char *identified) {
    trl_conn *conn = NULL;
    trl_failure lent;
    trl_failure_init(&lent);
    trl_status made = agent->connect(agent->context, server, user, &conn, &lent);
    if (made == TRL_OK && conn != NULL) {
        trl_failure_clear(&lent);
        if (!trl_agent_hold(agent, identified, conn)) {
            trl_conn_free(conn);
            free(identified);
            return trl_agent_no_memory(agent);
        }
        return TRL_OK;
    }
    free(identified);
    /* The connection's failure, else the one the connector told. */
    const trl_failure *why = conn != NULL ? trl_conn_failure(conn) : NULL;
    why = why == NULL && trl_failure_sqlcode(&lent) != 0 ? &lent : why;
    if (why == NULL || made == TRL_NO_MEMORY) {
        trl_failure_clear(&lent);
        trl_conn_free(conn);
        return trl_agent_no_memory(agent);
    }
    /* A server not reached, lost or never found, is one the client cannot
       connect to; any other failure is the server's refusal (RULES.md D8). */
    const char *told = trl_failure_sqlstate(why);
    bool unreached = strcmp(told, TRL_SQLSTATE_FAILURE) == 0 || strcmp(told, TRL_SQLSTATE_UNABLE) == 0;
    const char *state = unreached ? TRL_SQLSTATE_UNABLE : TRL_SQLSTATE_REJECTED;
    (void)trl_agent_exception(agent, state, TRL_RULE_CONNECT "10, GR11", trl_failure_message(why));
    trl_failure_clear(&lent);
    trl_conn_free(conn);
    return TRL_FAILED;
}

/* `text` as the connection's name, an identifier [§15.1 GR6], into
   `*named`. */
static trl_status identified(trl_agent *agent, const char *text, char **named) {
    bool none = false;
    *named = identifier(text, &none);
    if (*named != NULL) {
        return TRL_OK;
    }
    if (!none) {
        return trl_agent_no_memory(agent);
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "'%s' is not an <identifier>", text);
    return trl_agent_exception(agent, TRL_SQLSTATE_CONNECTION_NAME, TRL_RULE_CONNECT "6", message);
}

/* The connection's name: of AS, else the server's [§15.1 GR6]; checked
   as an identifier and as no connection's yet [GR7]. `*named` null of
   the default connection. */
static trl_status name_of(trl_agent *agent, trl_agent_statement *statement, const char *server, char **named) {
    *named = NULL;
    const char *text = server;
    if (statement->name.kind != TRL_VALUE_ABSENT) {
        text = text_of(agent, &statement->name);
        if (text == NULL) {
            return trl_agent_failed(agent);
        }
    }
    trl_status checked = text != NULL ? identified(agent, text, named) : TRL_OK;
    if (checked != TRL_OK || trl_agent_find(agent, *named) == agent->count) {
        return checked;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    if (*named == NULL) {
        trl_format(message, sizeof message, "the default connection is established");
    } else {
        trl_format(message, sizeof message, "the connection %s is established", *named);
    }
    free(*named);
    *named = NULL;
    return trl_agent_exception(agent, TRL_SQLSTATE_IN_USE, TRL_RULE_CONNECT "7", message);
}

/* CONNECT's user, its text, if it names one, checked as an
   authorization identifier [§15.1 GR2, GR3], into `*user`. */
static trl_status user_of(trl_agent *agent, trl_agent_statement *statement, const char **user) {
    *user = NULL;
    if (statement->user.kind == TRL_VALUE_ABSENT) {
        return TRL_OK;
    }
    *user = text_of(agent, &statement->user);
    if (*user == NULL) {
        return trl_agent_failed(agent);
    }
    bool none = false;
    char *checked = identifier(*user, &none);
    if (checked != NULL) {
        free(checked);
        return TRL_OK;
    }
    if (!none) {
        return trl_agent_no_memory(agent);
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "'%s' is not an <authorization identifier>", *user);
    return trl_agent_exception(agent, TRL_SQLSTATE_INVALID_AUTHORIZATION, TRL_RULE_CONNECT "3", message);
}

/* CONNECT TO a server [AS a name] [USER a user], or TO DEFAULT
   [§15.1]: the connection made current, the one current before dormant
   [GR9]. */
static trl_status connect_to(trl_agent *agent, trl_agent_statement *statement) {
    if (!not_in_transaction(agent)) {
        return TRL_FAILED;
    }
    const char *user = NULL;
    trl_status checked = user_of(agent, statement, &user);
    if (checked != TRL_OK) {
        return checked;
    }
    const char *server = NULL;
    if (statement->verb == TRL_AGENT_CONNECT) {
        server = text_of(agent, &statement->server);
        if (server == NULL) {
            return trl_agent_failed(agent);
        }
    }
    char *named = NULL;
    checked = name_of(agent, statement, server, &named);
    return checked == TRL_OK ? connected_as(agent, server, user, named) : checked;
}

/* The connection SET CONNECTION or DISCONNECT names: its text as an
   identifier, or as written if it is none, into `*name`, the caller's to
   free; null of DEFAULT. */
static trl_status named_by(trl_agent *agent, trl_agent_statement *statement, char **name) {
    *name = NULL;
    if (statement->object != TRL_AGENT_NAMED) {
        return TRL_OK;
    }
    const char *text = text_of(agent, &statement->name);
    if (text == NULL) {
        return trl_agent_failed(agent);
    }
    bool none = false;
    *name = identifier(text, &none);
    *name = *name == NULL && none ? copied(text) : *name;
    return *name == NULL ? trl_agent_no_memory(agent) : TRL_OK;
}

/* Fails as naming no connection held, `rule` the rule's. */
static trl_status missing(trl_agent *agent, const char *name, const char *rule) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    if (name == NULL) {
        trl_format(message, sizeof message, "there is no default connection");
    } else {
        trl_format(message, sizeof message, "there is no connection %s", name);
    }
    return trl_agent_exception(agent, TRL_SQLSTATE_MISSING, rule, message);
}

/* SET CONNECTION a name, or DEFAULT [§15.2]: that connection current,
   the one current before dormant. */
static trl_status set_connection(trl_agent *agent, trl_agent_statement *statement) {
    if (!not_in_transaction(agent)) {
        return TRL_FAILED;
    }
    char *name = NULL;
    trl_status named = named_by(agent, statement, &name);
    if (named != TRL_OK) {
        return named;
    }
    size_t at = trl_agent_find(agent, name);
    trl_status set = TRL_OK;
    if (at == agent->count) {
        set = missing(agent, name, "ISO/IEC 9075:1992 §15.2 GR2");
    } else {
        agent->current = at;
    }
    free(name);
    return set;
}

/* Whether the connection at `i` is one DISCONNECT ends. */
static bool ended_by(const trl_agent *agent, const trl_agent_statement *statement, size_t i, size_t named) {
    switch (statement->object) {
    case TRL_AGENT_ALL:
        return true;
    case TRL_AGENT_CURRENT:
        return i == agent->current;
    case TRL_AGENT_DEFAULT:
    case TRL_AGENT_NAMED:
    default:
        return i == named;
    }
}

/* The connection DISCONNECT names, if it names one: `count` of ALL. */
static trl_status disconnected(trl_agent *agent, trl_agent_statement *statement, size_t *at) {
    *at = agent->count;
    if (statement->object == TRL_AGENT_ALL) {
        return TRL_OK;
    }
    const char *rule = "ISO/IEC 9075:1992 §15.3 GR1-GR3";
    if (statement->object == TRL_AGENT_CURRENT) {
        *at = agent->current;
        return *at < agent->count
                   ? TRL_OK
                   : trl_agent_exception(agent, TRL_SQLSTATE_MISSING, rule, "there is no current connection");
    }
    char *name = NULL;
    trl_status named = named_by(agent, statement, &name);
    if (named != TRL_OK) {
        return named;
    }
    *at = trl_agent_find(agent, name);
    trl_status found = *at < agent->count ? TRL_OK : missing(agent, name, rule);
    free(name);
    return found;
}

/* DISCONNECT [§15.3]: each connection it names ended; none current
   after, if the current one was [GR9]; an error ending one a warning,
   01002 [GR8], into `state`. */
static trl_status disconnect(trl_agent *agent, trl_agent_statement *statement, const char **state) {
    size_t named = 0U;
    trl_status found = disconnected(agent, statement, &named);
    if (found != TRL_OK) {
        return found;
    }
    for (size_t i = 0U; i < agent->count; ++i) {
        if (ended_by(agent, statement, i, named) && trl_conn_in_transaction(agent->held[i].conn)) {
            trl_failure_cite(&agent->failure, TRL_SQLCODE_TRANSACTION_STATE, TRL_SQLSTATE_TRANSACTION_STATE,
                             "a connection's transaction is active", "ISO/IEC 9075:1992 §15.3 GR6");
            return TRL_FAILED;
        }
    }
    const trl_conn *current = trl_agent_current(agent);
    bool warned = false;
    size_t kept = 0U;
    for (size_t i = 0U; i < agent->count; ++i) {
        trl_held h = agent->held[i];
        if (ended_by(agent, statement, i, named)) {
            warned = trl_conn_terminate(h.conn) != TRL_OK || warned;
            trl_conn_free(h.conn);
            free(h.name);
        } else {
            agent->held[kept++] = h;
        }
    }
    agent->count = kept;
    agent->current = kept;
    for (size_t i = 0U; i < kept; ++i) {
        agent->current = agent->held[i].conn == current ? i : agent->current;
    }
    *state = warned ? TRL_SQLSTATE_DISCONNECT_ERROR : TRL_SQLSTATE_DONE;
    return TRL_OK;
}

/* The completion of a connection statement, of `state`. */
static trl_status completion(trl_agent *agent, const char *state, trl_result **out) {
    trl_result *result = trl_result_make();
    if (result == NULL) {
        return trl_agent_no_memory(agent);
    }
    result->sqlcode = 0;
    memcpy(result->sqlstate, state, TRL_SQLSTATE_BYTES);
    *out = result;
    return TRL_OK;
}

/* Runs a connection statement read. */
trl_status trl_agent_connection_statement(trl_agent *agent, trl_agent_statement *statement, trl_result **out) {
    const char *state = TRL_SQLSTATE_DONE;
    trl_status ran = TRL_OK;
    switch (statement->verb) {
    case TRL_AGENT_CONNECT:
    case TRL_AGENT_CONNECT_DEFAULT:
        ran = connect_to(agent, statement);
        break;
    case TRL_AGENT_SET:
        ran = set_connection(agent, statement);
        break;
    case TRL_AGENT_DISCONNECT:
    case TRL_AGENT_OTHER:
    default:
        ran = disconnect(agent, statement, &state);
        break;
    }
    return ran == TRL_OK ? completion(agent, state, out) : ran;
}
