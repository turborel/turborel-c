/**
 * @file xml_test.c
 * @brief XML values' nodes parsed from their serialization
 * [CLIENT XML values]: elements, attributes, text, comments and processing
 * instructions, references replaced, namespaces resolved, and what is not
 * well-formed content refused (SQLSTATE 2200N).
 */
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Writes `text`, its null not among it, at `to`; the bytes written. */
static size_t put(char *to, const char *text) {
    size_t length = strlen(text);
    for (size_t i = 0U; i < length; ++i) {
        to[i] = text[i];
    }
    return length;
}

/* Parses `text`; the tree, made either way. */
static trl_xml *parsed(const char *text, trl_status *status) {
    trl_xml *xml = NULL;
    *status = trl_xml_parse(text, strlen(text), &xml);
    return xml;
}

/* Whether `text` is refused as not well-formed content, its failure saying `why`. */
static bool refused(const char *text, const char *why) {
    trl_status status = TRL_OK;
    trl_xml *xml = parsed(text, &status);
    const trl_failure *failure = trl_xml_failure(xml);
    bool is = status == TRL_FAILED && trl_xml_root(xml) == NULL && trl_failure_sqlcode(failure) == -301 &&
              strcmp(trl_failure_sqlstate(failure), "2200N") == 0 && strstr(trl_failure_message(failure), why) != NULL;
    trl_xml_free(xml);
    return is;
}

/**
 * Verifies: [CLIENT XML values] a serialization's nodes: an element, its
 * attributes in the order written, its children in document order, text
 * with its references replaced, a CDATA section among it, a comment and a
 * processing instruction.
 */
static void nodes_are_parsed_from_text(trl_test *test) {
    trl_status status = TRL_FAILED;
    trl_xml *xml =
        parsed("<?xml version=\"1.0\"?><order no=\"18\" kind='a&amp;b'><item>jam &lt;&#65;&#x42;<![CDATA[<c>]]></item>"
               "<!-- note --><?pi data?></order>tail",
               &status);
    TRL_CHECK(test, status == TRL_OK);
    const trl_xml_node *root = trl_xml_root(xml);
    TRL_CHECK(test, trl_xml_node_kind(root) == TRL_XML_NODE_DOCUMENT && trl_xml_node_child_count(root) == 2U);
    const trl_xml_node *order = trl_xml_node_child(root, 0U);
    TRL_CHECK(test, trl_xml_node_kind(order) == TRL_XML_NODE_ELEMENT && strcmp(trl_xml_node_name(order), "order") == 0);
    TRL_CHECK(test, trl_xml_node_namespace(order) == NULL && trl_xml_node_prefix(order) == NULL);
    TRL_CHECK(test, trl_xml_node_attribute_count(order) == 2U && trl_xml_node_value(order) == NULL);
    const trl_xml_node *kind = trl_xml_node_attribute(order, 1U);
    TRL_CHECK(test, strcmp(trl_xml_node_name(kind), "kind") == 0 && strcmp(trl_xml_node_value(kind), "a&b") == 0);
    TRL_CHECK(test, trl_xml_node_attribute(order, 2U) == NULL && trl_xml_node_child_count(order) == 3U);
    const trl_xml_node *item = trl_xml_node_child(order, 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_value(trl_xml_node_child(item, 0U)), "jam <AB<c>") == 0);
    const trl_xml_node *note = trl_xml_node_child(order, 1U);
    TRL_CHECK(test, trl_xml_node_kind(note) == TRL_XML_NODE_COMMENT && strcmp(trl_xml_node_value(note), " note ") == 0);
    const trl_xml_node *pi = trl_xml_node_child(order, 2U);
    TRL_CHECK(test, trl_xml_node_kind(pi) == TRL_XML_NODE_PROCESSING_INSTRUCTION);
    TRL_CHECK(test, strcmp(trl_xml_node_name(pi), "pi") == 0 && strcmp(trl_xml_node_value(pi), "data") == 0);
    TRL_CHECK(test, trl_xml_node_child(order, 3U) == NULL);
    const trl_xml_node *tail = trl_xml_node_child(root, 1U);
    TRL_CHECK(test, trl_xml_node_kind(tail) == TRL_XML_NODE_TEXT && strcmp(trl_xml_node_value(tail), "tail") == 0);
    trl_xml_free(xml);
}

/**
 * Verifies: [CLIENT XML values] a prefix resolves to the namespace its
 * nearest declaration binds, the default namespace to an element's, none
 * to an attribute's; xml is bound always; line ends and an attribute's
 * white space normalized.
 */
static void namespaces_are_resolved(trl_test *test) {
    trl_status status = TRL_FAILED;
    trl_xml *xml = parsed("<a xmlns=\"u\" xmlns:p=\"v\" p:x=\"1\" y=\"a\tb\"><p:b xml:lang=\"en\">x\r\ny</p:b>"
                          "<c xmlns=\"\"/></a>",
                          &status);
    TRL_CHECK(test, status == TRL_OK);
    const trl_xml_node *a = trl_xml_node_child(trl_xml_root(xml), 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_namespace(a), "u") == 0 && trl_xml_node_attribute_count(a) == 2U);
    const trl_xml_node *x = trl_xml_node_attribute(a, 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_namespace(x), "v") == 0 && strcmp(trl_xml_node_prefix(x), "p") == 0);
    const trl_xml_node *y = trl_xml_node_attribute(a, 1U);
    TRL_CHECK(test, trl_xml_node_namespace(y) == NULL && strcmp(trl_xml_node_value(y), "a b") == 0);
    const trl_xml_node *b = trl_xml_node_child(a, 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_namespace(b), "v") == 0 && strcmp(trl_xml_node_name(b), "b") == 0);
    TRL_CHECK(test, strcmp(trl_xml_node_namespace(trl_xml_node_attribute(b, 0U)),
                           "http://www.w3.org/XML/1998/namespace") == 0);
    TRL_CHECK(test, strcmp(trl_xml_node_value(trl_xml_node_child(b, 0U)), "x\ny") == 0);
    TRL_CHECK(test, trl_xml_node_namespace(trl_xml_node_child(a, 1U)) == NULL);
    trl_xml_free(xml);
}

/**
 * Verifies: [CLIENT XML values] what is not well-formed XML content is
 * refused (SQLCODE -301, SQLSTATE 2200N), saying why.
 */
static void malformed_content_is_refused(trl_test *test) {
    TRL_CHECK(test, refused("<a>", "not ended"));
    TRL_CHECK(test, refused("<a></b>", "does not name the element"));
    TRL_CHECK(test, refused("</a>", "does not name the element"));
    TRL_CHECK(test, refused("<a b=1/>", "not quoted"));
    TRL_CHECK(test, refused("<a b/>", "has no value"));
    TRL_CHECK(test, refused("<a b='<'/>", "holds <"));
    TRL_CHECK(test, refused("<a b='1' b='2'/>", "given twice"));
    TRL_CHECK(test, refused("<a b='1'c='2'/>", "malformed"));
    TRL_CHECK(test, refused("<p:a/>", "not bound"));
    TRL_CHECK(test, refused("<a p:b='1'/>", "not bound"));
    TRL_CHECK(test, refused("<a xmlns:p=''/>", "no namespace"));
    TRL_CHECK(test, refused("<a xmlns:xmlns='u'/>", "reserved"));
    TRL_CHECK(test, refused("<a:/>", "qualified name"));
    TRL_CHECK(test, refused("&nope;", "not declared"));
    TRL_CHECK(test, refused("&#0;", "no Char"));
    TRL_CHECK(test, refused("&#x110000;", "names no character"));
    TRL_CHECK(test, refused("&#;", "malformed"));
    TRL_CHECK(test, refused("a]]>b", "holds ]]>"));
    TRL_CHECK(test, refused("<!-- a -- b -->", "holds --"));
    TRL_CHECK(test, refused("<!-- open", "not closed"));
    TRL_CHECK(test, refused("<![CDATA[open", "not closed"));
    TRL_CHECK(test, refused("<?xml-stylesheet x?><?xml x?>", "reserved"));
    TRL_CHECK(test, refused("<?pi x", "not closed"));
    TRL_CHECK(test, refused("<?pi!?>", "malformed"));
    TRL_CHECK(test, refused("<!DOCTYPE a>", "document type declaration"));
    TRL_CHECK(test, refused("<?xml encoding='utf-8'?>", "no version"));
    TRL_CHECK(test, refused("<?xml version='1.0'", "not closed"));
    TRL_CHECK(test, refused("\x01", "no XML Char"));
    TRL_CHECK(test, refused("\xef\xbf\xbe", "no XML Char"));
    TRL_CHECK(test, refused("\xff", "not UTF-8"));
    TRL_CHECK(test, refused("< a/>", "a name was expected"));
    TRL_CHECK(test, refused("<a b='1", "not closed"));
    TRL_CHECK(test, refused("<a></a >x</a>", "does not name the element"));
}

/**
 * Verifies: [CLIENT XML values] character references of one to four
 * UTF-8 bytes, in decimal and in hexadecimal digits of either case; names of
 * letters past ASCII, digits, hyphens and full stops; the prefix xml
 * declared to its own namespace; an attribute's name in two namespaces;
 * text past a buffer's first size.
 */
static void references_names_and_long_text(trl_test *test) {
    trl_status status = TRL_FAILED;
    trl_xml *xml = parsed("<\xc3\xa9l-1.x xmlns:xml='http://www.w3.org/XML/1998/namespace' xmlns:p='u' xmlns:q='v' "
                          "p:a='1' q:a='2'>&#65;&#xe9;&#x20AC;&#x1F600;&#xAb;</\xc3\xa9l-1.x>",
                          &status);
    TRL_CHECK(test, status == TRL_OK);
    const trl_xml_node *element = trl_xml_node_child(trl_xml_root(xml), 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_name(element), "\xc3\xa9l-1.x") == 0);
    TRL_CHECK(test, trl_xml_node_attribute_count(element) == 2U);
    TRL_CHECK(test, strcmp(trl_xml_node_value(trl_xml_node_child(element, 0U)),
                           "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\xc2\xab") == 0);
    trl_xml_free(xml);
    char text[2048];
    (void)put(text, "<a>");
    memset(text + 3, 'x', 1500U);
    (void)put(text + 1503, "</a>");
    text[1507] = '\0';
    xml = parsed(text, &status);
    TRL_CHECK(test, status == TRL_OK &&
                        strlen(trl_xml_node_value(trl_xml_node_child(trl_xml_node_child(trl_xml_root(xml), 0U), 0U))) ==
                            1500U);
    trl_xml_free(xml);
    char attribute[2048];
    (void)put(attribute, "<a b='");
    memset(attribute + 6, 'y', 1500U);
    (void)put(attribute + 1506, "'/>");
    attribute[1509] = '\0';
    xml = parsed(attribute, &status);
    TRL_CHECK(test, status == TRL_OK);
    trl_xml_free(xml);
}

/**
 * Verifies: [CLIENT XML values] references to no XML Char, prefixes and
 * namespaces reserved, an attribute's expanded name given twice, and end
 * tags malformed, refused.
 */
static void more_malformed_content_is_refused(trl_test *test) {
    TRL_CHECK(test, refused("&#xD800;", "no Char"));
    TRL_CHECK(test, refused("&#xFFFE;", "no Char"));
    TRL_CHECK(test, refused("&#xFFFF;", "no Char"));
    TRL_CHECK(test, refused("&#65", "malformed"));
    TRL_CHECK(test, refused("&#x;", "malformed"));
    TRL_CHECK(test, refused("&#X41;", "malformed"));
    TRL_CHECK(test, refused("<a xmlns:xml='u'/>", "reserved"));
    TRL_CHECK(test, refused("<a xmlns:p='http://www.w3.org/XML/1998/namespace'/>", "reserved"));
    TRL_CHECK(test, refused("<a xmlns:p='http://www.w3.org/2000/xmlns/'/>", "reserved"));
    TRL_CHECK(test, refused("<a xmlns:p='u' xmlns:q='u' p:x='1' q:x='2'/>", "given twice"));
    TRL_CHECK(test, refused("<a></a x>", "malformed"));
    TRL_CHECK(test, refused("<a></>", "a name was expected"));
    TRL_CHECK(test, refused("<a:b:c/>", "qualified name"));
    TRL_CHECK(test, refused("<:a/>", "qualified name"));
    TRL_CHECK(test, refused("<a p:='1'/>", "qualified name"));
    TRL_CHECK(test, refused("<a b='&nope;'/>", "not declared"));
    TRL_CHECK(test, refused("<?XmL x?>", "reserved"));
    TRL_CHECK(test, refused("<?p:i x?>", "reserved"));
    TRL_CHECK(test, refused("<a", "malformed"));
}

/**
 * Verifies: [CLIENT XML values] a serialization in XML 1.1, after its
 * declaration: a control character by a reference, NEL and U+2028 as line
 * ends, a restricted character written as it is refused; a declaration of
 * no VersionNum refused.
 */
static void xml_1_1_is_read_by_its_rules(trl_test *test) {
    trl_status status = TRL_FAILED;
    trl_xml *xml = parsed("<?xml version=\"1.1\"?><a>&#x1;b\xc2\x85"
                          "c\r\xc2\x85"
                          "d\xe2\x80\xa8"
                          "e</a>",
                          &status);
    TRL_CHECK(test, status == TRL_OK);
    const trl_xml_node *a = trl_xml_node_child(trl_xml_root(xml), 0U);
    TRL_CHECK(test, strcmp(trl_xml_node_value(trl_xml_node_child(a, 0U)), "\x01"
                                                                          "b\nc\nd\ne") == 0);
    trl_xml_free(xml);
    xml = parsed("<a>\xc2\x85</a>", &status);
    TRL_CHECK(test, status == TRL_OK &&
                        strcmp(trl_xml_node_value(trl_xml_node_child(trl_xml_node_child(trl_xml_root(xml), 0U), 0U)),
                               "\xc2\x85") == 0);
    trl_xml_free(xml);
    TRL_CHECK(test, refused("&#x1;", "no Char"));
    TRL_CHECK(test, refused("<?xml version='1.1'?>\x7f", "restricted"));
    TRL_CHECK(test, refused("<?xml version='1.1'?>\xc2\x80", "restricted"));
    TRL_CHECK(test, refused("<?xml version='2.0'?>", "VersionNum"));
    TRL_CHECK(test, refused("<?xml version='1.x'?>", "VersionNum"));
    TRL_CHECK(test, refused("<?xml version '1.0'?>", "VersionNum"));
    TRL_CHECK(test, refused("<?xml version=1.0?>", "VersionNum"));
    TRL_CHECK(test, refused("<?xml version='1.0?>", "VersionNum"));
    xml = parsed("<?xml version='1.5'?><a/>", &status);
    TRL_CHECK(test, status == TRL_OK);
    trl_xml_free(xml);
}

/* Whether `text` is refused as past turborel's limits, SQLCODE -900. */
static bool beyond(const char *text, size_t length) {
    trl_xml *xml = NULL;
    trl_status status = trl_xml_parse(text, length, &xml);
    const trl_failure *failure = trl_xml_failure(xml);
    bool is = status == TRL_FAILED && trl_failure_sqlcode(failure) == -900 &&
              strcmp(trl_failure_sqlstate(failure), "0A000") == 0;
    trl_xml_free(xml);
    return is;
}

/**
 * Verifies: [CLIENT XML values] elements nested 512 deep read, deeper
 * refused, and more than 2^20 nodes or 16 MiB of text refused, as past
 * turborel's limits (SQLCODE -900, SQLSTATE 0A000), as Turborel's own
 * trees are.
 */
static void trees_past_the_limits_are_refused(trl_test *test) {
    static char deep[(513U * 7U) + 8U];
    size_t at = 0U;
    for (size_t i = 0U; i < 512U; ++i) {
        at += put(deep + at, "<a>");
    }
    for (size_t i = 0U; i < 512U; ++i) {
        at += put(deep + at, "</a>");
    }
    trl_xml *xml = NULL;
    TRL_CHECK(test, trl_xml_parse(deep, at, &xml) == TRL_OK);
    trl_xml_free(xml);
    memmove(deep + 3, deep, at);
    (void)put(deep, "<a>");
    (void)put(deep + at + 3U, "</a>");
    TRL_CHECK(test, beyond(deep, at + 7U));
    size_t many = (1U << 20U) + 1U;
    char *comments = malloc(many * 7U);
    TRL_CHECK(test, comments != NULL);
    if (comments != NULL) {
        for (size_t i = 0U; i < many; ++i) {
            (void)put(comments + (i * 7U), "<!---->");
        }
        TRL_CHECK(test, beyond(comments, many * 7U));
        free(comments);
    }
    size_t octets = (16U << 20U) + 1U;
    char *text = malloc(octets);
    TRL_CHECK(test, text != NULL);
    if (text != NULL) {
        memset(text, 'x', octets);
        TRL_CHECK(test, beyond(text, octets));
        free(text);
    }
}

/**
 * Verifies: [CLIENT XML values] an XML value's nodes parsed from it; a
 * value of another kind, or a null pointer, refused.
 */
static void a_values_nodes(trl_test *test) {
    trl_xml *xml = NULL;
    TRL_CHECK(test, trl_xml_parse("", 0U, &xml) == TRL_OK && trl_xml_node_child_count(trl_xml_root(xml)) == 0U);
    trl_xml_free(xml);
    TRL_CHECK(test, trl_xml_parse(NULL, 0U, &xml) == TRL_OK);
    trl_xml_free(xml);
    TRL_CHECK(test, trl_xml_parse(NULL, 1U, &xml) == TRL_INVALID_ARGUMENT && xml == NULL);
    TRL_CHECK(test, trl_xml_parse("x", 1U, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_value_xml(NULL, &xml) == TRL_INVALID_ARGUMENT && xml == NULL);
    TRL_CHECK(test, trl_xml_failure(NULL) == NULL && trl_xml_root(NULL) == NULL);
    TRL_CHECK(test, trl_xml_node_kind(NULL) == TRL_XML_NODE_DOCUMENT && trl_xml_node_name(NULL) == NULL);
    TRL_CHECK(test, trl_xml_node_namespace(NULL) == NULL && trl_xml_node_prefix(NULL) == NULL);
    TRL_CHECK(test, trl_xml_node_value(NULL) == NULL && trl_xml_node_child_count(NULL) == 0U);
    TRL_CHECK(test, trl_xml_node_child(NULL, 0U) == NULL && trl_xml_node_attribute_count(NULL) == 0U);
    TRL_CHECK(test, trl_xml_node_attribute(NULL, 0U) == NULL);
    trl_xml_free(NULL);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(nodes_are_parsed_from_text),        TRL_TEST_CASE(namespaces_are_resolved),
        TRL_TEST_CASE(malformed_content_is_refused),      TRL_TEST_CASE(references_names_and_long_text),
        TRL_TEST_CASE(more_malformed_content_is_refused), TRL_TEST_CASE(a_values_nodes),
        TRL_TEST_CASE(xml_1_1_is_read_by_its_rules),      TRL_TEST_CASE(trees_past_the_limits_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
