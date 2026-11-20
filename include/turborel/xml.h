/**
 * @file xml.h
 * @brief XML values' nodes [CLIENT XML values]: a serialization, as an
 * XML value carries it, parsed as well-formed XML content into a tree of
 * a document node, elements, attributes, text, comments and processing
 * instructions, namespaces resolved.
 *
 * A program includes turborel/turborel.h, which includes this.
 */
#ifndef TRL_XML_H
#define TRL_XML_H

#include "turborel/base.h"
#include "turborel/failure.h"
#include "turborel/values.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A tree of nodes parsed from a serialization. */
typedef struct trl_xml trl_xml;

/** @brief A node of a tree; read only through its functions. */
typedef struct trl_xml_node trl_xml_node;

/** @brief The kinds of node. */
typedef enum trl_xml_kind {
    /** @brief The document node, the tree's root. */
    TRL_XML_NODE_DOCUMENT = 0,
    /** @brief An element. */
    TRL_XML_NODE_ELEMENT,
    /** @brief An attribute of an element. */
    TRL_XML_NODE_ATTRIBUTE,
    /** @brief Text, CDATA sections and references among it. */
    TRL_XML_NODE_TEXT,
    /** @brief A comment. */
    TRL_XML_NODE_COMMENT,
    /** @brief A processing instruction. */
    TRL_XML_NODE_PROCESSING_INSTRUCTION,
} trl_xml_kind;

/**
 * @brief Parses a serialization, UTF-8, as well-formed XML content: an XML
 * declaration, if any, then elements, text, comments and processing
 * instructions, in any order; line ends normalized, entity and character
 * references replaced, every prefix bound.
 *
 * @param text The serialization.
 * @param length Its bytes.
 * @param out Where the tree goes. On TRL_FAILED it is made, holding the
 * failure (SQLCODE -301, SQLSTATE 2200N); free it either way.
 * @return TRL_OK; TRL_FAILED; TRL_INVALID_ARGUMENT of a null pointer;
 * TRL_NO_MEMORY, with nothing made.
 */
TRL_API trl_status trl_xml_parse(const char *text, size_t length, trl_xml **out);

/**
 * @brief Parses an XML value's serialization, as trl_xml_parse() does.
 *
 * @param value An XML value.
 * @param out Where the tree goes, as trl_xml_parse() gives it.
 * @return As trl_xml_parse() returns; TRL_INVALID_ARGUMENT of a value of
 * another kind.
 */
TRL_API trl_status trl_value_xml(const trl_value *value, trl_xml **out);

/**
 * @brief The tree's last failure.
 *
 * @param xml The tree.
 * @return The failure, owned by the tree; null if `xml` is null.
 */
TRL_API const trl_failure *trl_xml_failure(const trl_xml *xml);

/**
 * @brief The tree's document node.
 *
 * @param xml The tree.
 * @return The node, owned by the tree; null if it failed or is null.
 */
TRL_API const trl_xml_node *trl_xml_root(const trl_xml *xml);

/**
 * @brief A node's kind.
 *
 * @param node The node; null gives TRL_XML_NODE_DOCUMENT.
 * @return Its kind.
 */
TRL_API trl_xml_kind trl_xml_node_kind(const trl_xml_node *node);

/**
 * @brief An element's or attribute's local name, or a processing
 * instruction's target.
 *
 * @param node The node.
 * @return The name, UTF-8, null-ended, owned by the tree; null of a node
 * of no name.
 */
TRL_API const char *trl_xml_node_name(const trl_xml_node *node);

/**
 * @brief An element's or attribute's namespace URI, or null if it is in
 * none.
 *
 * @param node The node.
 * @return The URI, owned by the tree, or null.
 */
TRL_API const char *trl_xml_node_namespace(const trl_xml_node *node);

/**
 * @brief An element's or attribute's prefix, or null if it has none.
 *
 * @param node The node.
 * @return The prefix, owned by the tree, or null.
 */
TRL_API const char *trl_xml_node_prefix(const trl_xml_node *node);

/**
 * @brief A text's, comment's, processing instruction's or attribute's
 * value.
 *
 * @param node The node.
 * @return The value, UTF-8, null-ended, owned by the tree; null of an
 * element or the document node.
 */
TRL_API const char *trl_xml_node_value(const trl_xml_node *node);

/**
 * @brief How many children the document node or an element has.
 *
 * @param node The node; another kind gives 0.
 * @return The count.
 */
TRL_API size_t trl_xml_node_child_count(const trl_xml_node *node);

/**
 * @brief A child, in document order.
 *
 * @param node The node.
 * @param index Which, from 0.
 * @return The child, owned by the tree; null past the last.
 */
TRL_API const trl_xml_node *trl_xml_node_child(const trl_xml_node *node, size_t index);

/**
 * @brief How many attributes an element has, namespace declarations not
 * among them.
 *
 * @param node The node; another kind gives 0.
 * @return The count.
 */
TRL_API size_t trl_xml_node_attribute_count(const trl_xml_node *node);

/**
 * @brief An attribute, in the order written.
 *
 * @param node The element.
 * @param index Which, from 0.
 * @return The attribute, owned by the tree; null past the last.
 */
TRL_API const trl_xml_node *trl_xml_node_attribute(const trl_xml_node *node, size_t index);

/**
 * @brief Frees a tree, and every node of it.
 *
 * @param xml The tree; null does nothing.
 */
TRL_API void trl_xml_free(trl_xml *xml);

#ifdef __cplusplus
}
#endif

#endif
