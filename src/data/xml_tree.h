/**
 * @file xml_tree.h
 * @brief An XML tree's nodes, as the parser makes them.
 */
#ifndef TRL_DATA_XML_TREE_H
#define TRL_DATA_XML_TREE_H

#include "codec/arena.h"
#include "failure.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>

/** @brief How deep elements nest at most, as turborel's own trees do. */
#define TRL_XML_MAX_NESTING 512U
/** @brief How many nodes and namespace bindings a tree has at most. */
#define TRL_XML_MAX_NODES 1048576U
/** @brief How many octets of names and text a tree holds at most. */
#define TRL_XML_MAX_TEXT 16777216U

/** @brief A node: its kind, names and value, and those it holds. */
struct trl_xml_node {
    /** @brief Its kind. */
    trl_xml_kind kind;
    /** @brief Its qualified name, as written; null of a node of no name. */
    const char *qualified;
    /** @brief Its local name, or a processing instruction's target. */
    const char *name;
    /** @brief Its prefix, or null. */
    const char *prefix;
    /** @brief Its namespace URI, or null. */
    const char *uri;
    /** @brief Its value, or null. */
    const char *value;
    /** @brief The node it is a child or an attribute of. */
    trl_xml_node *parent;
    /** @brief Its first and last children. */
    trl_xml_node *first;
    /** @brief Its last child. */
    trl_xml_node *last;
    /** @brief How many children. */
    size_t child_count;
    /** @brief Its first and last attributes. */
    trl_xml_node *first_attribute;
    /** @brief Its last attribute. */
    trl_xml_node *last_attribute;
    /** @brief How many attributes. */
    size_t attribute_count;
    /** @brief The node after it among its parent's children or attributes. */
    trl_xml_node *next;
};

/** @brief A tree: its arena, its document node, and its failure. */
struct trl_xml {
    /** @brief What its nodes and their strings live in. */
    trl_arena arena;
    /** @brief Its document node, null if it failed. */
    trl_xml_node *root;
    /** @brief Its failure. */
    trl_failure failure;
};

/** @brief Parses `length` bytes of `text` into `xml`, setting its failure if they are not well-formed content. */
bool trl_xml_build(trl_xml *xml, const char *text, size_t length);

#endif
