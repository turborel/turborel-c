/**
 * @file tags.h
 * @brief The tags of the protocol's data types and values, each named once
 * (R-CONST-2) [PROTOCOL Data types] [PROTOCOL Values].
 *
 * A type and a value of one kind share a tag where both have one. UUID's
 * tag, 27, is turborel's RULES.md D184 and `protocol/src/fields.rs`.
 */
#ifndef TRL_CODEC_TAGS_H
#define TRL_CODEC_TAGS_H

/** @brief The null value. */
#define TRL_TAG_NULL 0U
/** @brief CHARACTER; a character string value, of CHARACTER VARYING too. */
#define TRL_TAG_CHARACTER 1U
/** @brief An exact numeric type and value. */
#define TRL_TAG_EXACT 2U
/** @brief An approximate numeric type and value. */
#define TRL_TAG_APPROXIMATE 3U
/** @brief A plug-in's type and value. */
#define TRL_TAG_PLUGIN 4U
/** @brief CHARACTER VARYING, as a type. */
#define TRL_TAG_VARYING 5U
/** @brief BINARY; a binary string value, of either binary type. */
#define TRL_TAG_BINARY 6U
/** @brief BINARY VARYING, as a type. */
#define TRL_TAG_BINARY_VARYING 7U
/** @brief DATE, as a type and a value. */
#define TRL_TAG_DATE 8U
/** @brief TIME; a time value, of either time type. */
#define TRL_TAG_TIME 9U
/** @brief TIME WITH TIME ZONE, as a type. */
#define TRL_TAG_TIME_ZONED 10U
/** @brief TIMESTAMP; a timestamp value, of either timestamp type. */
#define TRL_TAG_TIMESTAMP 11U
/** @brief TIMESTAMP WITH TIME ZONE, as a type. */
#define TRL_TAG_TIMESTAMP_ZONED 12U
/** @brief INTERVAL, as a type and a value. */
#define TRL_TAG_INTERVAL 13U
/** @brief BOOLEAN; a truth value. */
#define TRL_TAG_BOOLEAN 14U
/** @brief DECFLOAT, as a type and a value. */
#define TRL_TAG_DECFLOAT 15U
/** @brief An array type and value. */
#define TRL_TAG_ARRAY 16U
/** @brief A row type and value. */
#define TRL_TAG_ROW 17U
/** @brief CHARACTER LARGE OBJECT, as a type. */
#define TRL_TAG_CLOB 18U
/** @brief BINARY LARGE OBJECT, as a type. */
#define TRL_TAG_BLOB 19U
/** @brief A large object's value, whole. */
#define TRL_TAG_LOB_WHOLE 20U
/** @brief A large object's value, by its locator. */
#define TRL_TAG_LOB_LOCATOR 21U
/** @brief A multiset type and value. */
#define TRL_TAG_MULTISET 22U
/** @brief A structured type and value. */
#define TRL_TAG_STRUCTURED 23U
/** @brief A reference type and value. */
#define TRL_TAG_REFERENCE 24U
/** @brief JSON, as a type and a value. */
#define TRL_TAG_JSON 25U
/** @brief An XML type and value. */
#define TRL_TAG_XML 26U
/** @brief UUID, as a type and a value. */
#define TRL_TAG_UUID 27U

/** @brief A character length counted in characters [PROTOCOL Data types]. */
#define TRL_UNITS_CHARACTERS 0U
/** @brief A character length counted in octets [PROTOCOL Data types]. */
#define TRL_UNITS_OCTETS 1U

/** @brief A large object of characters [PROTOCOL Values]. */
#define TRL_LOB_CHARACTER 0U
/** @brief A large object of octets [PROTOCOL Values]. */
#define TRL_LOB_BINARY 1U

/** @brief The interval field YEAR [PROTOCOL Data types]. */
#define TRL_FIELD_YEAR 0U
/** @brief The interval field MONTH. */
#define TRL_FIELD_MONTH 1U
/** @brief The interval field DAY. */
#define TRL_FIELD_DAY 2U
/** @brief The interval field HOUR. */
#define TRL_FIELD_HOUR 3U
/** @brief The interval field MINUTE. */
#define TRL_FIELD_MINUTE 4U
/** @brief The interval field SECOND, the last. */
#define TRL_FIELD_SECOND 5U

/** @brief XML's primary modifier DOCUMENT [PROTOCOL Data types]. */
#define TRL_XML_DOCUMENT 0U
/** @brief XML's primary modifier CONTENT. */
#define TRL_XML_CONTENT 1U
/** @brief XML's primary modifier SEQUENCE. */
#define TRL_XML_SEQUENCE 2U
/** @brief XML's secondary modifier UNTYPED. */
#define TRL_XML_UNTYPED 0U
/** @brief XML's secondary modifier ANY. */
#define TRL_XML_ANY 1U
/** @brief XML's secondary modifier XMLSCHEMA. */
#define TRL_XML_XMLSCHEMA 2U

/** @brief The bytes of a UUID's value, after their count [PROTOCOL Values]. */
#define TRL_UUID_OCTETS 16U
/** @brief The greatest maximum cardinality of an array type, turborel's. */
#define TRL_MAX_ARRAY_CARDINALITY 65535U
/** @brief The most elements of a multiset, turborel's. */
#define TRL_MAX_MULTISET_CARDINALITY 65535U
/** @brief The most parameters of a plug-in's type, turborel's MAX_PARAMS. */
#define TRL_MAX_PLUGIN_PARAMS 2U

#endif
