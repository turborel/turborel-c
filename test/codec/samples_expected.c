/**
 * @file samples_expected.c
 * @brief The reply of each frame a server sends among the sample frames,
 * as reply_text() writes it, each read from the sample's meaning
 * [PROTOCOL Sample frames]: a type as trl_type_to_text() writes it and,
 * in brackets, what that leaves out; a value as its tag, a colon and its
 * text, then what its text leaves out.
 */
#include "samples.h"

#include <stddef.h>

const sample_reply sample_replies[] = {
    {"challenge", "Challenge r=abcdef,s=c2FsdA==,i=4096"},
    {"ready", "Ready ANN key=A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5 capabilities=ping server=turborel 0.0.0 (alpha)"},
    {
        "prepared",
        "Prepared 1 parameters (varying CHARACTER VARYING(32767); NUMERIC(10,0)) columns (K NUMERIC(10,0)"
        " not null; ? CHARACTER VARYING(5))",
    },
    {"prepared-empty", "Prepared 2 parameters () columns ()"},
    {
        "prepared-called",
        "Prepared 3 parameters (NUMERIC(10,0)) columns (G NUMERIC(10,0) not null) called S.P (IN 1 NUMERIC(10,0);"
        " OUT 2 NUMERIC(10,0))",
    },
    {
        "prepared-declared",
        "Prepared 1 parameters (varying CHARACTER VARYING(32767); NUMERIC(10,0)) columns (K NUMERIC(10,0)"
        " not null declared INTEGER/INTEGER)",
    },
    {"description", "Description (K NUMERIC(10,0) not null; ? CHARACTER VARYING(5))"},
    {"description-empty", "Description ()"},
    {
        "description-declared",
        "Description (I NUMERIC(10,0) not null declared INTEGER/INTEGER; S NUMERIC(5,0) declared SMALLINT"
        "/SMALLINT; B NUMERIC(18,0) declared BIGINT/BIGINT; D NUMERIC(9,2) declared DECIMAL/DECIMAL(9,2);"
        " R FLOAT(24) declared REAL/REAL; P FLOAT(53) declared DOUBLE PRECISION/DOUBLE PRECISION; M NUMER"
        "IC(9,2) declared USER-DEFINED/S.MONEY)",
    },
    {"rows", "Rows (2:1, 1:a) | (0:NULL, 1:)"},
    {"rows-none", "Rows"},
    {"rows-empty-row", "Rows ()"},
    {"suspended", "Suspended"},
    {"complete", "Complete 0 00000 inactive"},
    {"complete-active", "Complete 0 00000 active"},
    {"complete-no-data", "Complete 100 02000 inactive"},
    {"complete-position", "Complete 0 00000 inactive position=4711"},
    {"complete-more", "Complete 0 00000 inactive more"},
    {"complete-rows", "Complete 0 00000 inactive rows=1"},
    {"complete-every", "Complete 0 00000 inactive position=4711 more rows=3 generated=7,8 time=1500/20/300"},
    {"complete-conditions", "Complete 0 01004 active condition=[0 01004 'string data, right truncation' active]"},
    {"error", "Error -803 23505 'a unique constraint is broken' inactive"},
    {"error-cited", "Error -803 23505 'a unique constraint is broken' rule='ISO/IEC 9075-2:2023 §11.7' at=12 active"},
    {
        "error-diagnostics",
        "Error -803 23505 'a unique constraint is broken' rule='ISO/IEC 9075-2:2023 §11.7' at=12 active 7"
        "=ERROR 8=key (K)=(1) 9=choose another K 10=trigger S.TR 11=S 12=T 13=K 14=INTEGER 15=T_PK",
    },
    {"error-connection", "Error -919 08006 'a malformed message' inactive"},
    {"lob-data", "Lob data 61C3A9"},
    {"lob-data-empty", "Lob data "},
    {"lob-locator", "Lob locator 4"},
    {"notice", "Notice 01004 'string data, right truncation'"},
    {"notice-diagnostics", "Notice 01004 'string data, right truncation' 7=WARNING 8=cut to 4"},
    {"pong", "Pong"},
    {"status-server-version", "Status SERVER_VERSION=turborel 0.0.0 (alpha)"},
    {"status-authorization", "Status AUTHORIZATION=ANN"},
    {"status-schema", "Status SCHEMA=ANN"},
    {"status-read-only", "Status READ_ONLY=NO"},
    {"status-server-read-only", "Status SERVER_READ_ONLY=YES"},
    {"status-catalog", "Status CATALOG=TURBOREL"},
    {"status-owner", "Status OWNER=YES"},
    {"status-time-zone", "Status SESSION.TIME_ZONE=+05:30"},
    {"status-null", "Status STATEMENT.TIMEOUT_MS="},
    {
        "rows-compressed",
        "Rows (2:0, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:1, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:"
        "2, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:3, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:4, 1:abc"
        "defghabcdefghabcdefghabcdefgh) | (2:5, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:6, 1:abcdefghabc"
        "defghabcdefghabcdefgh) | (2:7, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:8, 1:abcdefghabcdefghabc"
        "defghabcdefgh) | (2:9, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:10, 1:abcdefghabcdefghabcdefghab"
        "cdefgh) | (2:11, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:12, 1:abcdefghabcdefghabcdefghabcdefgh"
        ") | (2:13, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:14, 1:abcdefghabcdefghabcdefghabcdefgh) | (2"
        ":15, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:16, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:17, 1"
        ":abcdefghabcdefghabcdefghabcdefgh) | (2:18, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:19, 1:abcde"
        "fghabcdefghabcdefghabcdefgh) | (2:20, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:21, 1:abcdefghabc"
        "defghabcdefghabcdefgh) | (2:22, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:23, 1:abcdefghabcdefgha"
        "bcdefghabcdefgh) | (2:24, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:25, 1:abcdefghabcdefghabcdefg"
        "habcdefgh) | (2:26, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:27, 1:abcdefghabcdefghabcdefghabcde"
        "fgh) | (2:28, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:29, 1:abcdefghabcdefghabcdefghabcdefgh) |"
        " (2:30, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:31, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:32"
        ", 1:abcdefghabcdefghabcdefghabcdefgh) | (2:33, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:34, 1:ab"
        "cdefghabcdefghabcdefghabcdefgh) | (2:35, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:36, 1:abcdefgh"
        "abcdefghabcdefghabcdefgh) | (2:37, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:38, 1:abcdefghabcdef"
        "ghabcdefghabcdefgh) | (2:39, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:40, 1:abcdefghabcdefghabcd"
        "efghabcdefgh) | (2:41, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:42, 1:abcdefghabcdefghabcdefghab"
        "cdefgh) | (2:43, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:44, 1:abcdefghabcdefghabcdefghabcdefgh"
        ") | (2:45, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:46, 1:abcdefghabcdefghabcdefghabcdefgh) | (2"
        ":47, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:48, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:49, 1"
        ":abcdefghabcdefghabcdefghabcdefgh) | (2:50, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:51, 1:abcde"
        "fghabcdefghabcdefghabcdefgh) | (2:52, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:53, 1:abcdefghabc"
        "defghabcdefghabcdefgh) | (2:54, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:55, 1:abcdefghabcdefgha"
        "bcdefghabcdefgh) | (2:56, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:57, 1:abcdefghabcdefghabcdefg"
        "habcdefgh) | (2:58, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:59, 1:abcdefghabcdefghabcdefghabcde"
        "fgh) | (2:60, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:61, 1:abcdefghabcdefghabcdefghabcdefgh) |"
        " (2:62, 1:abcdefghabcdefghabcdefghabcdefgh) | (2:63, 1:abcdefghabcdefghabcdefghabcdefgh)",
    },
    {"description-character", "Description (C CHARACTER(4))"},
    {"description-character-octets", "Description (C CHARACTER(16 OCTETS))"},
    {"description-exact", "Description (C NUMERIC(9,2))"},
    {"description-approximate", "Description (C FLOAT(53))"},
    {"description-plugin-type", "Description (C UNREGISTERED [ST_GEOMETRY of 0 parameters])"},
    {"description-varying", "Description (C CHARACTER VARYING(20))"},
    {"description-binary", "Description (C BINARY(16))"},
    {"description-binary-varying", "Description (C BINARY VARYING(255))"},
    {"description-date", "Description (C DATE)"},
    {"description-time", "Description (C TIME(3))"},
    {"description-time-zoned", "Description (C TIME(0) WITH TIME ZONE)"},
    {"description-timestamp", "Description (C TIMESTAMP(6))"},
    {"description-timestamp-zoned", "Description (C TIMESTAMP(6) WITH TIME ZONE)"},
    {"description-interval-year-month", "Description (C INTERVAL YEAR(4) TO MONTH)"},
    {"description-interval-day-second", "Description (C INTERVAL DAY(2) TO SECOND(6))"},
    {"description-boolean", "Description (C BOOLEAN)"},
    {"description-decfloat", "Description (C DECFLOAT(34))"},
    {"description-clob", "Description (C CHARACTER LARGE OBJECT(1048576) [units 0])"},
    {"description-blob", "Description (C BINARY LARGE OBJECT(65536) [units 1])"},
    {"description-json", "Description (C JSON)"},
    {"description-uuid", "Description (C UUID)"},
    {"description-array", "Description (C BOOLEAN ARRAY[3])"},
    {"description-row", "Description (C ROW(X CHARACTER VARYING(3), Y BOOLEAN))"},
    {"description-multiset", "Description (C CHARACTER(2) MULTISET)"},
    {"description-structured", "Description (C S.SHAPE [S.SHAPE under S.FIGURE])"},
    {"description-reference", "Description (C REF(S.SHAPE) [S.SHAPE under S.FIGURE])"},
    {"description-reference-scoped", "Description (C REF(S.SHAPE) SCOPE S.SHAPES [S.SHAPE under S.FIGURE scoped])"},
    {"description-xml-document", "Description (C XML(DOCUMENT(UNTYPED)))"},
    {"description-xml-content", "Description (C XML(CONTENT(ANY)))"},
    {"description-xml-sequence", "Description (C XML(SEQUENCE))"},
    {"description-xml-schema", "Description (C XML(DOCUMENT(XMLSCHEMA ID S.PO NAMESPACE 'urn:po')))"},
    {"description-distinct", "Description (C CHARACTER VARYING(4))"},
    {"description-declared-array", "Description (C NUMERIC(10,0) ARRAY[3] declared ARRAY/INTEGER ARRAY[3])"},
    {"value-null", "Rows (0:NULL)"},
    {"value-character", "Rows (1:abc)"},
    {"value-character-empty", "Rows (1:)"},
    {"value-character-utf8", "Rows (1:é€)"},
    {"value-exact", "Rows (2:123.45)"},
    {"value-exact-negative", "Rows (2:-1)"},
    {"value-exact-largest", "Rows (2:170141183460469231731687303715884105727)"},
    {"value-approximate", "Rows (3:1.5E0 bits=3FF8000000000000)"},
    {"value-approximate-negative-zero", "Rows (3:-0E0 bits=8000000000000000)"},
    {
        "value-plugin",
        "Rows (4:POINT (1 2) type=ST_GEOMETRY bytes=1A00000001E61000000101000000000000000000F03F000000000"
        "0000040)",
    },
    {"value-binary", "Rows (6:DEADBEEF)"},
    {"value-binary-empty", "Rows (6:)"},
    {"value-date", "Rows (8:2024-02-29 zone=0 precision=0 utc=63844761600000000)"},
    {"value-time", "Rows (9:12:34:56.5 zone=0 precision=1 utc=45296500000)"},
    {"value-time-zoned", "Rows (9:12:34:56+05:30 zoned zone=330 precision=0 utc=25496000000)"},
    {"value-timestamp", "Rows (11:2024-02-29 12:34:56.123456 zone=0 precision=6 utc=63844806896123456)"},
    {"value-timestamp-zoned", "Rows (11:2024-02-29 12:34:56-08:00 zoned zone=-480 precision=0 utc=63844835696000000)"},
    {"value-timestamp-earliest", "Rows (11:0001-01-01 00:00:00 zone=0 precision=0 utc=0)"},
    {"value-interval-year-month", "Rows (13:-1-02 qualifier=0/1/2/0 units=-14)"},
    {"value-interval-day-second", "Rows (13:1 02:03:04.500000 qualifier=2/5/2/6 units=93784500000)"},
    {"value-true", "Rows (14:TRUE)"},
    {"value-false", "Rows (14:FALSE)"},
    {"value-decfloat", "Rows (15:-1.5 coefficient=-15 exponent=-1)"},
    {"value-array", "Rows (16:ARRAY[1, NULL])"},
    {"value-array-empty", "Rows (16:ARRAY[])"},
    {"value-row", "Rows (17:ROW('x', TRUE))"},
    {"value-lob-character", "Rows (20:aé kind=0)"},
    {"value-lob-binary", "Rows (20:00FF kind=1)"},
    {
        "value-lob-locator",
        "Rows (21:<CHARACTER LARGE OBJECT of 200000 characters> kind=0 locator=7 octets=300000 characters"
        "=200000)",
    },
    {"value-multiset", "Rows (22:MULTISET['a', 'a'])"},
    {"value-structured", "Rows (23:S.SHAPE(3, NULL))"},
    {"value-reference", "Rows (24:REF(0102030405060708))"},
    {"value-json", "Rows (25:{\"a\":[1,\"x\"]} stored=51)"},
    {"value-json-scalars", "Rows (25:[null,true,false,-2,1.50,\"\",{}] stored=74)"},
    {"value-xml", "Rows (26:<a>&lt;</a>)"},
    {"value-uuid", "Rows (27:00112233-4455-6677-8899-aabbccddeeff)"},
    {"debugged-connection", "Debugged connection 2"},
    {"debugged-breakpoint", "Debugged breakpoint 1 ANN.RAISE_PAY line 4"},
    {"debugged-breakpoints", "Debugged breakpoints (1 ANN.RAISE_PAY line 4)"},
    {"debugged-running", "Debugged running"},
    {
        "debugged-stack",
        "Debugged stack breakpoint 1 frames (procedure ANN.RAISE_PAY/ANN.RAISE_PAY visible place 1 line 4:3 parameters"
        " (PCT INTEGER 2:10) variables (N INTEGER 0:NULL) cursors (C closed 'SELECT ID FROM STAFF'))",
    },
    {
        "event-paused",
        "Event paused breakpoint 1 frames (procedure ANN.RAISE_PAY/ANN.RAISE_PAY visible place 1 line 4:3 parameters"
        " (PCT INTEGER 2:10) variables (N INTEGER 0:NULL) cursors (C closed 'SELECT ID FROM STAFF'))",
    },
    {"event-timed-out", "Event timed out"},
    {"event-cancelled", "Event cancelled"},
    {"event-ended", "Event ended"},
};

const size_t sample_reply_count = sizeof sample_replies / sizeof sample_replies[0];
