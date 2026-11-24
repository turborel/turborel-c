/**
 * @file odbc_types_test.c
 * @brief The ODBC driver's conversions against a real server (RULES.md
 * R-TEST-8): each value of a result given as each C type, as ODBC's
 * reference's Appendix D converts it, and each C type of a parameter sent.
 */
#include "format.h"
#include "harness.h"
#include "odbc_live.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKIPPED 77
#define LONG_BYTES 70000U
#define PARAMETERS 14U

/* The row of numbers fetched: a statement over one row of each type. */
static bool one_row(SQLHSTMT stmt, const char *sql) {
    (void)SQLFreeStmt(stmt, SQL_CLOSE);
    return odbc_said(stmt, SQLExecDirect(stmt, sql_text(sql), SQL_NTS)) == SQL_SUCCESS &&
           odbc_said(stmt, SQLFetch(stmt)) == SQL_SUCCESS;
}

/* What SQLGetData of column `column` as `c_type` returns, its SQLSTATE in
   `state` if it warned or failed; another column read first, so the
   column is read afresh, as SQLGetData gives a value whole once. */
static SQLRETURN got(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT c_type, void *out, char *state) {
    SQLLEN length = 0;
    char scratch[1];
    (void)SQLGetData(stmt, column == 1U ? 2U : 1U, SQL_C_BINARY, scratch, 0, &length);
    SQLRETURN returned = SQLGetData(stmt, column, c_type, out, ODBC_TEXT_BYTES, &length);
    trl_format(state, sizeof "00000", "%s",
               returned == SQL_SUCCESS ? "00000" : odbc_state(SQL_HANDLE_STMT, stmt, NULL));
    return returned;
}

/* A number given as one of the C types. */
typedef union number_out {
    signed char tiny;
    unsigned char utiny;
    short small;
    unsigned short usmall;
    int32_t integer;
    uint32_t uinteger;
    int64_t big;
    uint64_t ubig;
    float single;
    double real;
    char bytes[ODBC_TEXT_BYTES];
} number_out;

/* The row of numbers the tests of the numeric C types read. */
#define NUMBERS                                                                                                        \
    "VALUES (CAST(300 AS INTEGER), CAST(-2.75 AS NUMERIC(5,2)), CAST(1.5E0 AS DOUBLE PRECISION), TRUE, '42', 'x', "    \
    "CAST(5000000000 AS BIGINT), CAST(0.5 AS NUMERIC(2,1)))"

/**
 * Verifies: [DESIGN] D2 numbers given as each integer C type, in range or
 * out of it (22003), a fraction dropped (01S07); as floats and doubles;
 * as SQL_C_BIT, 0 and 1, between them dropped (01S07), past them 22003;
 * a character string's number read, one that is none refused (22018).
 */
static void numbers_are_given_as_c_integers_and_floats(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, one_row(o.stmt, NUMBERS));
    char state[6];
    number_out v;
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_STINYINT, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_SSHORT, &v, state) == SQL_SUCCESS && v.small == 300);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_USHORT, &v, state) == SQL_SUCCESS && v.usmall == 300U);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_UTINYINT, &v, state) == SQL_ERROR);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_SLONG, &v, state) == SQL_SUCCESS_WITH_INFO && v.integer == -2);
    TRL_CHECK(test, strcmp(state, "01S07") == 0);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_ULONG, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_DOUBLE, &v, state) == SQL_SUCCESS && v.real == -2.75);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_SBIGINT, &v, state) == SQL_SUCCESS_WITH_INFO && v.big == 1);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_FLOAT, &v, state) == SQL_SUCCESS && v.single == 1.5F);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_UTINYINT, &v, state) == SQL_SUCCESS_WITH_INFO && v.utiny == 1U);
    TRL_CHECK(test, got(o.stmt, 4, SQL_C_SLONG, &v, state) == SQL_SUCCESS && v.integer == 1);
    TRL_CHECK(test, got(o.stmt, 4, SQL_C_BIT, &v, state) == SQL_SUCCESS && v.utiny == 1U);
    TRL_CHECK(test, got(o.stmt, 5, SQL_C_TINYINT, &v, state) == SQL_SUCCESS && v.tiny == 42);
    TRL_CHECK(test, got(o.stmt, 5, SQL_C_UBIGINT, &v, state) == SQL_SUCCESS && v.ubig == 42U);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a number past a C type's range refused (22003), as
 * an integer, SQL_C_BIT or a float; a character string's that is none
 * (22018); a fraction of a bit dropped (01S07).
 */
static void numbers_past_a_c_type_are_refused(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, one_row(o.stmt, NUMBERS));
    char state[6];
    number_out v;
    TRL_CHECK(test, got(o.stmt, 6, SQL_C_LONG, &v, state) == SQL_ERROR && strcmp(state, "22018") == 0);
    TRL_CHECK(test, got(o.stmt, 7, SQL_C_SLONG, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    TRL_CHECK(test, got(o.stmt, 7, SQL_C_UBIGINT, &v, state) == SQL_SUCCESS && v.ubig == 5000000000U);
    TRL_CHECK(test, got(o.stmt, 7, SQL_C_SHORT, &v, state) == SQL_ERROR);
    TRL_CHECK(test, got(o.stmt, 8, SQL_C_BIT, &v, state) == SQL_SUCCESS_WITH_INFO && v.utiny == 0U);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_BIT, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_UBIGINT, &v, state) == SQL_ERROR);
    TRL_CHECK(test, one_row(o.stmt, "VALUES (CAST(1E300 AS DOUBLE PRECISION), CAST(-1E300 AS DOUBLE PRECISION))"));
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_FLOAT, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_SBIGINT, &v, state) == SQL_ERROR);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 numbers at the C types' edges: a fraction of a bit
 * dropped (01S07), a bit past 1 (22003), text a number begins (22018), a
 * DECFLOAT as an integer, a float past a REAL's range (22003).
 */
static void numbers_at_the_c_types_edges(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, one_row(o.stmt, "VALUES (CAST(0.5 AS NUMERIC(2,1)), CAST(2 AS INTEGER), '1x', "
                                    "CAST(1.5 AS DECFLOAT), CAST(1E39 AS DOUBLE PRECISION))"));
    char state[6];
    number_out v;
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_BIT, &v, state) == SQL_SUCCESS_WITH_INFO && v.utiny == 0U);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_BIT, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_SLONG, &v, state) == SQL_ERROR && strcmp(state, "22018") == 0);
    TRL_CHECK(test, got(o.stmt, 4, SQL_C_SLONG, &v, state) == SQL_SUCCESS_WITH_INFO && v.integer == 1);
    TRL_CHECK(test, got(o.stmt, 5, SQL_C_FLOAT, &v, state) == SQL_ERROR && strcmp(state, "22003") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 exact numbers given as SQL_C_NUMERIC at the scale
 * the bound column asks, by SQL_ARD_TYPE, a fraction past it dropped
 * (01S07); a negative one's sign; an approximate one's.
 */
static void numbers_are_given_as_sql_numeric(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE N (A NUMERIC(10,3), B DOUBLE PRECISION)"));
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO N VALUES (-12.345, 2.5)"));
    TRL_CHECK(test, one_row(o.stmt, "SELECT A, B FROM N"));
    SQL_NUMERIC_STRUCT n;
    SQLHDESC ard = odbc_descriptor(o.stmt, SQL_ATTR_APP_ROW_DESC);
    (void)SQLSetDescField(ard, 1, SQL_DESC_TYPE, (SQLPOINTER)SQL_C_NUMERIC, 0);
    (void)SQLSetDescField(ard, 1, SQL_DESC_PRECISION, (SQLPOINTER)10, 0);
    (void)SQLSetDescField(ard, 1, SQL_DESC_SCALE, (SQLPOINTER)2, 0);
    (void)SQLSetDescField(ard, 2, SQL_DESC_TYPE, (SQLPOINTER)SQL_C_NUMERIC, 0);
    (void)SQLSetDescField(ard, 2, SQL_DESC_SCALE, (SQLPOINTER)1, 0);
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_ARD_TYPE, &n, (SQLLEN)sizeof n, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, n.sign == 0U && n.scale == 2 && n.val[0] == 0xD2 && n.val[1] == 0x04); /* 1234 */
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_ARD_TYPE, &n, (SQLLEN)sizeof n, &length) == SQL_SUCCESS);
    TRL_CHECK(test, n.sign == 1U && n.val[0] == 25U);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_NUMERIC, &n, (SQLLEN)sizeof n, &length) == SQL_SUCCESS_WITH_INFO);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE N CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 datetimes given as ODBC's date, time and
 * timestamp structures, ODBC 2's and 3's, a timestamp's time dropped of a
 * date (01S07), a time of a date refused (07006); a datetime's text read;
 * text that is none refused (22018); a UUID and its text as SQLGUID.
 */
static void datetimes_and_uuids_are_given_as_structures(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, one_row(o.stmt, "VALUES (DATE '2020-02-03', TIME '04:05:06.5', TIMESTAMP '2021-07-08 09:10:11.25', "
                                    "'2022-01-02 03:04:05', 'nope', CAST('12345678-9abc-def0-1234-56789abcdef0' AS "
                                    "UUID), '12345678-9abc-def0-1234-56789abcdef0')"));
    char state[6];
    DATE_STRUCT d;
    TIME_STRUCT t;
    TIMESTAMP_STRUCT ts;
    SQLGUID guid;
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_DATE, &d, state) == SQL_SUCCESS && d.year == 2020 && d.day == 3U);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_TYPE_TIMESTAMP, &ts, state) == SQL_SUCCESS && ts.hour == 0U);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_TYPE_TIME, &t, state) == SQL_ERROR && strcmp(state, "07006") == 0);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_TIME, &t, state) == SQL_SUCCESS_WITH_INFO && t.second == 6U);
    TRL_CHECK(test, got(o.stmt, 2, SQL_C_TYPE_DATE, &d, state) == SQL_ERROR);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_TIMESTAMP, &ts, state) == SQL_SUCCESS && ts.fraction == 250000000U);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_TYPE_DATE, &d, state) == SQL_SUCCESS_WITH_INFO && d.month == 7U);
    TRL_CHECK(test, strcmp(state, "01S07") == 0);
    TRL_CHECK(test, got(o.stmt, 3, SQL_C_TYPE_TIME, &t, state) == SQL_SUCCESS_WITH_INFO && t.minute == 10U);
    TRL_CHECK(test, got(o.stmt, 4, SQL_C_TYPE_TIMESTAMP, &ts, state) == SQL_SUCCESS && ts.year == 2022);
    TRL_CHECK(test, got(o.stmt, 5, SQL_C_TYPE_DATE, &d, state) == SQL_ERROR && strcmp(state, "22018") == 0);
    TRL_CHECK(test, got(o.stmt, 6, SQL_C_GUID, &guid, state) == SQL_SUCCESS && guid.Data1 == 0x12345678U);
    TRL_CHECK(test, guid.Data2 == 0x9ABCU && guid.Data3 == 0xDEF0U && guid.Data4[7] == 0xF0U);
    TRL_CHECK(test, got(o.stmt, 7, SQL_C_GUID, &guid, state) == SQL_SUCCESS && guid.Data2 == 0x9ABCU);
    TRL_CHECK(test, got(o.stmt, 5, SQL_C_GUID, &guid, state) == SQL_ERROR && strcmp(state, "22018") == 0);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_INTERVAL_DAY, &d, state) == SQL_ERROR && strcmp(state, "07006") == 0);
    TRL_CHECK(test, got(o.stmt, 1, SQL_C_SLONG, &d, state) == SQL_ERROR && strcmp(state, "07006") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 strings given as text, SQLWCHAR text and octets a
 * piece at a time, each piece's length what was left; a binary string's
 * octets, and as text its hexadecimal; the null value's indicator, and
 * without one 22002; SQL_NO_DATA once a value is given.
 */
static void strings_are_given_in_pieces(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, one_row(o.stmt, "VALUES ('ab\xC3\xA9', CAST(X'0102' AS VARBINARY(4)), CAST(NULL AS INTEGER), '')"));
    SQLWCHAR wide[3];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, wide[0] == 'a' && wide[1] == 'b' && length == (SQLLEN)(3U * sizeof(SQLWCHAR)));
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length) == SQL_SUCCESS);
    TRL_CHECK(test, wide[0] == 0xE9 && length == (SQLLEN)sizeof(SQLWCHAR));
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length) == SQL_NO_DATA);
    unsigned char octets[4];
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, octets, 1, &length) == SQL_SUCCESS_WITH_INFO && length == 2);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, octets, 1, &length) == SQL_SUCCESS && octets[0] == 2U);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, NULL, 0, &length) == SQL_SUCCESS_WITH_INFO);
    char text[ODBC_TEXT_BYTES];
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp(text, "0102") == 0);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, NULL, 0, &length) == SQL_NO_DATA);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_BINARY, octets, (SQLLEN)sizeof octets, &length) == SQL_SUCCESS);
    TRL_CHECK(test, length == 4 && octets[2] == 0xC3U);
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_SLONG, octets, 0, NULL) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "22002") == 0);
    TRL_CHECK(test, SQLGetData(o.stmt, 4, SQL_C_CHAR, NULL, 0, &length) == SQL_SUCCESS);
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_SLONG, octets, 0, &length) == SQL_SUCCESS && length == SQL_NULL_DATA);
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_SLONG, octets, 0, &length) == SQL_NO_DATA);
    TRL_CHECK(test, SQLGetData(o.stmt, 4, SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) == SQL_SUCCESS);
    TRL_CHECK(test, length == 0 && text[0] == '\0');
    TRL_CHECK(test, SQLGetData(o.stmt, 4, SQL_C_CHAR, NULL, 0, &length) == SQL_NO_DATA);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, NULL, 0, &length) == SQL_SUCCESS_WITH_INFO && length == 4);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, NULL, 0, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLGetData(o.stmt, 5, SQL_C_CHAR, text, 1, &length) == SQL_ERROR);
    TRL_CHECK(test, SQLGetData(o.stmt, 0, SQL_C_CHAR, text, 1, &length) == SQL_ERROR);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, text, 1, &length) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "24000") == 0);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 large objects, one sent whole and one by its
 * locator, given as text, SQLWCHAR text and octets a piece at a time; a
 * large object as a number refused (07006).
 */
static void large_objects_are_given_in_pieces(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE L (C CLOB, B BLOB)"));
    char *long_text = malloc(LONG_BYTES + 1U);
    TRL_CHECK(test, long_text != NULL);
    if (long_text == NULL) {
        odbc_close(&o);
        return;
    }
    memset(long_text, 'y', LONG_BYTES);
    long_text[LONG_BYTES] = '\0';
    SQLLEN nts = SQL_NTS;
    SQLLEN octets_length = 3;
    unsigned char three[3] = {1U, 2U, 3U};
    TRL_CHECK(test, SQLPrepare(o.stmt, sql_text("INSERT INTO L VALUES (?, ?)"), SQL_NTS) == SQL_SUCCESS);
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_LONGVARCHAR, LONG_BYTES, 0, long_text, 0, &nts);
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_LONGVARBINARY, 3, 0, three, 3, &octets_length);
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecute(o.stmt)) == SQL_SUCCESS);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    TRL_CHECK(test, one_row(o.stmt, "SELECT C, B, CAST('short' AS CLOB) FROM L"));
    char piece[1000];
    SQLLEN length = 0;
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, length == (SQLLEN)LONG_BYTES && piece[998] == 'y' && piece[999] == '\0');
    unsigned char octets[2];
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, octets, 2, &length) == SQL_SUCCESS_WITH_INFO && length == 3);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, octets, 2, &length) == SQL_SUCCESS && octets[0] == 3U);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, octets, 2, &length) == SQL_NO_DATA);
    SQLWCHAR wide[8];
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length) == SQL_SUCCESS);
    TRL_CHECK(test, wide[0] == 's' && wide[5] == 0);
    TRL_CHECK(test, SQLGetData(o.stmt, 3, SQL_C_SLONG, octets, 0, &length) == SQL_ERROR);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_BINARY, NULL, 0, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, NULL, 0, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLGetData(o.stmt, 1, SQL_C_WCHAR, wide, (SQLLEN)sizeof wide, &length) == SQL_SUCCESS_WITH_INFO);
    TRL_CHECK(test, SQLGetData(o.stmt, 2, SQL_C_CHAR, piece, (SQLLEN)sizeof piece, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp(piece, "010203") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    free(long_text);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE L CASCADE"));
    odbc_close(&o);
}

/* A row of each C type a parameter may be. */
typedef struct parameters {
    signed char tiny;
    unsigned char utiny;
    short small;
    unsigned short usmall;
    int32_t integer;
    uint32_t uinteger;
    int64_t big;
    uint64_t ubig;
    float single;
    TIME_STRUCT time;
    DATE_STRUCT date;
    SQL_NUMERIC_STRUCT numeric;
    SQLGUID guid;
    SQLWCHAR wide[4];
} parameters;

/* Binds each parameter of `p` to the statement's `?` marks in order. */
static void bound(SQLHSTMT stmt, parameters *p, SQLLEN *wide_length) {
    static const SQLSMALLINT types[] = {
        SQL_C_STINYINT, SQL_C_UTINYINT, SQL_C_SSHORT,    SQL_C_USHORT, SQL_C_SLONG,   SQL_C_ULONG, SQL_C_SBIGINT,
        SQL_C_UBIGINT,  SQL_C_FLOAT,    SQL_C_TYPE_TIME, SQL_C_DATE,   SQL_C_NUMERIC, SQL_C_GUID,  SQL_C_WCHAR,
    };
    void *values[] = {
        &p->tiny, &p->utiny,  &p->small, &p->usmall, &p->integer, &p->uinteger, &p->big,
        &p->ubig, &p->single, &p->time,  &p->date,   &p->numeric, &p->guid,     p->wide,
    };
    for (SQLUSMALLINT i = 0U; i < (SQLUSMALLINT)(sizeof types / sizeof types[0]); ++i) {
        SQLLEN *length = types[i] == SQL_C_WCHAR ? wide_length : NULL;
        (void)SQLBindParameter(stmt, (SQLUSMALLINT)(i + 1U), SQL_PARAM_INPUT, types[i], SQL_VARCHAR, 40, 0, values[i],
                               0, length);
    }
}

/* Gives each parameter of `p` its value. */
static void parameters_set(parameters *p) {
    memset(p, 0, sizeof *p);
    p->tiny = -1;
    p->utiny = 2U;
    p->small = -3;
    p->usmall = 4U;
    p->integer = -5;
    p->uinteger = 6U;
    p->big = -7;
    p->ubig = 8U;
    p->single = 9.5F;
    p->time.hour = 10U;
    p->date.year = 2011;
    p->date.month = 12U;
    p->date.day = 13U;
    p->numeric.sign = 0U;
    p->numeric.scale = 1;
    p->numeric.val[0] = 145U; /* -14.5 */
    p->guid.Data1 = 0x12345678U;
    p->wide[0] = 'w';
}

/* Prepares on `stmt` a row of the PARAMETERS parameters, each as text; whether it could. */
static bool prepared_with_parameters(SQLHSTMT stmt) {
    char sql[ODBC_TEXT_BYTES * 4] = "VALUES (CAST(? AS VARCHAR(40))";
    for (size_t i = 1U; i < PARAMETERS; ++i) {
        size_t at = strlen(sql);
        trl_format(sql + at, sizeof sql - at, "%s", ", CAST(? AS VARCHAR(40))");
    }
    size_t end = strlen(sql);
    trl_format(sql + end, sizeof sql - end, "%s", ")");
    return SQLPrepare(stmt, (SQLCHAR *)sql, SQL_NTS) == SQL_SUCCESS;
}

/**
 * Verifies: [DESIGN] D2 a parameter of each C type sent as its value: the
 * integers as exact numbers, a float, a time and ODBC 2's date as their
 * literals, SQL_C_NUMERIC negative, SQLGUID as a UUID, SQLWCHAR text.
 */
static void parameters_of_each_c_type_are_sent(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    parameters p;
    parameters_set(&p);
    SQLLEN wide_length = (SQLLEN)sizeof(SQLWCHAR);
    TRL_CHECK(test, prepared_with_parameters(o.stmt));
    bound(o.stmt, &p, &wide_length);
    TRL_CHECK(test, odbc_said(o.stmt, SQLExecute(o.stmt)) == SQL_SUCCESS && SQLFetch(o.stmt) == SQL_SUCCESS);
    char text[ODBC_TEXT_BYTES];
    SQLLEN length = 0;
    static const char *const expected[] = {
        "-1",
        "2",
        "-3",
        "4",
        "-5",
        "6",
        "-7",
        "8",
        "9.5",
        "10:00:00",
        "2011-12-13",
        "-14.5",
        "12345678-0000-0000-0000-000000000000",
        "w",
    };
    bool every = true;
    for (SQLUSMALLINT i = 0U; i < (SQLUSMALLINT)(sizeof expected / sizeof expected[0]); ++i) {
        every = every && SQLGetData(o.stmt, (SQLUSMALLINT)(i + 1U), SQL_C_CHAR, text, (SQLLEN)sizeof text, &length) ==
                             SQL_SUCCESS;
        every = every && strstr(text, expected[i]) != NULL;
    }
    TRL_CHECK(test, every);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a parameter refused: data at execution (HYC00), a
 * C type the driver does not take (HY003), a null value's pointer
 * (HY009), a value not bound (07002), a streamed output parameter
 * (HYC00), one of no kind ODBC has (HY105) or one numbered 0 (07009).
 */
static void parameters_are_refused(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, prepared_with_parameters(o.stmt));
    parameters p;
    parameters_set(&p);
    SQLLEN wide_length = (SQLLEN)sizeof(SQLWCHAR);
    bound(o.stmt, &p, &wide_length);
    char text[ODBC_TEXT_BYTES] = "";
    SQLLEN at_exec = SQL_DATA_AT_EXEC;
    (void)SQLBindParameter(o.stmt, 14, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 4, 0, text, 0, &at_exec);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HYC00") == 0);
    (void)SQLBindParameter(o.stmt, 14, SQL_PARAM_INPUT, SQL_C_INTERVAL_DAY, SQL_VARCHAR, 4, 0, text, 0, NULL);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HY003") == 0);
    (void)SQLBindParameter(o.stmt, 14, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 4, 0, NULL, 0, NULL);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HY009") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    TRL_CHECK(test, SQLExecute(o.stmt) == SQL_ERROR && strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "07002") == 0);
    TRL_CHECK(test, SQLBindParameter(o.stmt, 1, SQL_PARAM_OUTPUT_STREAM, SQL_C_CHAR, SQL_VARCHAR, 4, 0, text, 0,
                                     NULL) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HYC00") == 0);
    TRL_CHECK(test, SQLBindParameter(o.stmt, 1, 99, SQL_C_CHAR, SQL_VARCHAR, 4, 0, text, 0, NULL) == SQL_ERROR &&
                        strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "HY105") == 0);
    TRL_CHECK(test,
              SQLBindParameter(o.stmt, 0, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 4, 0, text, 0, NULL) == SQL_ERROR);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 parameters bound row-wise at an offset, and a
 * binary string's octets of a length; a statement run directly with
 * parameters bound prepared, run and destroyed, a failure to prepare it
 * the server's.
 */
static void parameters_bound_at_an_offset_and_run_directly(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    struct row {
        int32_t key;
        SQLLEN key_length;
        unsigned char octets[2];
        SQLLEN octets_length;
    } rows[2] = {{1, 0, {9U, 8U}, 1}, {2, 0, {7U, 6U}, 2}};
    SQLLEN offset = (SQLLEN)sizeof rows[0];
    // NOLINTNEXTLINE(performance-no-int-to-ptr): ODBC passes an attribute's number as a pointer.
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_PARAM_BIND_TYPE, (SQLPOINTER)sizeof rows[0], 0);
    (void)SQLSetStmtAttr(o.stmt, SQL_ATTR_PARAM_BIND_OFFSET_PTR, &offset, 0);
    (void)SQLBindParameter(o.stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &rows[0].key, 0,
                           &rows[0].key_length);
    (void)SQLBindParameter(o.stmt, 2, SQL_PARAM_INPUT, SQL_C_BINARY, SQL_VARBINARY, 2, 0, rows[0].octets, 2,
                           &rows[0].octets_length);
    TRL_CHECK(test,
              odbc_said(o.stmt, SQLExecDirect(o.stmt, sql_text("VALUES (CAST(? AS INTEGER), CAST(? AS VARBINARY(2)))"),
                                              SQL_NTS)) == SQL_SUCCESS);
    int32_t key = 0;
    char hex[8];
    SQLLEN length = 0;
    TRL_CHECK(test,
              SQLFetch(o.stmt) == SQL_SUCCESS && SQLGetData(o.stmt, 1, SQL_C_SLONG, &key, 0, NULL) == SQL_SUCCESS);
    TRL_CHECK(test, key == 2 && SQLGetData(o.stmt, 2, SQL_C_CHAR, hex, (SQLLEN)sizeof hex, &length) == SQL_SUCCESS);
    TRL_CHECK(test, strcmp(hex, "0706") == 0);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_RESET_PARAMS);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT FROM"), SQL_NTS) == SQL_ERROR);
    TRL_CHECK(test, strcmp(odbc_state(SQL_HANDLE_STMT, o.stmt, NULL), "42000") == 0);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)2, 0) == SQL_ERROR);
    TRL_CHECK(test, SQLSetStmtAttr(o.stmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)1, 0) == SQL_SUCCESS);
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a column bound of each C type fetched into it,
 * and of SQL_C_DEFAULT the C type of its SQL type; a column bound but not
 * of the result passed over.
 */
static void columns_of_each_c_type_are_bound(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE B (S SMALLINT, I INTEGER, G BIGINT, R REAL, D DOUBLE PRECISION, "
                                     "F FLOAT, BO BOOLEAN, BI VARBINARY(4), DA DATE, TI TIME, TS TIMESTAMP, U UUID, "
                                     "C VARCHAR(4))"));
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO B VALUES (1, 2, 3, 4.5, 5.5, 6.5, TRUE, X'01', DATE '2020-01-01', "
                                     "TIME '01:02:03', TIMESTAMP '2020-01-01 01:02:03', CAST('12345678-9abc-def0-1234-"
                                     "56789abcdef0' AS UUID), 'c')"));
    static const SQLSMALLINT c_types[] = {
        SQL_C_STINYINT, SQL_C_TINYINT, SQL_C_UTINYINT, SQL_C_BIT,    SQL_C_SSHORT,
        SQL_C_SHORT,    SQL_C_USHORT,  SQL_C_SLONG,    SQL_C_LONG,   SQL_C_ULONG,
        SQL_C_FLOAT,    SQL_C_SBIGINT, SQL_C_UBIGINT,  SQL_C_DOUBLE, SQL_C_NUMERIC,
    };
    unsigned char buffers[sizeof c_types / sizeof c_types[0]][ODBC_TEXT_BYTES];
    SQLLEN lengths[sizeof c_types / sizeof c_types[0]];
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT S, S, S, S, S, S, S, S, S, S, S, S, S, S, S FROM B"),
                                  SQL_NTS) == SQL_SUCCESS);
    for (SQLUSMALLINT i = 0U; i < (SQLUSMALLINT)(sizeof c_types / sizeof c_types[0]); ++i) {
        (void)SQLBindCol(o.stmt, (SQLUSMALLINT)(i + 1U), c_types[i], buffers[i], ODBC_TEXT_BYTES, &lengths[i]);
    }
    (void)SQLBindCol(o.stmt, 20, SQL_C_CHAR, buffers[0], ODBC_TEXT_BYTES, &lengths[0]);
    TRL_CHECK(test, odbc_said(o.stmt, SQLFetch(o.stmt)) == SQL_SUCCESS && buffers[0][0] == 1U && lengths[13] == 8);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT S, I, G, R, D, F, BO, BI, DA, TI, TS, U, C FROM B"),
                                  SQL_NTS) == SQL_SUCCESS);
    for (SQLUSMALLINT i = 0U; i < 13U; ++i) {
        (void)SQLBindCol(o.stmt, (SQLUSMALLINT)(i + 1U), SQL_C_DEFAULT, buffers[i], ODBC_TEXT_BYTES, &lengths[i]);
    }
    TRL_CHECK(test, odbc_said(o.stmt, SQLFetch(o.stmt)) == SQL_SUCCESS);
    TRL_CHECK(test, lengths[0] == 2 && lengths[1] == 4 && lengths[2] == 8 && lengths[6] == 1);
    TRL_CHECK(test, lengths[8] == (SQLLEN)sizeof(DATE_STRUCT) && lengths[11] == (SQLLEN)sizeof(SQLGUID));
    TRL_CHECK(test, lengths[12] == 1 && buffers[12][0] == 'c');
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE B CASCADE"));
    odbc_close(&o);
}

/**
 * Verifies: [DESIGN] D2 a datetime column bound as ODBC 2's C types, as
 * their ODBC 3 names are; the cursor's row number, escape scan and
 * rowset size, ODBC 2's, read once fetched.
 */
static void columns_of_odbc_2_datetime_types_are_bound(trl_test *test) {
    odbc_opened o;
    TRL_CHECK(test, odbc_open(&o));
    TRL_CHECK(test, odbc_run(o.stmt, "CREATE TABLE O (DA DATE, TI TIME, TS TIMESTAMP)"));
    TRL_CHECK(test, odbc_run(o.stmt, "INSERT INTO O VALUES (DATE '2020-01-01', TIME '01:02:03', "
                                     "TIMESTAMP '2020-01-01 01:02:03')"));
    unsigned char buffers[3][ODBC_TEXT_BYTES];
    SQLLEN lengths[3];
    TRL_CHECK(test, SQLExecDirect(o.stmt, sql_text("SELECT DA, TI, TS FROM O"), SQL_NTS) == SQL_SUCCESS);
    static const SQLSMALLINT older[] = {SQL_C_DATE, SQL_C_TIME, SQL_C_TIMESTAMP};
    for (SQLUSMALLINT i = 0U; i < 3U; ++i) {
        (void)SQLBindCol(o.stmt, (SQLUSMALLINT)(i + 1U), older[i], buffers[i], ODBC_TEXT_BYTES, &lengths[i]);
    }
    TRL_CHECK(test, odbc_said(o.stmt, SQLFetch(o.stmt)) == SQL_SUCCESS);
    TRL_CHECK(test, lengths[0] == (SQLLEN)sizeof(DATE_STRUCT) && lengths[1] == (SQLLEN)sizeof(TIME_STRUCT) &&
                        lengths[2] == (SQLLEN)sizeof(TIMESTAMP_STRUCT));
    SQLULEN number = 0U;
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ATTR_ROW_NUMBER, &number, 0, NULL) == SQL_SUCCESS && number == 1U);
    TRL_CHECK(test,
              SQLGetStmtAttr(o.stmt, SQL_ATTR_NOSCAN, &number, 0, NULL) == SQL_SUCCESS && number == SQL_NOSCAN_OFF);
    TRL_CHECK(test, SQLGetStmtAttr(o.stmt, SQL_ROWSET_SIZE, &number, 0, NULL) == SQL_SUCCESS && number == 1U);
    (void)SQLFreeStmt(o.stmt, SQL_CLOSE);
    (void)SQLFreeStmt(o.stmt, SQL_UNBIND);
    TRL_CHECK(test, odbc_run(o.stmt, "DROP TABLE O CASCADE"));
    odbc_close(&o);
}

int main(int argc, char **argv) {
    bool skip = false;
    if (!odbc_start("live-odbc-types", &skip)) {
        return skip ? SKIPPED : EXIT_FAILURE;
    }
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(numbers_are_given_as_c_integers_and_floats),
        TRL_TEST_CASE(numbers_past_a_c_type_are_refused),
        TRL_TEST_CASE(numbers_at_the_c_types_edges),
        TRL_TEST_CASE(numbers_are_given_as_sql_numeric),
        TRL_TEST_CASE(datetimes_and_uuids_are_given_as_structures),
        TRL_TEST_CASE(strings_are_given_in_pieces),
        TRL_TEST_CASE(large_objects_are_given_in_pieces),
        TRL_TEST_CASE(parameters_of_each_c_type_are_sent),
        TRL_TEST_CASE(parameters_are_refused),
        TRL_TEST_CASE(parameters_bound_at_an_offset_and_run_directly),
        TRL_TEST_CASE(columns_of_each_c_type_are_bound),
        TRL_TEST_CASE(columns_of_odbc_2_datetime_types_are_bound),
    };
    int run_result = trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
    odbc_stop();
    return run_result;
}
