/**
 * @file xml_parser.h
 * @brief The XML parser's state, and its lexical pieces: places, names,
 * references, characters.
 */
#ifndef TRL_DATA_XML_PARSER_H
#define TRL_DATA_XML_PARSER_H

#include "codec/buffer.h"
#include "data/xml_tree.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** A namespace binding in scope: a prefix, empty of the default, its URI,
   and the element depth it was declared at. */
typedef struct binding {
    const char *prefix;
    const char *uri;
    size_t depth;
    struct binding *previous;
} binding;

/** @brief The version an XML declaration names. */
typedef enum trl_xml_version_named {
    /** @brief XML 1.0, or no declaration, or one naming another 1.x. */
    TRL_XML_VERSION_10 = 0,
    /** @brief XML 1.1. */
    TRL_XML_VERSION_11,
    /** @brief A declaration whose version is no VersionNum. */
    TRL_XML_VERSION_NONE,
} trl_xml_version_named;

/** @brief The parser's state. */
typedef struct parser {
    const char *text;
    size_t length;
    size_t at;
    trl_xml *xml;
    trl_xml_node *current;
    size_t depth;
    binding *scope;
    trl_buffer pending;
    size_t nodes;
    size_t held;
    bool v11;
    bool failed;
} parser;

/** @brief Fails: the string is not well-formed XML content, and why (SQLSTATE 2200N). */
bool trl_xml_invalid(parser *p, const char *why);

/** @brief Fails: the value is past turborel's limits, and why (SQLCODE -900, SQLSTATE 0A000). */
bool trl_xml_beyond(parser *p, const char *why);

/** @brief Counts a node or a binding made; false, failed, past the most a tree has. */
bool trl_xml_counted(parser *p);

/** @brief Fails as memory not had. */
bool trl_xml_no_memory(parser *p);

/** @brief Whether the text at the parser's place begins with `prefix`. */
bool trl_xml_at(const parser *p, const char *prefix);

/** @brief Whether `c` is XML's white space. */
bool trl_xml_is_space(char c);

/** @brief Passes over white space; whether there was any. */
bool trl_xml_skip_space(parser *p);

/** @brief A copy of `length` bytes in the tree's arena, null-ended; null if memory cannot be had. */
char *trl_xml_keep(parser *p, const char *bytes, size_t length);

/** @brief Reads a reference after its "&", its replacement appended to `out`. */
bool trl_xml_reference(parser *p, trl_buffer *out);

/** @brief Reads a name; null if there is none. */
const char *trl_xml_read_name(parser *p);

/** @brief The version the XML declaration `text` begins with names, if any. */
trl_xml_version_named trl_xml_version(const char *text, size_t length);

/** @brief Appends `text` to `out`, each line end made LF, of XML 1.1 if `v11` [XML \xc2\xa72.11]. */
void trl_xml_normalize(const char *text, size_t length, bool v11, trl_buffer *out);

/** @brief Whether every character of the parser's text may be written as it is; else it fails. */
bool trl_xml_chars_allowed(parser *p);

#endif
