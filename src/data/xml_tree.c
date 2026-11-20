/**
 * @file xml_tree.c
 * @brief XML trees [CLIENT XML values]: parsed from a serialization, and
 * read node by node.
 */
#include "data/xml_tree.h"

#include "codec/arena.h"
#include "codec/tags.h"
#include "codec/values.h"
#include "failure.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

trl_status trl_xml_parse(const char *text, size_t length, trl_xml **out) {
    if (out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    *out = NULL;
    if (text == NULL && length > 0U) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_xml *xml = trl_calloc(1U, sizeof *xml);
    if (xml == NULL) {
        return TRL_NO_MEMORY;
    }
    trl_arena_init(&xml->arena);
    trl_failure_init(&xml->failure);
    *out = xml;
    return trl_xml_build(xml, text == NULL ? "" : text, length) ? TRL_OK : TRL_FAILED;
}

trl_status trl_value_xml(const trl_value *value, trl_xml **out) {
    if (value == NULL || value->tag != TRL_TAG_XML) {
        if (out != NULL) {
            *out = NULL;
        }
        return TRL_INVALID_ARGUMENT;
    }
    return trl_xml_parse((const char *)value->as.bytes.bytes, value->as.bytes.length, out);
}

const trl_failure *trl_xml_failure(const trl_xml *xml) {
    return xml == NULL ? NULL : &xml->failure;
}

const trl_xml_node *trl_xml_root(const trl_xml *xml) {
    return xml == NULL ? NULL : xml->root;
}

trl_xml_kind trl_xml_node_kind(const trl_xml_node *node) {
    return node == NULL ? TRL_XML_NODE_DOCUMENT : node->kind;
}

const char *trl_xml_node_name(const trl_xml_node *node) {
    return node == NULL ? NULL : node->name;
}

const char *trl_xml_node_namespace(const trl_xml_node *node) {
    return node == NULL ? NULL : node->uri;
}

const char *trl_xml_node_prefix(const trl_xml_node *node) {
    return node == NULL ? NULL : node->prefix;
}

const char *trl_xml_node_value(const trl_xml_node *node) {
    return node == NULL ? NULL : node->value;
}

size_t trl_xml_node_child_count(const trl_xml_node *node) {
    return node == NULL ? 0U : node->child_count;
}

const trl_xml_node *trl_xml_node_child(const trl_xml_node *node, size_t index) {
    const trl_xml_node *child = node == NULL ? NULL : node->first;
    for (size_t i = 0U; child != NULL && i < index; ++i) {
        child = child->next;
    }
    return child;
}

size_t trl_xml_node_attribute_count(const trl_xml_node *node) {
    return node == NULL ? 0U : node->attribute_count;
}

const trl_xml_node *trl_xml_node_attribute(const trl_xml_node *node, size_t index) {
    const trl_xml_node *attribute = node == NULL ? NULL : node->first_attribute;
    for (size_t i = 0U; attribute != NULL && i < index; ++i) {
        attribute = attribute->next;
    }
    return attribute;
}

void trl_xml_free(trl_xml *xml) {
    if (xml == NULL) {
        return;
    }
    trl_arena_free(&xml->arena);
    trl_failure_clear(&xml->failure);
    free(xml);
}
