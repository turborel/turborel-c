# The ODBC Driver

[Contents](README.md)

## NAME

`libturborelodbc`: the ODBC 3.x driver for Turborel, a module a driver
manager loads.

## SYNOPSIS

```
#include <sql.h>
#include <sqlext.h>

cc app.c -lodbc            # unixODBC
cc app.c -liodbc           # iODBC
cl app.c odbc32.lib        # Windows
```

A program links its driver manager, never the driver. It connects by a
data source's name,

```
SQLConnect(dbc, (SQLCHAR *)"reports", SQL_NTS, NULL, 0, NULL, 0);
```

or by a connection string that names the driver:

```
DRIVER={Turborel};SERVER=db.example.com;PORT=7422;UID=ann;PWD=...;TLS_MODE=require
```

The driver's entry points are those of Microsoft's *ODBC Programmer's
Reference*:

| Kind | Functions |
|---|---|
| Handles | `SQLAllocHandle`, `SQLFreeHandle`, `SQLFreeStmt`, `SQLSetEnvAttr`, `SQLGetEnvAttr` |
| Connecting | `SQLConnect`, `SQLDriverConnect`, `SQLDisconnect`, `SQLSetConnectAttr`, `SQLGetConnectAttr`, `SQLEndTran`, `SQLGetInfo`, `SQLGetFunctions`, `SQLNativeSql` |
| Running | `SQLExecDirect`, `SQLPrepare`, `SQLExecute`, `SQLBindParameter`, `SQLNumParams`, `SQLDescribeParam`, `SQLRowCount`, `SQLMoreResults`, `SQLCancel`, `SQLCloseCursor`, `SQLParamData`, `SQLPutData`, `SQLSetStmtAttr`, `SQLGetStmtAttr`, `SQLSetCursorName`, `SQLGetCursorName` |
| Results | `SQLNumResultCols`, `SQLDescribeCol`, `SQLColAttribute`, `SQLBindCol`, `SQLFetch`, `SQLFetchScroll`, `SQLGetData`, `SQLSetPos` |
| Descriptors | `SQLGetDescField`, `SQLSetDescField`, `SQLGetDescRec`, `SQLSetDescRec`, `SQLCopyDesc` |
| Catalog | `SQLTables`, `SQLColumns`, `SQLPrimaryKeys`, `SQLForeignKeys`, `SQLStatistics`, `SQLSpecialColumns`, `SQLProcedures`, `SQLProcedureColumns`, `SQLTablePrivileges`, `SQLColumnPrivileges`, `SQLGetTypeInfo` |
| Diagnostics | `SQLGetDiagRec`, `SQLGetDiagField` |

Each function that takes or gives text has a `W` form as well, such as
`SQLExecDirectW`.

## DESCRIPTION

The driver is built on the C API alone. A connection of the driver is a
connection of `libturborel`, and its statements run as
[Statements and Cursors](22-statements.md) describes. The library's
objects are linked into the module, and only ODBC's functions are
exported from it, so a program that also links `libturborel` keeps its
own copy.

### Building and installing

The driver is built with the library wherever ODBC's headers and the
installer library are found: unixODBC's or iODBC's on Linux and macOS,
and the Windows SDK's on Windows. On Debian and Ubuntu they come in the
package `unixodbc-dev`; on macOS, in Homebrew's `unixodbc`. The CMake
option `TRL_ODBC` asks for the driver and is ON unless set OFF. CMake's
output says whether the driver was built. `cmake --install` installs it
in the library directory, as `libturborelodbc.so`, or
`libturborelodbc.dll` on Windows.

### Registering the driver

A driver manager finds a driver by the name it is registered under.
unixODBC reads its drivers from `odbcinst.ini`, in the directory that
`odbcinst -j` prints:

```
[Turborel]
Description = Turborel's ODBC driver
Driver      = /usr/local/lib/libturborelodbc.so
```

`odbcinst -i -d -f turborel.ini` adds the same lines from a file. A data
source is a section of `odbc.ini`, either the system's or the user's
`~/.odbc.ini`. It names the driver and holds any of the connection keys
below:

```
[reports]
Driver   = Turborel
SERVER   = db.example.com
PORT     = 7422
UID      = ann
TLS_MODE = require
```

iODBC reads the same two files from its own places, and loads a driver
built with its own headers (see Text, below). On Windows the
driver is registered in the registry: under
`HKEY_LOCAL_MACHINE\SOFTWARE\ODBC\ODBCINST.INI\Turborel`, its value
`Driver` is the path of `libturborelodbc.dll`, and under
`HKEY_LOCAL_MACHINE\SOFTWARE\ODBC\ODBCINST.INI\ODBC Drivers` the value
`Turborel` is `Installed`. The driver has no setup dialog of its own, so
a data source is added under `HKEY_CURRENT_USER\SOFTWARE\ODBC\ODBC.INI`
with its keys as values, or the program connects by a connection
string.

### Connection keys

The keys of a connection string or a data source are the driver's
settings, by the names [Appendix C](../appendix/c-settings.md) gives
them, with ODBC's own names beside them. A key is read in any case. A
value that holds a `;` is set in braces, as in `PWD={a;b}`.

| Key | Gives |
|---|---|
| `DSN` | the data source whose keys are read |
| `DRIVER` | the driver; it is read by the driver manager, and the driver passes over it |
| `URI` | a `turborel://` URI, with the other keys laid over it |
| `UID`, `user` | the user |
| `PWD`, `password` | the password |
| `SERVER`, `host` | the server's host |
| `port` | the server's port |
| `tls_mode`, `channel_binding`, `server_name`, `tls_roots`, `tls_system_roots`, `tls_certificate`, `tls_key` | TLS, as [Settings](26-settings.md) describes |
| `application_name`, `connect_timeout` | the session's name, and how long connecting may take |
| `service`, `service_file`, `passfile` | a service's settings, and the password file |
| `target`, `load_balance` | several servers, and the order they are tried in |

Any other key, such as `DATABASE` or a key of another driver, is passed
over. A key in the connection string comes before the data source's,
which comes before the environment's settings and the defaults.
SQLConnect's user and password come before the data source's. The
connection attribute SQL_ATTR_LOGIN_TIMEOUT gives `connect_timeout` when
no key does.

SQLDriverConnect never shows a dialog, whatever its completion argument
asks. A string the driver cannot connect by fails, and the string it
gives back is the one it was given.

### Handles and attributes

An environment takes ODBC versions 2, 3 and 3.80. A connection starts in
autocommit. When SQL_ATTR_AUTOCOMMIT is set off, a transaction begins
with the first statement and ends with SQLEndTran, and SQLDisconnect
refuses to end a connection whose transaction is open (25000). Setting
autocommit back on commits an open transaction.

SQL_ATTR_TXN_ISOLATION takes each of ODBC's four levels, READ
UNCOMMITTED, READ COMMITTED, REPEATABLE READ and SERIALIZABLE, and
SQL_ATTR_ACCESS_MODE READ ONLY or READ WRITE. As each transaction
begins, before its first statement, the driver sends SET TRANSACTION
with the level and access mode the application set, so the server keeps
them for that transaction: a READ ONLY transaction's statement that
writes fails (25000), and a READ UNCOMMITTED transaction is READ ONLY
unless the server is told otherwise. A level not set is the session's,
READ COMMITTED unless the database's TRANSACTION.ISOLATION says
another. The level is not set while a transaction is open (HY011); the
access mode, set then, holds from the next transaction. SQLGetInfo
reports all four levels, READ COMMITTED the default, and
SQL_DATA_SOURCE_READ_ONLY the access mode. SQL_ATTR_CURRENT_CATALOG reports the session's catalog; setting
it is accepted and changes nothing. SQL_ATTR_CONNECTION_DEAD reports
whether the connection was lost.

A statement's cursor is forward-only or static, read-only and
insensitive. A request for a keyset-driven or dynamic cursor, for
another concurrency or sensitivity, or for bookmarks is changed to what
the driver has (01S02). The rowset size, binding by row or by column,
bind offsets, the row status and rows-fetched arrays, SQL_ATTR_MAX_ROWS
and SQL_ATTR_NOSCAN all work as ODBC describes. SQL_ATTR_QUERY_TIMEOUT
is the session's STATEMENT.TIMEOUT_MS while the statement runs, so the
server cancels a statement that runs past it, which then fails as its
timeout's (HYT00), the connection kept. A statement is also stopped by
SQLCancel from another thread, which cancels it on the server
[PROTOCOL Cancel 'Z'].

Each statement has its four implicit descriptors. A descriptor the
application allocates on the connection may take the place of a
statement's row or parameter descriptor. The implementation row
descriptor cannot be set (HY016), and an implicit descriptor cannot be
freed (HY017).

### Running statements

A statement runs directly, by SQLExecDirect, or is prepared once and run
many times, by SQLPrepare and SQLExecute. Its parameters are `?` marks,
bound by SQLBindParameter or set in the application's parameter
descriptor, and each C type is sent as the value it holds. Parameters are
input only, one set at a time: an output parameter, an array of
parameter sets, or data given at execution is refused (HYC00).
SQLDescribeParam reports the type the server gives a prepared
statement's parameter.

All of a query's rows are read when it runs. A static cursor therefore
scrolls over them in any direction by SQLFetchScroll, and SQLRowCount
reports how many rows a statement changed. A statement has one result:
SQLMoreResults returns SQL_NO_DATA. SQLSetPos and positioned updates are
refused (HYC00).

### Getting data

SQLDescribeCol and SQLColAttribute report each column's SQL type. Where
the server reports the column's declared type [PROTOCOL Declared types],
the type is found by that name, so a column declared INTEGER is
SQL_INTEGER:

| Turborel's type | SQL type |
|---|---|
| SMALLINT, INTEGER, BIGINT | SQL_SMALLINT, SQL_INTEGER, SQL_BIGINT |
| NUMERIC, DECIMAL | SQL_NUMERIC, SQL_DECIMAL |
| REAL, DOUBLE PRECISION, FLOAT | SQL_REAL, SQL_DOUBLE, SQL_FLOAT |
| CHARACTER, CHARACTER VARYING | SQL_CHAR, SQL_VARCHAR |
| CHARACTER LARGE OBJECT, JSON, XML, arrays, rows and multisets | SQL_LONGVARCHAR |
| BINARY, BINARY VARYING, BINARY LARGE OBJECT | SQL_BINARY, SQL_VARBINARY, SQL_LONGVARBINARY |
| BOOLEAN | SQL_BIT |
| DATE, TIME, TIMESTAMP | SQL_TYPE_DATE, SQL_TYPE_TIME, SQL_TYPE_TIMESTAMP |
| UUID | SQL_GUID |
| a datetime with a time zone, an interval, DECFLOAT | SQL_VARCHAR, holding its text |

SQLBindCol and SQLGetData give a value as the C type asked for, or, for
SQL_C_DEFAULT, as the C type of its SQL type:

- any value as SQL_C_CHAR, in UTF-8, or as SQL_C_WCHAR: its text, as
  [Values](23-values.md) writes it; a binary string's text is its octets
  in hexadecimal;
- any value as SQL_C_BINARY: a binary string's octets, or any other
  value's text;
- a number, a boolean, or a character string holding a number, as each
  integer C type, SQL_C_FLOAT, SQL_C_DOUBLE, SQL_C_BIT and SQL_C_NUMERIC,
  at the scale the descriptor asks for;
- a date, time or timestamp, or a character string holding one, as
  SQL_C_TYPE_DATE, SQL_C_TYPE_TIME and SQL_C_TYPE_TIMESTAMP, and as
  ODBC 2's SQL_C_DATE, SQL_C_TIME and SQL_C_TIMESTAMP;
- a UUID as SQL_C_GUID.

A number outside the C type's range is refused (22003), and a fraction
the C type cannot hold is dropped (01S07). Text that is not a number is
refused (22018). Text cut short to fit its buffer gives a warning
(01004), and further calls of SQLGetData give the rest in pieces. A large
object is read from the server when SQLGetData asks for it. A null value
needs an indicator to report it (22002).

### Text

The `W` functions take and give SQLWCHAR: UTF-16 where SQLWCHAR has two
bytes, as on Windows and with unixODBC, and UTF-32 where it has four, as
with iODBC. The size is that of the headers the driver is built with, so
a driver for iODBC is built with iODBC's headers and installer library,
named by the CMake variables `TRL_ODBC_INCLUDE_DIR` and
`TRL_ODBCINST_LIBRARY` where both managers are installed. The other
functions take and give UTF-8, whatever the process's locale. Each length
is in characters or bytes, as ODBC says for that argument.

### Escapes

Before a statement runs, its escapes are written as Turborel's SQL,
unless SQL_ATTR_NOSCAN is on. SQLNativeSql shows what they become.

| Escape | Becomes |
|---|---|
| `{d '2026-11-20'}`, `{t '...'}`, `{ts '...'}` | `DATE '2026-11-20'`, `TIME '...'`, `TIMESTAMP '...'` |
| `{interval '1' DAY}` | `INTERVAL '1' DAY` |
| `{oj A LEFT OUTER JOIN B ON ...}` | `A LEFT OUTER JOIN B ON ...` |
| `{call P(?)}` | `CALL P(?)` |
| `LIKE '...' {escape '\'}` | `LIKE '...' ESCAPE '\'` |
| `{fn UCASE(x)}`, `LCASE`, `LENGTH`, `IFNULL` | `UPPER(x)`, `LOWER`, `CHAR_LENGTH`, `COALESCE` |
| `{fn NOW()}`, `CURDATE()`, `CURTIME()`, `USER()` | `LOCALTIMESTAMP`, `CURRENT_DATE`, `LOCALTIME`, `CURRENT_USER` |
| `{fn CONCAT(a, b)}` | `(a \|\| b)` |
| `{fn LTRIM(x)}`, `{fn RTRIM(x)}` | `TRIM(LEADING FROM x)`, `TRIM(TRAILING FROM x)` |
| `{fn CONVERT(x, SQL_INTEGER)}` | `CAST(x AS INTEGER)`, with each SQL type written as Turborel's |
| `{fn F(...)}` for any other function | `F(...)` |

CONCAT, LTRIM, RTRIM and CONVERT given other arguments than those
shown, and NOW, CURDATE, CURTIME and USER given any, are written as they
are, as any other function is. Escapes may be nested. Text in quotes
and in comments is kept as it is.
An escape the driver does not know, or one that is not closed, is
refused (42000).

### Catalog functions

The catalog functions are queries of INFORMATION_SCHEMA, and their
result sets have the columns ODBC gives each one. A name argument is a
pattern, with `_` and `%`, as ODBC describes, unless
SQL_ATTR_METADATA_ID is on; then it is an identifier, folded to upper
case unless it is quoted. SQLTables lists the catalogs, the schemas or
the table types when called as ODBC describes for each. SQLGetTypeInfo
lists Turborel's types, mapped as in the table above.

### What the driver reports

SQLGetInfo reports the driver as `libturborelodbc`, of ODBC version
03.80, with interface conformance core and SQL conformance SQL-92 entry.
It reports the DBMS as `Turborel`, at the server's version.
SQLGetFunctions reports each function listed above.

## ERRORS

A failure of the server's is given with its own SQLSTATE, its SQLCODE as
the native error, and its message; [Appendix B](../appendix/b-errors.md)
describes each class. The driver's own failures, whose messages begin
`[Turborel][libturborelodbc]`, are:

| SQLSTATE | Meaning |
|---|---|
| 01002 | Disconnect error: the connection was not ended cleanly |
| 01004 | String data, right truncated |
| 01S02 | Option value changed |
| 01S07 | Fractional truncation |
| 07002 | A parameter has no value bound |
| 07006 | A value cannot be given as that C type |
| 07009 | Invalid descriptor index: a column or parameter past the last |
| 08002 | The connection is already open |
| 08003 | The connection is not open |
| 22002 | A null value, and no indicator |
| 22003 | A number outside the C type's range |
| 22018 | Text that is not a number, date or time |
| 24000 | Invalid cursor state |
| 25000 | Invalid transaction state: a transaction is open |
| 25S01 | Transaction state unknown: one of the environment's connections failed to end its transaction |
| 34000 | Invalid cursor name |
| 42000 | An escape the driver does not know, or one not closed |
| HY001 | Memory could not be had |
| HY003 | A C type the driver does not take |
| HY009 | A null pointer where none is allowed |
| HY010 | Function sequence error |
| HY011 | An isolation level set while a transaction is open |
| HY012 | Invalid transaction operation code |
| HY016 | The implementation row descriptor cannot be set |
| HY017 | An implicit descriptor cannot be freed |
| HY024 | Invalid attribute value |
| HY091 | Invalid descriptor field identifier |
| HY092 | Invalid attribute identifier |
| HY096 | Information type out of range |
| HY106 | Fetch type out of range, or a scroll of a forward-only cursor |
| HYT00 | Timeout expired: the statement ran past its query timeout |
| HYC00 | Optional feature not implemented: output parameters, arrays of parameters, data at execution, positioned operations |

## EXAMPLES

This program connects by the data source `reports`, creates a table,
adds rows by a prepared statement, and reads them back into bound
columns:

```c
#include <sql.h>
#include <sqlext.h>

#include <stdio.h>
#include <string.h>

static void diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
    SQLCHAR state[6];
    SQLINTEGER native = 0;
    SQLCHAR message[256];
    for (SQLSMALLINT i = 1;
         SQLGetDiagRec(type, handle, i, state, &native, message, sizeof message, NULL) == SQL_SUCCESS; ++i) {
        printf("%s (%d) %s\n", state, (int)native, message);
    }
}

int main(void) {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    if (!SQL_SUCCEEDED(SQLConnect(dbc, (SQLCHAR *)"reports", SQL_NTS, NULL, 0, NULL, 0))) {
        diagnostics(SQL_HANDLE_DBC, dbc);
        return 1;
    }
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    SQLExecDirect(stmt, (SQLCHAR *)"CREATE TABLE STAFF (ID INTEGER PRIMARY KEY, NAME VARCHAR(40), SALARY NUMERIC(9,2))",
                  SQL_NTS);

    static const char *const names[] = {"Ann", "Bob", "Cy"};
    static const double salaries[] = {5200.0, 4100.5, 3900.0};
    SQLINTEGER id = 0;
    SQLCHAR name[41];
    SQLLEN name_length = SQL_NTS;
    double salary = 0.0;
    SQLPrepare(stmt, (SQLCHAR *)"INSERT INTO STAFF VALUES (?, ?, ?)", SQL_NTS);
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &id, 0, NULL);
    SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 40, 0, name, sizeof name, &name_length);
    SQLBindParameter(stmt, 3, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_NUMERIC, 9, 2, &salary, 0, NULL);
    for (int i = 0; i < 3; ++i) {
        id = i + 1;
        strcpy((char *)name, names[i]);
        salary = salaries[i];
        if (!SQL_SUCCEEDED(SQLExecute(stmt))) {
            diagnostics(SQL_HANDLE_STMT, stmt);
        }
    }
    SQLFreeStmt(stmt, SQL_RESET_PARAMS);

    double floor = 4000.0;
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &floor, 0, NULL);
    SQLExecDirect(stmt, (SQLCHAR *)"SELECT ID, NAME, SALARY FROM STAFF WHERE SALARY > ? ORDER BY ID", SQL_NTS);
    SQLCHAR pay[16];
    SQLLEN pay_length = 0;
    SQLBindCol(stmt, 1, SQL_C_SLONG, &id, 0, NULL);
    SQLBindCol(stmt, 2, SQL_C_CHAR, name, sizeof name, &name_length);
    SQLBindCol(stmt, 3, SQL_C_CHAR, pay, sizeof pay, &pay_length);
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        printf("%d %s %s\n", (int)id, name, pay);
    }
    SQLCloseCursor(stmt);

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    return 0;
}
```

```
1 Ann 5200.00
2 Bob 4100.50
```

A connection string names the driver and holds the keys a data source
would. A failure is read by SQLGetDiagRec, with the server's SQLSTATE and
SQLCODE:

```c
#include <sql.h>
#include <sqlext.h>

#include <stdio.h>

static void diagnostics(SQLSMALLINT type, SQLHANDLE handle) {
    SQLCHAR state[6];
    SQLINTEGER native = 0;
    SQLCHAR message[256];
    for (SQLSMALLINT i = 1;
         SQLGetDiagRec(type, handle, i, state, &native, message, sizeof message, NULL) == SQL_SUCCESS; ++i) {
        printf("%s (%d) %s\n", state, (int)native, message);
    }
}

int main(void) {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    SQLCHAR *string = (SQLCHAR *)"DRIVER={Turborel};SERVER=127.0.0.1;PORT=17438;UID=ann;TLS_MODE=disable";
    if (!SQL_SUCCEEDED(SQLDriverConnect(dbc, NULL, string, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT))) {
        diagnostics(SQL_HANDLE_DBC, dbc);
        return 1;
    }
    SQLCHAR dbms[64];
    SQLGetInfo(dbc, SQL_DBMS_NAME, dbms, sizeof dbms, NULL);
    printf("connected to %s\n", dbms);
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    if (SQLExecDirect(stmt, (SQLCHAR *)"SELECT NAME FROM NOWHERE", SQL_NTS) == SQL_ERROR) {
        diagnostics(SQL_HANDLE_STMT, stmt);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    return 0;
}
```

```
connected to Turborel
42000 (-103) [Turborel][libturborelodbc] table ANN.NOWHERE is not defined
```

A tool reads the catalog as ODBC lays it out, and SQLNativeSql shows
what a statement's escapes become:

```c
#include <sql.h>
#include <sqlext.h>

#include <stdio.h>

int main(void) {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env);
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc);
    if (!SQL_SUCCEEDED(SQLConnect(dbc, (SQLCHAR *)"reports", SQL_NTS, NULL, 0, NULL, 0))) {
        return 1;
    }
    SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
    SQLColumns(stmt, NULL, 0, (SQLCHAR *)"ANN", SQL_NTS, (SQLCHAR *)"STAFF", SQL_NTS, NULL, 0);
    SQLCHAR column[64];
    SQLCHAR type[64];
    SQLINTEGER size = 0;
    SQLBindCol(stmt, 4, SQL_C_CHAR, column, sizeof column, NULL);
    SQLBindCol(stmt, 6, SQL_C_CHAR, type, sizeof type, NULL);
    SQLBindCol(stmt, 7, SQL_C_SLONG, &size, 0, NULL);
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        printf("%-6s %-17s %d\n", column, type, (int)size);
    }
    SQLCloseCursor(stmt);

    SQLCHAR native[256];
    SQLNativeSql(dbc, (SQLCHAR *)"SELECT {fn UCASE(NAME)} FROM STAFF WHERE NAME LIKE 'A%' {escape '\\'}", SQL_NTS,
                 native, sizeof native, NULL);
    printf("%s\n", native);

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
    return 0;
}
```

```
ID     INTEGER           10
NAME   CHARACTER VARYING 40
SALARY NUMERIC           9
SELECT UPPER(NAME) FROM STAFF WHERE NAME LIKE 'A%' ESCAPE '\'
```

## STANDARD

Microsoft's *ODBC Programmer's Reference*, ODBC 3.8: its handles,
functions, attributes, descriptors, data conversions and escape
sequences. The connections and statements beneath the driver are those
of [CLIENT Connecting], [CLIENT Connecting by settings],
[CLIENT Running statements] and [CLIENT Prepared statements].
The driver fulfils rows S-API-9 to S-API-14 of
[CONFORMANCE.md](../../CONFORMANCE.md); where it departs from ODBC,
[RULES.md](../../RULES.md) D8 says so.

## SEE ALSO

[The C API](02-api-c.md), [Connections](21-connections.md),
[Statements and Cursors](22-statements.md), [Values](23-values.md),
[Settings](26-settings.md), [Errors](30-errors.md),
[Installing](../user/01-installing.md).
