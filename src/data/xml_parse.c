/**
 * @file xml_parse.c
 * @brief Well-formed XML content parsed into a tree [CLIENT XML values]:
 * the XML 1.0 constructs a serialization holds, its elements, attributes,
 * text, CDATA sections, comments and processing instructions, references
 * replaced and namespaces resolved. A document type declaration is not
 * read (RULES.md D8).
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/utf8.h"
#include "data/xml_parser.h"
#include "data/xml_tree.h"
#include "failure.h"
#include "format.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The namespace the prefix xml is bound to, and the one xmlns names. */
#define TRL_XML_NAMESPACE "http://www.w3.org/XML/1998/namespace"
#define TRL_XMLNS_NAMESPACE "http://www.w3.org/2000/xmlns/"

/* A new node of `kind`; null if memory cannot be had. */
static trl_xml_node *node_of(parser *p, trl_xml_kind kind) {
    if (!trl_xml_counted(p)) {
        return NULL;
    }
    trl_xml_node *node = trl_arena_alloc(&p->xml->arena, 1U, sizeof *node);
    if (node == NULL) {
        (void)trl_xml_no_memory(p);
        return NULL;
    }
    memset(node, 0, sizeof *node);
    node->kind = kind;
    return node;
}

/* Makes `child` the last child of `parent`. */
static void append_child(trl_xml_node *parent, trl_xml_node *child) {
    child->parent = parent;
    if (parent->last == NULL) {
        parent->first = child;
    } else {
        parent->last->next = child;
    }
    parent->last = child;
    ++parent->child_count;
}

/* Makes the pending text, if any, a text node of the current node. */
static bool flush_text(parser *p) {
    if (p->pending.state != TRL_BUFFER_OK) {
        return trl_xml_no_memory(p);
    }
    if (p->pending.length == 0U) {
        return true;
    }
    trl_xml_node *text = node_of(p, TRL_XML_NODE_TEXT);
    if (text == NULL) {
        return false;
    }
    text->value = trl_xml_keep(p, (const char *)p->pending.data, p->pending.length);
    if (text->value == NULL) {
        return false;
    }
    append_child(p->current, text);
    trl_buffer_free(&p->pending);
    return true;
}

/* Splits a qualified name into prefix and local part [Namespaces §4 [7]]. */
static bool split_name(parser *p, trl_xml_node *node) {
    const char *colon = strchr(node->qualified, ':');
    if (colon == NULL) {
        node->name = node->qualified;
        return true;
    }
    if (colon == node->qualified || colon[1] == '\0' || strchr(colon + 1, ':') != NULL) {
        return trl_xml_invalid(p, "a name is no qualified name [Namespaces \xc2\xa7"
                                  "4 [7]]");
    }
    node->prefix = trl_xml_keep(p, node->qualified, (size_t)(colon - node->qualified));
    node->name = colon + 1;
    return node->prefix != NULL;
}

/* The URI `prefix` is bound to, empty of the default; null if unbound. */
static const char *bound_to(const parser *p, const char *prefix) {
    if (strcmp(prefix, "xml") == 0) {
        return TRL_XML_NAMESPACE;
    }
    for (const binding *b = p->scope; b != NULL; b = b->previous) {
        if (strcmp(b->prefix, prefix) == 0) {
            return b->uri;
        }
    }
    return prefix[0] == '\0' ? "" : NULL;
}

/* Reads an attribute's value to its closing quote, or the text's end. */
static bool value_chars(parser *p, char quote, trl_buffer *value) {
    while (p->at < p->length && p->text[p->at] != quote) {
        char c = p->text[p->at];
        if (c == '<') {
            return trl_xml_invalid(p, "an attribute's value holds < [XML \xc2\xa7"
                                      "3.1 WFC: No < in Attribute Values]");
        }
        if (c == '&') {
            if (!trl_xml_reference(p, value)) {
                return false;
            }
            continue;
        }
        trl_put_u8(value, trl_xml_is_space(c) ? (uint8_t)' ' : (uint8_t)c);
        ++p->at;
    }
    return true;
}

/* Reads a quoted value after "=", its references replaced and its white
   space each a space [XML §3.3.3]. */
static const char *attribute_value(parser *p) {
    if (p->at == p->length || (p->text[p->at] != '"' && p->text[p->at] != '\'')) {
        (void)trl_xml_invalid(p, "an attribute's value is not quoted [XML \xc2\xa7"
                                 "3.1 [10]]");
        return NULL;
    }
    char quote = p->text[p->at++];
    trl_buffer value;
    trl_buffer_init(&value);
    bool read = value_chars(p, quote, &value);
    if (read && value.state != TRL_BUFFER_OK) {
        read = trl_xml_no_memory(p);
    }
    if (read && p->at == p->length) {
        read = trl_xml_invalid(p, "an attribute's value is not closed [XML \xc2\xa7"
                                  "3.1 [10]]");
    }
    const char *kept = NULL;
    if (read) {
        kept = trl_xml_keep(p, value.length == 0U ? "" : (const char *)value.data, value.length);
        ++p->at;
    }
    trl_buffer_free(&value);
    return kept;
}

/* Binds a prefix of a namespace declaration [Namespaces §3]. */
static bool declare(parser *p, const char *prefix, const char *uri) {
    bool reserved = strcmp(prefix, "xmlns") == 0 || strcmp(uri, TRL_XMLNS_NAMESPACE) == 0 ||
                    (strcmp(prefix, "xml") == 0) != (strcmp(uri, TRL_XML_NAMESPACE) == 0);
    if (reserved) {
        return trl_xml_invalid(p, "a reserved prefix or namespace is declared [Namespaces \xc2\xa7"
                                  "3]");
    }
    if (prefix[0] != '\0' && uri[0] == '\0') {
        return trl_xml_invalid(p, "a prefix is declared of no namespace [Namespaces \xc2\xa7"
                                  "3 NSC: No Prefix Undeclaring]");
    }
    if (!trl_xml_counted(p)) {
        return false;
    }
    binding *made = trl_arena_alloc(&p->xml->arena, 1U, sizeof *made);
    if (made == NULL) {
        return trl_xml_no_memory(p);
    }
    made->prefix = prefix;
    made->uri = uri;
    made->depth = p->depth;
    made->previous = p->scope;
    p->scope = made;
    return true;
}

/* Reads one attribute of a start tag: a namespace declaration binds, any
   other is the element's. */
static bool read_attribute(parser *p, trl_xml_node *element) {
    trl_xml_node *attribute = node_of(p, TRL_XML_NODE_ATTRIBUTE);
    if (attribute == NULL) {
        return false;
    }
    attribute->qualified = trl_xml_read_name(p);
    if (attribute->qualified == NULL) {
        return false;
    }
    (void)trl_xml_skip_space(p);
    if (!trl_xml_at(p, "=")) {
        return trl_xml_invalid(p, "an attribute has no value [XML \xc2\xa7"
                                  "3.1 [41]]");
    }
    ++p->at;
    (void)trl_xml_skip_space(p);
    attribute->value = attribute_value(p);
    if (attribute->value == NULL) {
        return false;
    }
    if (strcmp(attribute->qualified, "xmlns") == 0) {
        return declare(p, "", attribute->value);
    }
    if (strncmp(attribute->qualified, "xmlns:", strlen("xmlns:")) == 0) {
        return declare(p, attribute->qualified + strlen("xmlns:"), attribute->value);
    }
    attribute->parent = element;
    if (element->last_attribute == NULL) {
        element->first_attribute = attribute;
    } else {
        element->last_attribute->next = attribute;
    }
    element->last_attribute = attribute;
    ++element->attribute_count;
    return true;
}

/* Whether two attributes have one expanded name. */
static bool same_name(const trl_xml_node *a, const trl_xml_node *b) {
    bool same_uri = a->uri == NULL ? b->uri == NULL : b->uri != NULL && strcmp(a->uri, b->uri) == 0;
    return same_uri && strcmp(a->name, b->name) == 0;
}

/* Resolves an attribute's prefix, and refuses it if an attribute before it
   has its expanded name [Namespaces \xc2\xa76.3]. */
static bool resolve_attribute(parser *p, const trl_xml_node *element, trl_xml_node *attribute) {
    if (!split_name(p, attribute)) {
        return false;
    }
    const char *bound = attribute->prefix == NULL ? "" : bound_to(p, attribute->prefix);
    if (bound == NULL) {
        return trl_xml_invalid(p, "a prefix is not bound [Namespaces \xc2\xa7"
                                  "5 NSC: Prefix Declared]");
    }
    attribute->uri = bound[0] == '\0' ? NULL : bound;
    for (const trl_xml_node *b = element->first_attribute; b != attribute; b = b->next) {
        if (same_name(attribute, b)) {
            return trl_xml_invalid(p, "an attribute is given twice [XML \xc2\xa7"
                                      "3.1 WFC: Unique Att Spec]");
        }
    }
    return true;
}

/* Resolves the element's and its attributes' prefixes. */
static bool resolve(parser *p, trl_xml_node *element) {
    if (!split_name(p, element)) {
        return false;
    }
    const char *uri = bound_to(p, element->prefix == NULL ? "" : element->prefix);
    if (uri == NULL) {
        return trl_xml_invalid(p, "a prefix is not bound [Namespaces \xc2\xa7"
                                  "5 NSC: Prefix Declared]");
    }
    element->uri = uri[0] == '\0' ? NULL : uri;
    for (trl_xml_node *a = element->first_attribute; a != NULL; a = a->next) {
        if (!resolve_attribute(p, element, a)) {
            return false;
        }
    }
    return true;
}

/* Ends the scope of the namespace declarations of an element ended. */
static void close_scope(parser *p) {
    --p->depth;
    while (p->scope != NULL && p->scope->depth > p->depth) {
        p->scope = p->scope->previous;
    }
}

/* Reads a start tag's attributes, to its "/>" or ">". */
static bool read_attributes(parser *p, trl_xml_node *element) {
    for (;;) {
        bool spaced = trl_xml_skip_space(p);
        if (trl_xml_at(p, "/>") || trl_xml_at(p, ">")) {
            return true;
        }
        if (!spaced) {
            return trl_xml_invalid(p, "a start tag is malformed [XML \xc2\xa7"
                                      "3.1 [40]]");
        }
        if (!read_attribute(p, element)) {
            return false;
        }
    }
}

/* Reads a start tag after "<", and opens its element unless it is empty. */
static bool start_tag(parser *p) {
    trl_xml_node *element = node_of(p, TRL_XML_NODE_ELEMENT);
    if (element == NULL || !flush_text(p)) {
        return false;
    }
    p->at += strlen("<");
    if (p->depth == TRL_XML_MAX_NESTING) {
        char why[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(why, sizeof why, "its elements nest deeper than %lu", (unsigned long)TRL_XML_MAX_NESTING);
        return trl_xml_beyond(p, why);
    }
    ++p->depth;
    element->qualified = trl_xml_read_name(p);
    if (element->qualified == NULL || !read_attributes(p, element) || !resolve(p, element)) {
        return false;
    }
    append_child(p->current, element);
    if (trl_xml_at(p, "/>")) {
        p->at += strlen("/>");
        close_scope(p);
        return true;
    }
    p->at += strlen(">");
    p->current = element;
    return true;
}

/* Reads an end tag after "</", closing the element it names. */
static bool end_tag(parser *p) {
    if (!flush_text(p)) {
        return false;
    }
    p->at += strlen("</");
    const char *name = trl_xml_read_name(p);
    if (name == NULL) {
        return false;
    }
    (void)trl_xml_skip_space(p);
    if (!trl_xml_at(p, ">")) {
        return trl_xml_invalid(p, "an end tag is malformed [XML \xc2\xa7"
                                  "3.1 [42]]");
    }
    ++p->at;
    if (p->current->kind != TRL_XML_NODE_ELEMENT || strcmp(name, p->current->qualified) != 0) {
        return trl_xml_invalid(p, "an end tag does not name the element it ends [XML \xc2\xa7"
                                  "3 WFC: Element Type Match]");
    }
    close_scope(p);
    p->current = p->current->parent;
    return true;
}

/* Reads to `end`, from the parser's place; the bytes before it, kept. */
static const char *until(parser *p, const char *end, const char *why) {
    size_t began = p->at;
    size_t length = strlen(end);
    while (p->at < p->length && !trl_xml_at(p, end)) {
        ++p->at;
    }
    if (p->at == p->length) {
        (void)trl_xml_invalid(p, why);
        return NULL;
    }
    const char *kept = trl_xml_keep(p, p->text + began, p->at - began);
    p->at += length;
    return kept;
}

/* Reads a comment after "<!--" [XML §2.5 [15]]. */
static bool comment(parser *p) {
    if (!flush_text(p)) {
        return false;
    }
    p->at += strlen("<!--");
    const char *text = until(p, "--",
                             "a comment is not closed [XML \xc2\xa7"
                             "2.5 [15]]");
    if (text == NULL) {
        return false;
    }
    if (!trl_xml_at(p, ">")) {
        return trl_xml_invalid(p, "a comment holds -- [XML \xc2\xa7"
                                  "2.5 [15]]");
    }
    ++p->at;
    trl_xml_node *node = node_of(p, TRL_XML_NODE_COMMENT);
    if (node == NULL) {
        return false;
    }
    node->value = text;
    append_child(p->current, node);
    return true;
}

/* Whether a target is "xml" in any case, which XML reserves. */
static bool xml_named(const char *name) {
    static const char lower[] = "xml";
    static const char upper[] = "XML";
    for (size_t i = 0U; i < sizeof lower; ++i) {
        if (name[i] != lower[i] && name[i] != upper[i]) {
            return false;
        }
    }
    return true;
}

/* Reads a processing instruction after "<?" [XML §2.6 [16]]. */
static bool instruction(parser *p) {
    if (!flush_text(p)) {
        return false;
    }
    p->at += strlen("<?");
    trl_xml_node *node = node_of(p, TRL_XML_NODE_PROCESSING_INSTRUCTION);
    if (node == NULL) {
        return false;
    }
    node->name = trl_xml_read_name(p);
    if (node->name == NULL) {
        return false;
    }
    if (xml_named(node->name) || strchr(node->name, ':') != NULL) {
        return trl_xml_invalid(p, "a processing instruction's target is reserved [XML \xc2\xa7"
                                  "2.6 [17]]");
    }
    if (!trl_xml_skip_space(p) && !trl_xml_at(p, "?>")) {
        return trl_xml_invalid(p, "a processing instruction is malformed [XML \xc2\xa7"
                                  "2.6 [16]]");
    }
    node->qualified = node->name;
    node->value = until(p, "?>",
                        "a processing instruction is not closed [XML \xc2\xa7"
                        "2.6 [16]]");
    if (node->value == NULL) {
        return false;
    }
    append_child(p->current, node);
    return true;
}

/* Reads a CDATA section after "<![CDATA[" into the pending text. */
static bool cdata(parser *p) {
    p->at += strlen("<![CDATA[");
    size_t began = p->at;
    while (p->at < p->length && !trl_xml_at(p, "]]>")) {
        ++p->at;
    }
    if (p->at == p->length) {
        return trl_xml_invalid(p, "a CDATA section is not closed [XML \xc2\xa7"
                                  "2.7 [18]]");
    }
    trl_put_raw(&p->pending, (const uint8_t *)p->text + began, p->at - began);
    p->at += strlen("]]>");
    return true;
}

/* Reads character data into the pending text, its line ends normalized. */
static bool char_data(parser *p) {
    while (p->at < p->length && p->text[p->at] != '<' && p->text[p->at] != '&') {
        if (trl_xml_at(p, "]]>")) {
            return trl_xml_invalid(p, "character data holds ]]> [XML \xc2\xa7"
                                      "2.4 [14]]");
        }
        trl_put_u8(&p->pending, (uint8_t)p->text[p->at]);
        ++p->at;
    }
    return true;
}

/* Reads one construct of content. */
static bool construct(parser *p) {
    if (trl_xml_at(p, "<!--")) {
        return comment(p);
    }
    if (trl_xml_at(p, "<![CDATA[")) {
        return cdata(p);
    }
    if (trl_xml_at(p, "<!")) {
        return trl_xml_invalid(p, "a document type declaration is not read by this driver");
    }
    if (trl_xml_at(p, "<?")) {
        return instruction(p);
    }
    if (trl_xml_at(p, "</")) {
        return end_tag(p);
    }
    if (trl_xml_at(p, "<")) {
        return start_tag(p);
    }
    if (trl_xml_at(p, "&")) {
        return trl_xml_reference(p, &p->pending);
    }
    return char_data(p);
}

/* Passes over an XML declaration, as `<?xml version="1.0" ...?>`
   [XML §2.8 [23]]. */
static bool declaration(parser *p) {
    if (!trl_xml_at(p, "<?xml") || p->length - p->at < strlen("<?xml ") ||
        !trl_xml_is_space(p->text[p->at + strlen("<?xml")])) {
        return true;
    }
    p->at += strlen("<?xml");
    (void)trl_xml_skip_space(p);
    if (!trl_xml_at(p, "version")) {
        return trl_xml_invalid(p, "an XML declaration names no version [XML \xc2\xa7"
                                  "2.8 [23]]");
    }
    if (trl_xml_version(p->text, p->length) == TRL_XML_VERSION_NONE) {
        return trl_xml_invalid(p, "an XML declaration's version is no VersionNum [XML \xc2\xa7"
                                  "2.8 [26]]");
    }
    return until(p, "?>",
                 "an XML declaration is not closed [XML \xc2\xa7"
                 "2.8 [23]]") != NULL;
}

bool trl_xml_build(trl_xml *xml, const char *text, size_t length) {
    parser p;
    memset(&p, 0, sizeof p);
    p.xml = xml;
    trl_buffer_init(&p.pending);
    trl_buffer normalized;
    trl_buffer_init(&normalized);
    p.v11 = trl_xml_version(text, length) == TRL_XML_VERSION_11;
    trl_xml_normalize(text, length, p.v11, &normalized);
    p.text = (const char *)normalized.data;
    p.length = normalized.length;
    trl_xml_node *document = node_of(&p, TRL_XML_NODE_DOCUMENT);
    p.current = document;
    bool read = document != NULL;
    if (read && normalized.state != TRL_BUFFER_OK) {
        read = trl_xml_no_memory(&p);
    }
    if (read && !trl_utf8_valid((const uint8_t *)p.text, p.length)) {
        read = trl_xml_invalid(&p, "the string is not UTF-8");
    }
    read = read && (p.length == 0U || (trl_xml_chars_allowed(&p) && declaration(&p)));
    while (read && p.at < p.length) {
        read = construct(&p);
    }
    read = read && flush_text(&p);
    if (read && p.current != document) {
        read = trl_xml_invalid(&p, "an element is not ended [XML \xc2\xa7"
                                   "3 [39]]");
    }
    trl_buffer_free(&p.pending);
    trl_buffer_free(&normalized);
    xml->root = read ? document : NULL;
    return read;
}
