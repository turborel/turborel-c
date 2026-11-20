/**
 * @file statement.h
 * @brief The statements of an SQL-agent's connections, CONNECT, SET
 * CONNECTION and DISCONNECT, read from SQL text as Turborel's parser
 * reads them [CLIENT The SQL-agent].
 */
#ifndef TRL_AGENT_STATEMENT_H
#define TRL_AGENT_STATEMENT_H

#include "codec/buffer.h"
#include "failure.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief Which statement it is. */
typedef enum trl_agent_verb {
    /** @brief None of them: a statement for the current connection. */
    TRL_AGENT_OTHER = 0,
    /** @brief CONNECT TO a server [AS a name] [USER a user]. */
    TRL_AGENT_CONNECT,
    /** @brief CONNECT TO DEFAULT. */
    TRL_AGENT_CONNECT_DEFAULT,
    /** @brief SET CONNECTION a name, or DEFAULT. */
    TRL_AGENT_SET,
    /** @brief DISCONNECT its object. */
    TRL_AGENT_DISCONNECT,
} trl_agent_verb;

/** @brief What DISCONNECT ends. */
typedef enum trl_agent_object {
    /** @brief The connection a name names. */
    TRL_AGENT_NAMED = 0,
    /** @brief DEFAULT. */
    TRL_AGENT_DEFAULT,
    /** @brief ALL. */
    TRL_AGENT_ALL,
    /** @brief CURRENT. */
    TRL_AGENT_CURRENT,
} trl_agent_object;

/** @brief What a <simple value specification> of a statement is. */
typedef enum trl_agent_value {
    /** @brief Not written. */
    TRL_VALUE_ABSENT = 0,
    /** @brief A <character string literal>, its value kept. */
    TRL_VALUE_TEXT,
    /** @brief A literal of another type, or a parameter: no name. */
    TRL_VALUE_OTHER,
} trl_agent_value;

/** @brief A value of a statement: its kind and, of a literal, its characters, null-ended. */
typedef struct trl_agent_text {
    /** @brief Its kind. */
    trl_agent_value kind;
    /** @brief Its characters. */
    trl_buffer text;
} trl_agent_text;

/** @brief A statement read. */
typedef struct trl_agent_statement {
    /** @brief Which. */
    trl_agent_verb verb;
    /** @brief DISCONNECT's object; SET CONNECTION's, DEFAULT or NAMED. */
    trl_agent_object object;
    /** @brief CONNECT's server. */
    trl_agent_text server;
    /** @brief CONNECT's AS, SET CONNECTION's or DISCONNECT's name. */
    trl_agent_text name;
    /** @brief CONNECT's USER. */
    trl_agent_text user;
} trl_agent_statement;

/** @brief Makes `statement` empty. */
void trl_agent_statement_init(trl_agent_statement *statement);

/** @brief Frees what `statement` holds. */
void trl_agent_statement_free(trl_agent_statement *statement);

/**
 * @brief Reads `sql` into `statement`: TRL_AGENT_OTHER unless it begins
 * CONNECT, DISCONNECT or SET CONNECTION; one that begins so and is none
 * of them a failure set in `failure` and false.
 */
bool trl_agent_statement_read(const char *sql, trl_agent_statement *statement, trl_failure *failure);

#endif
