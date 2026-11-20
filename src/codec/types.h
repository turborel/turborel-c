/**
 * @file types.h
 * @brief A data type, as a column or a parameter of a reply carries it:
 * its tag, then what its kind writes after it [PROTOCOL Data types].
 */
#ifndef TRL_CODEC_TYPES_H
#define TRL_CODEC_TYPES_H

#include "codec/buffer.h"
#include "codec/reader.h"
#include "codec/tags.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief A schema-qualified name, of a structured type or an XML schema. */
typedef struct trl_name {
    /** @brief The schema. */
    trl_slice schema;
    /** @brief The name in it. */
    trl_slice name;
} trl_name;

/** @brief A data type. */
typedef struct trl_data_type trl_data_type;

/** @brief A field of a row type: its name and type. */
typedef struct trl_row_field {
    /** @brief Its name. */
    trl_slice name;
    /** @brief Its type. */
    trl_data_type *type;
} trl_row_field;

/** @brief A structured type's names: its own and its proper supertypes'. */
typedef struct trl_type_names {
    /** @brief Its schema and name. */
    trl_name name;
    /** @brief Its proper supertypes, nearest first. */
    trl_name *supertypes;
    /** @brief How many. */
    uint32_t supertype_count;
} trl_type_names;

/** @brief What an XML type writes after its tag [PROTOCOL Data types]. */
typedef struct trl_xml_type {
    /** @brief TRL_XML_DOCUMENT, CONTENT or SEQUENCE. */
    uint8_t primary;
    /** @brief TRL_XML_UNTYPED, ANY or XMLSCHEMA. */
    uint8_t secondary;
    /** @brief Of XMLSCHEMA, the schema's schema and name. */
    trl_name schema;
    /** @brief Of XMLSCHEMA, whether a namespace is named. */
    bool has_namespace;
    /** @brief The namespace. */
    trl_slice target_namespace;
    /** @brief Of XMLSCHEMA, whether an element is named. */
    bool has_element;
    /** @brief The element. */
    trl_slice element;
} trl_xml_type;

/** @brief A data type, by its tag (`tags.h`). */
struct trl_data_type {
    /** @brief Its tag. */
    uint8_t tag;
    /** @brief What its tag writes after it. */
    union {
        /** @brief CHARACTER and CHARACTER VARYING: length and units. */
        struct {
            uint32_t length;
            uint8_t units;
        } character;
        /** @brief Exact numeric: precision and scale. */
        struct {
            uint32_t precision;
            uint32_t scale;
        } exact;
        /** @brief Approximate numeric, the datetimes and DECFLOAT: precision. */
        uint32_t precision;
        /** @brief BINARY and BINARY VARYING: length. */
        uint32_t length;
        /** @brief A plug-in's type: its name and parameters. */
        struct {
            trl_slice name;
            uint32_t params[TRL_MAX_PLUGIN_PARAMS];
            uint32_t param_count;
        } plugin;
        /** @brief INTERVAL: its qualifier. */
        trl_interval_qualifier interval;
        /** @brief Array: maximum cardinality and element type. */
        struct {
            uint32_t max;
            trl_data_type *element;
        } array;
        /** @brief Row: its fields, one or more. */
        struct {
            trl_row_field *fields;
            uint32_t count;
        } row;
        /** @brief CLOB and BLOB: length and units. */
        struct {
            uint64_t length;
            uint8_t units;
        } lob;
        /** @brief Multiset: its element type. */
        trl_data_type *element;
        /** @brief Structured type: its names. */
        trl_type_names structured;
        /** @brief Reference type: the referenced type and its scope. */
        struct {
            trl_type_names referenced;
            bool scoped;
            trl_name scope;
        } reference;
        /** @brief XML: its modifiers. */
        trl_xml_type xml;
    } as;
};

/**
 * @brief Reads a data type [PROTOCOL Data types].
 * @param reader The reader; its arena holds what the type holds.
 * @param out The type.
 * @return Nonzero if read; else the reader failed.
 */
bool trl_read_data_type(trl_reader *reader, trl_data_type *out);

/**
 * @brief Reads an interval's qualifier, as an INTERVAL type writes it and
 * an interval value begins [PROTOCOL Data types].
 * @param reader The reader.
 * @param out The qualifier.
 * @return Nonzero if read; its fields are 0 to 5.
 */
bool trl_read_interval_qualifier(trl_reader *reader, trl_interval_qualifier *out);

#endif
