/**
 * @file odbc_descriptors_test.c
 * @brief The ODBC driver's descriptors against a real server (RULES.md
 * R-TEST-8): an application's used as a statement's, the implementation's
 * read, fields set and read, record by record and field by field, copied
 * and freed; and each column attribute SQLColAttribute tells.
 */
#include "harness.h"
#include "odbc_live.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKIPPED 77

/* A record's fields SQLGetDescRec gives. */
typedef struct record {
    SQLCHAR name[ODBC_TEXT_BYTES];
    SQLSMALLINT name_length;
    SQLSMALLINT type;
    SQLLEN length;
    SQLSMALLINT precision;
    SQLSMALLINT scale;
    SQLSMALLINT nullable;
} record;

/* SQLGetDescRec of record `number` of `desc` into `r`. */
static SQLRETURN record_of(SQLHDESC desc, SQLSMALLINT number, record *r) {
    return SQLGetDescRec(desc, number, r->name, (SQLSMALLINT)sizeof r->name, &r->name_length, &r->type, NULL,
                         &r->length, &r->precision, &r->scale, &r->nullable);
}

/* Whether every field of record 1 of `desc` in `fields` is read and set. */
static bool fields_read_and_set(SQLHDESC desc, const SQLSMALLINT *fields, size_t count) {
    SQLLEN field = 0;
    bool every = true;
    for (size_t i = 0U; i < count; ++i) {
        every = every && SQLGetDescField(desc, 1, fields[i], &field, 0, NULL) == SQL_SUCCESS;
        every = every && SQLSetDescField(desc, 1, fields[i], NULL, 0) == SQL_SUCCESS;
    }
    return every;
}

/**
 * Verifies: [DESIGN] D2 an application's descriptor allocated, set record
 * by record and field by field, and used as a statement's row descriptor,
 * fetched into; an implicit descriptor not freed (HY017).
 */
static void an_application_descriptor_is_fetched_into(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLHDESC desc = SQL_NULL_HANDLE;
    SQLHDESC ard = odbc_descriptor(o.stmt, SQL_ATTR_APP_ROW_DESC);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DESC, o.dbc, &desc) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_DESC, ard) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_DESC, ard, NULL), "HY017") == 0);
    int32_t key = 0;
    SQLLEN key_length = 0;
    TRL_CHECK(test, SQLSetDescRec(desc, 1, SQL_C_SLONG, 0, 4, 0, 0, &key, &key_length, &key_length) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetDescField(desc, 1, SQL_DESC_OCTET_LENGTH, (SQLPOINTER)4, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetDescField(desc, 0, SQL_DESC_ARRAY_SIZE, (SQLPOINTER)1, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_APP_ROW_DESC, desc, 0) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE D (K INTEGER)") && odbc_run(o.stmt, "INSERT INTO D VALUES (41)"));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT K FROM D"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLFetch(o.stmt) == SQL_SUCCESS && key == 41);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, SQLFreeHandle(SQL_HANDLE_DESC, desc) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_descriptor(o.stmt, SQL_ATTR_APP_ROW_DESC) == ard);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_APP_ROW_DESC, o.stmt, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_APP_PARAM_DESC, SQL_NULL_HDESC, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DESC, o.dbc, &desc) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_APP_PARAM_DESC, desc, 0) == SQL_SUCCESS);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE D CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 the implementation row descriptor read, record by
 * record and field by field, as text and SQLWCHAR, not set (HY016) nor
 * copied into; the application's copied from another.
 */
static void the_implementation_descriptor_is_read(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLHDESC desc = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DESC, o.dbc, &desc) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetDescRec(desc, 1, SQL_C_SLONG, 0, 4, 0, 0, NULL, NULL, NULL) == SQL_SUCCESS);
    SQLHDESC ard = odbc_descriptor(o.stmt, SQL_ATTR_APP_ROW_DESC);
    SQLHDESC ird = odbc_descriptor(o.stmt, SQL_ATTR_IMP_ROW_DESC);
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE I (K INTEGER)") && odbc_run(o.stmt, "INSERT INTO I VALUES (41)"));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT K FROM I"), SQL_NTS) == SQL_SUCCESS);
    SQLSMALLINT count = 0;
    record r;
    TRL_CHECK(test, SQLGetDescField(ird, 0, SQL_DESC_COUNT, &count, 0, NULL) == SQL_SUCCESS && count == 1);
    TRL_CHECK(test, record_of(ird, 1, &r) == SQL_SUCCESS);
    TRL_CHECK(test, r.type == SQL_INTEGER && r.nullable == SQL_NULLABLE_UNKNOWN);
    TRL_CHECK(test,
              SQLGetDescField(ird, 1, SQL_DESC_TYPE_NAME, r.name, (SQLINTEGER)sizeof r.name, NULL) == SQL_SUCCESS);
    TRL_CHECK(test,
              SQLGetDescFieldW(ird, 1, SQL_DESC_OCTET_LENGTH, &r.length, 0, NULL) == SQL_SUCCESS && r.length == 4);
    SQLINTEGER sensitive = SQL_TRUE;
    TRL_CHECK(test, SQLGetDescField(ird, 1, SQL_DESC_CASE_SENSITIVE, &sensitive, 0, NULL) == SQL_SUCCESS &&
                        sensitive == SQL_FALSE);
    TRL_CHECK(test, SQLSetDescField(ird, 1, SQL_DESC_TYPE, (SQLPOINTER)SQL_C_CHAR, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetDescRec(ird, 1, SQL_C_CHAR, 0, 4, 0, 0, NULL, NULL, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLCopyDesc(desc, ird) == SQL_ERROR && SQLCopyDesc(ird, desc) == SQL_ERROR);
    TRL_CHECK(test, SQLCopyDesc(desc, ard) == SQL_SUCCESS);
    TRL_CHECK(test, record_of(ard, 1, &r) == SQL_SUCCESS && r.type == SQL_C_SLONG && r.length == 4);
    TRL_CHECK(test, SQLGetDescRec(ard, 9, r.name, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == SQL_NO_DATA);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeHandle(SQL_HANDLE_DESC, desc);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE I CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 each field of a record and of the header read and
 * set; a field that is none, or a record below 0, refused (HY091, 07009);
 * the count grown and shrunk; a datetime record set by its code; the
 * parameters' implementation descriptor's records nullable.
 */
static void each_descriptor_field_is_read_and_set(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    SQLHDESC ard = odbc_descriptor(o.stmt, SQL_ATTR_APP_ROW_DESC);
    SQLSMALLINT type = 0;
    TRL_CHECK(test, SQLGetDescField(ard, 0, SQL_DESC_ALLOC_TYPE, &type, 0, NULL) == SQL_SUCCESS &&
                        type == SQL_DESC_ALLOC_AUTO);
    TRL_CHECK(test, SQLSetDescFieldW(ard, 1, SQL_DESC_TYPE, (SQLPOINTER)SQL_C_TYPE_DATE, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetDescField(ard, 1, 9999, &type, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLGetDescField(ard, -1, SQL_DESC_TYPE, &type, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, SQLSetDescField(ard, 1, 9999, NULL, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetDescField(ard, -1, SQL_DESC_TYPE, NULL, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetDescFieldW(ard, 1, SQL_DESC_TYPE, (SQLPOINTER)SQL_C_TYPE_DATE, 0) == SQL_SUCCESS);
    static const SQLSMALLINT fields[] = {
        SQL_DESC_CONCISE_TYPE,   SQL_DESC_DATETIME_INTERVAL_CODE,
        SQL_DESC_DATA_PTR,       SQL_DESC_OCTET_LENGTH_PTR,
        SQL_DESC_INDICATOR_PTR,  SQL_DESC_LENGTH,
        SQL_DESC_PRECISION,      SQL_DESC_SCALE,
        SQL_DESC_PARAMETER_TYPE,
    };
    TRL_CHECK(test, fields_read_and_set(ard, fields, sizeof fields / sizeof fields[0]));
    SQLHDESC desc = SQL_NULL_HANDLE;
    TRL_CHECK(test, SQLAllocHandle(SQL_HANDLE_DESC, o.dbc, &desc) == SQL_SUCCESS);
    static const SQLSMALLINT headers[] = {
        SQL_DESC_COUNT,           SQL_DESC_ARRAY_SIZE,       SQL_DESC_BIND_TYPE,
        SQL_DESC_BIND_OFFSET_PTR, SQL_DESC_ARRAY_STATUS_PTR, SQL_DESC_ROWS_PROCESSED_PTR,
    };
    SQLLEN field = 0;
    bool every = true;
    for (size_t i = 0U; i < sizeof headers / sizeof headers[0]; ++i) {
        every = every && SQLGetDescField(desc, 0, headers[i], &field, 0, NULL) == SQL_SUCCESS;
        every = every && (i < 3U || SQLSetDescField(desc, 0, headers[i], NULL, 0) == SQL_SUCCESS);
    }
    TRL_CHECK(test,
              every && SQLSetDescField(desc, 0, SQL_DESC_BIND_TYPE, (SQLPOINTER)SQL_BIND_BY_COLUMN, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetDescField(desc, 0, SQL_DESC_COUNT, (SQLPOINTER)2, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetDescField(desc, 0, SQL_DESC_COUNT, (SQLPOINTER)1, 0) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetDescField(desc, 0, SQL_DESC_ALLOC_TYPE, &type, 0, NULL) == SQL_SUCCESS &&
                        type == SQL_DESC_ALLOC_USER);
    TRL_CHECK(test, SQLSetDescRec(desc, 2, SQL_DATETIME, SQL_CODE_DATE, 6, 0, 0, NULL, NULL, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, SQLSetDescRec(desc, 0, SQL_C_CHAR, 0, 1, 0, 0, NULL, NULL, NULL) == SQL_ERROR);
    SQLHDESC ipd = odbc_descriptor(o.stmt, SQL_ATTR_IMP_PARAM_DESC);
    (void)SQLSetDescField(ipd, 1, SQL_DESC_TYPE, (SQLPOINTER)SQL_INTEGER, 0);
    record r;
    TRL_CHECK(test, record_of(ipd, 1, &r) == SQL_SUCCESS && r.nullable == SQL_NULLABLE);
    (void)SQLFreeHandle(SQL_HANDLE_DESC, desc);
    odbc_close(&o);
}

/* Each attribute SQLColAttribute tells. */
static const SQLUSMALLINT attributes_told[] = {
    SQL_DESC_NAME,
    SQL_DESC_LABEL,
    SQL_DESC_BASE_COLUMN_NAME,
    SQL_COLUMN_NAME,
    SQL_DESC_TYPE_NAME,
    SQL_DESC_LOCAL_TYPE_NAME,
    SQL_DESC_TABLE_NAME,
    SQL_DESC_BASE_TABLE_NAME,
    SQL_DESC_SCHEMA_NAME,
    SQL_DESC_CATALOG_NAME,
    SQL_DESC_LITERAL_PREFIX,
    SQL_DESC_LITERAL_SUFFIX,
    SQL_DESC_LENGTH,
    SQL_COLUMN_LENGTH,
    SQL_DESC_PRECISION,
    SQL_COLUMN_PRECISION,
    SQL_DESC_SCALE,
    SQL_COLUMN_SCALE,
    SQL_DESC_OCTET_LENGTH,
    SQL_DESC_DISPLAY_SIZE,
    SQL_DESC_NUM_PREC_RADIX,
    SQL_DESC_TYPE,
    SQL_DESC_CONCISE_TYPE,
    SQL_DESC_DATETIME_INTERVAL_CODE,
    SQL_DESC_NULLABLE,
    SQL_COLUMN_NULLABLE,
    SQL_DESC_CASE_SENSITIVE,
    SQL_DESC_SEARCHABLE,
    SQL_DESC_UPDATABLE,
    SQL_DESC_UNNAMED,
    SQL_DESC_AUTO_UNIQUE_VALUE,
    SQL_DESC_FIXED_PREC_SCALE,
    SQL_DESC_UNSIGNED,
    SQL_DESC_COUNT,
    SQL_COLUMN_COUNT,
};

/* Whether each attribute SQLColAttribute tells is told of each of the
   result's `columns`, as text and as SQLWCHAR. */
static bool every_attribute_told(SQLHSTMT stmt, SQLUSMALLINT columns) {
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    SQLLEN number = 0;
    bool every = true;
    for (SQLUSMALLINT column = 1U; column <= columns; ++column) {
        for (size_t i = 0U; i < sizeof attributes_told / sizeof attributes_told[0]; ++i) {
            every = every && SQLColAttribute(stmt, column, attributes_told[i], text, (SQLSMALLINT)sizeof text, &length,
                                             &number) == SQL_SUCCESS;
        }
        every = every && SQLColAttributeW(stmt, column, SQL_DESC_NAME, wide, (SQLSMALLINT)sizeof wide, &length,
                                          &number) == SQL_SUCCESS;
    }
    return every;
}

/**
 * Verifies: [DESIGN] D2 a result's columns described by each attribute
 * SQLColAttribute gives, ODBC 2's among them, as text and SQLWCHAR, and by
 * the implementation row descriptor's fields; an attribute that is none
 * refused (HY091), a column past the last (07009).
 */
static void each_column_attribute_is_told(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, SQLExecDirect(o.stmt,
                                  sql_text("VALUES (CAST(1 AS SMALLINT), CAST('x' AS CHARACTER(3)), "
                                           "CAST(X'01' AS BINARY(2)), CAST(1 AS BIGINT), CAST(1 AS REAL), "
                                           "TIME '01:02:03', CAST('[1]' AS JSON), CAST(1 AS DECFLOAT))"),
                                  SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, every_attribute_told(o.stmt, 8U));
    SQLCHAR text[ODBC_TEXT_BYTES];
    SQLLEN number = 0;
    TRL_CHECK(test, SQLColAttribute(o.stmt, 1, 9999, text, 0, NULL, &number) == SQL_ERROR);
    TRL_CHECK(test, SQLColAttribute(o.stmt, 9, SQL_DESC_NAME, text, 0, NULL, &number) == SQL_ERROR);
    TRL_CHECK(test, SQLColAttribute(o.stmt, 7, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &number) == SQL_SUCCESS &&
                        number == SQL_LONGVARCHAR);
    TRL_CHECK(test, SQLColAttribute(o.stmt, 8, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &number) == SQL_SUCCESS &&
                        number == SQL_VARCHAR);
    SQLWCHAR wide[ODBC_TEXT_BYTES];
    SQLSMALLINT length = 0;
    SQLULEN size = 0;
    SQLSMALLINT type = 0;
    TRL_CHECK(test,
              SQLDescribeColW(o.stmt, 6, wide, ODBC_TEXT_BYTES, &length, &type, &size, NULL, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, type == SQL_TYPE_TIME);
    SQLHDESC ird = odbc_descriptor(o.stmt, SQL_ATTR_IMP_ROW_DESC);
    static const SQLSMALLINT fields[] = {
        SQL_DESC_LENGTH,
        SQL_DESC_DISPLAY_SIZE,
        SQL_DESC_AUTO_UNIQUE_VALUE,
        SQL_DESC_NUM_PREC_RADIX,
        SQL_DESC_DATETIME_INTERVAL_PRECISION,
    };
    bool every = true;
    for (size_t i = 0U; i < sizeof fields / sizeof fields[0]; ++i) {
        SQLLEN field = 0;
        SQLINTEGER field_length = 0;
        every = every && SQLGetDescField(ird, 1, fields[i], &field, 0, &field_length) == SQL_SUCCESS;
    }
    TRL_CHECK(test, every);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [PROTOCOL Declared types] a table's SMALLINT, INTEGER and
 * BIGINT columns are told by their declared types, as `declared-types`
 * gives them: SQLDescribeCol's SQL_SMALLINT, SQL_INTEGER and SQL_BIGINT,
 * SQLColAttribute's SQL_DESC_CONCISE_TYPE the same and its
 * SQL_DESC_TYPE_NAME the names declared, where their tags alone say
 * NUMERIC of 5, 10 and 19 digits.
 */
static void declared_integers_are_odbcs(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o) && odbc_run(o.stmt, "CREATE TABLE W (S SMALLINT, I INTEGER, B BIGINT)"));
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT S, I, B FROM W"), SQL_NTS) == SQL_SUCCESS);
    static const SQLSMALLINT types[] = {SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT};
    static const char *const names[] = {"SMALLINT", "INTEGER", "BIGINT"};
    for (SQLUSMALLINT i = 0U; i < 3U; ++i) {
        SQLCHAR name[ODBC_TEXT_BYTES];
        SQLSMALLINT type = 0;
        SQLULEN size = 0U;
        TRL_CHECK(test, SQLDescribeCol(o.stmt, (SQLUSMALLINT)(i + 1U), name, (SQLSMALLINT)sizeof name, NULL, &type,
                                       &size, NULL, NULL) == SQL_SUCCESS &&
                            type == types[i]);
        SQLLEN concise = 0;
        TRL_CHECK(test, SQLColAttribute(o.stmt, (SQLUSMALLINT)(i + 1U), SQL_DESC_CONCISE_TYPE, NULL, 0, NULL,
                                        &concise) == SQL_SUCCESS &&
                            concise == types[i]);
        TRL_CHECK(test, SQLColAttribute(o.stmt, (SQLUSMALLINT)(i + 1U), SQL_DESC_TYPE_NAME, name,
                                        (SQLSMALLINT)sizeof name, NULL, NULL) == SQL_SUCCESS &&
                            strcmp((const char *)name, names[i]) == 0);
    }
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE W CASCADE"));
    odbc_close(&o);
}

/* A result column's SQL type, column size and display size, as SQLColAttribute tells them. */
typedef struct told {
    SQLLEN type;
    SQLLEN size;
    SQLLEN display;
} told;

/* What SQLColAttribute tells of column `column`. */
static told told_of(SQLHSTMT stmt, SQLUSMALLINT column) {
    told t = {0, 0, 0};
    (void)SQLColAttribute(stmt, column, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &t.type);
    (void)SQLColAttribute(stmt, column, SQL_DESC_LENGTH, NULL, 0, NULL, &t.size);
    (void)SQLColAttribute(stmt, column, SQL_DESC_DISPLAY_SIZE, NULL, 0, NULL, &t.display);
    return t;
}

/**
 * Verifies: [DESIGN] D2 each of Turborel's types told as ODBC's: a time
 * and a timestamp of fractional seconds, their sizes counting them; JSON,
 * an array, a row and a multiset as long text; a binary string's display
 * twice its octets; BOOLEAN as SQL_BIT, UUID as SQL_GUID of 36
 * characters; a type ODBC has none of, an interval, as text.
 */
static void each_type_is_odbcs(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    static SQLCHAR sql[] =
        "VALUES (CAST('01:02:03.123' AS TIME(3)), CAST('2020-01-01 01:02:03.5' AS TIMESTAMP(6)), "
        "CAST('[1]' AS JSON), ARRAY[1, 2], ROW(1, 'a'), MULTISET[1], "
        "CAST(X'01' AS BINARY VARYING(3)), TRUE, CAST('12345678-9abc-def0-1234-56789abcdef0' AS UUID), "
        "INTERVAL '1' DAY)";
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecDirect(o.stmt, sql, SQL_NTS)) == SQL_SUCCESS);
    told t = told_of(o.stmt, 1);
    TRL_CHECK(test, t.type == SQL_TYPE_TIME && t.size == 12);
    t = told_of(o.stmt, 2);
    TRL_CHECK(test, t.type == SQL_TYPE_TIMESTAMP && t.size == 26);
    bool every = true;
    for (SQLUSMALLINT c = 3U; c <= 6U; ++c) {
        every = every && told_of(o.stmt, c).type == SQL_LONGVARCHAR;
    }
    TRL_CHECK(test, every);
    t = told_of(o.stmt, 7);
    TRL_CHECK(test, t.type == SQL_VARBINARY && t.size == 3 && t.display == 6);
    TRL_CHECK(test, told_of(o.stmt, 8).type == SQL_BIT);
    t = told_of(o.stmt, 9);
    TRL_CHECK(test, t.type == SQL_GUID && t.size == 36);
    TRL_CHECK(test, told_of(o.stmt, 10).type == SQL_VARCHAR);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/* 38 digits, the most an exact number holds, and 39, past them. */
#define WIDEST "12345678901234567890123456789012345678"
#define PAST_WIDEST "100000000000000000000000000000000000000"

/* SQL_NUMERIC_STRUCT of the digits `digits`, scale 0, positive. */
static SQL_NUMERIC_STRUCT numeric_of(const char *digits) {
    SQL_NUMERIC_STRUCT n;
    memset(&n, 0, sizeof n);
    n.precision = 38U;
    n.sign = 1U;
    for (const char *at = digits; *at != '\0'; ++at) {
        unsigned int carry = (unsigned int)(*at - '0');
        for (size_t i = 0U; i < SQL_MAX_NUMERIC_LEN; ++i) {
            unsigned int next = (n.val[i] * 10U) + carry;
            n.val[i] = (SQLCHAR)(next & 0xFFU);
            carry = next >> 8U;
        }
    }
    return n;
}

/**
 * Verifies: [DESIGN] D2 an exact number of 38 digits, the most turborel
 * holds, through ODBC: sent as SQL_C_NUMERIC and as text, read as
 * SQL_C_CHAR and as SQL_C_NUMERIC unchanged; its column's size, and
 * SQLGetTypeInfo's of NUMERIC, 38; one of 39 digits refused (22003).
 */
static void exact_numbers_hold_38_digits(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE WIDE (N NUMERIC(38,0), T NUMERIC(38,0))"));
    SQL_NUMERIC_STRUCT sent = numeric_of(WIDEST);
    char text[] = WIDEST;
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_NUMERIC, SQL_NUMERIC, 38, 0, &sent, 0, NULL);
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_NUMERIC, 38, 0, text, 0, NULL);
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecDirect(o.stmt, sql_text("INSERT INTO WIDE VALUES (?, ?)"), SQL_NTS)) ==
                        SQL_SUCCESS);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT N, T FROM WIDE"), SQL_NTS) == SQL_SUCCESS &&
                        SQLFetch(o.stmt) == SQL_SUCCESS);
    char read[64];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, read, (SQLLEN)sizeof read, &length) == SQL_SUCCESS &&
                        strcmp(read, WIDEST) == 0);
    SQL_NUMERIC_STRUCT got;
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_NUMERIC, &got, 0, &length) == SQL_SUCCESS &&
                        memcmp(got.val, sent.val, sizeof got.val) == 0 && got.sign == 1U);
    SQLULEN size = 0U;
    TRL_CHECK(test, SQLDescribeCol(o.stmt, 1, NULL, 0, NULL, NULL, &size, NULL, NULL) == SQL_SUCCESS && size == 38U);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    SQLINTEGER most = 0;
    TRL_CHECK(test, SQLGetTypeInfo(o.stmt, SQL_NUMERIC) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS &&
                        SQLGetData(o.stmt, 3, SQL_C_SLONG, &most, 0, &length) == SQL_SUCCESS && most == 38);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    char past[] = PAST_WIDEST;
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_NUMERIC, 39, 0, past, 0, NULL);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("INSERT INTO WIDE (N) VALUES (?)"), SQL_NTS) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "22003") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE WIDE CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a query that fails before its first batch is
 * sent, answered by its Error alone [PROTOCOL Query 'Q']: SQLExecDirect
 * SQL_ERROR (22012) with no result's columns, and SQLExecute of it
 * prepared, its prepared columns kept; the statement used again after.
 */
static void a_query_failing_at_its_first_batch_is_an_error(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test,
              odbc_run(o.stmt, "CREATE TABLE ZERO (K INTEGER)") && odbc_run(o.stmt, "INSERT INTO ZERO VALUES (0)"));
    SQLSMALLINT columns = -1;
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT 1 / K FROM ZERO"), SQL_NTS) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "22012") == 0);
    TRL_CHECK(test, SQLNumResultCols(o.stmt, &columns) == SQL_SUCCESS && columns == 0);
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("SELECT 1 / K FROM ZERO"), SQL_NTS) == SQL_SUCCESS);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "22012") == 0);
    TRL_CHECK(test, SQLNumResultCols(o.stmt, &columns) == SQL_SUCCESS && columns == 1);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE ZERO CASCADE"));
    odbc_close(&o);
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc-desc", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(an_application_descriptor_is_fetched_into),
        TRL_TEST_CASE(the_implementation_descriptor_is_read),
        TRL_TEST_CASE(each_descriptor_field_is_read_and_set),
        TRL_TEST_CASE(each_column_attribute_is_told),
        TRL_TEST_CASE(declared_integers_are_odbcs),
        TRL_TEST_CASE(each_type_is_odbcs),
        TRL_TEST_CASE(exact_numbers_hold_38_digits),
        TRL_TEST_CASE(a_query_failing_at_its_first_batch_is_an_error),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
