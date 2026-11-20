/**
 * @file connect.c
 * @brief Connecting (RULES.md D2): SQLConnect by a data source's name,
 * SQLDriverConnect by a connection string, and SQLDisconnect. A data
 * source's attributes, from odbc.ini, and a connection string's keys are
 * the driver's settings by their names, `host`, `port`, `user` and the
 * rest of Appendix C, with ODBC's own names beside them: UID the user,
 * PWD the password, SERVER the host; URI a `turborel://` URI the rest are
 * laid over. A connection string's keys come before the data source's,
 * which come before the environment's settings and the defaults.
 */
#include "format.h"
#include "memory.h"
#include "odbc/odbc.h"
#include "turborel/turborel.h"

/* ODBC's installer header, each platform's own (R-LINT-3). */
#include <ctype.h>
#include <odbcinst.h> // NOLINT(misc-include-cleaner)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The file a data source's attributes are in, and room for one's value. */
#define TRL_ODBC_INI "odbc.ini"
#define TRL_VALUE_BYTES 1024U
/* Room for a key, and for the seconds of a login timeout as text. */
#define TRL_KEY_BYTES 64U
#define TRL_NUMBER_BYTES 24U
/* The key of a URI the rest are laid over. */
#define TRL_KEY_URI "uri"
/* How a seconds' limit is told in milliseconds. */
#define TRL_MS_SECOND 1000U

/* A key and the setting it names. */
typedef struct named {
    const char *key;
    const char *setting;
} named;

/* ODBC's keys and the settings', each with the setting it gives. */
static const named keys[] = {
    {"uid", TRL_SETTING_USER},
    {"pwd", TRL_SETTING_PASSWORD},
    {"server", TRL_SETTING_HOST},
    {TRL_SETTING_HOST, TRL_SETTING_HOST},
    {TRL_SETTING_PORT, TRL_SETTING_PORT},
    {TRL_SETTING_USER, TRL_SETTING_USER},
    {TRL_SETTING_PASSWORD, TRL_SETTING_PASSWORD},
    {TRL_SETTING_TLS_MODE, TRL_SETTING_TLS_MODE},
    {TRL_SETTING_CHANNEL_BINDING, TRL_SETTING_CHANNEL_BINDING},
    {TRL_SETTING_APPLICATION_NAME, TRL_SETTING_APPLICATION_NAME},
    {TRL_SETTING_CONNECT_TIMEOUT, TRL_SETTING_CONNECT_TIMEOUT},
    {TRL_SETTING_SERVICE, TRL_SETTING_SERVICE},
    {TRL_SETTING_SERVICE_FILE, TRL_SETTING_SERVICE_FILE},
    {TRL_SETTING_PASSFILE, TRL_SETTING_PASSFILE},
    {TRL_SETTING_TARGET, TRL_SETTING_TARGET},
    {TRL_SETTING_LOAD_BALANCE, TRL_SETTING_LOAD_BALANCE},
    {TRL_SETTING_TLS_ROOTS, TRL_SETTING_TLS_ROOTS},
    {TRL_SETTING_TLS_SYSTEM_ROOTS, TRL_SETTING_TLS_SYSTEM_ROOTS},
    {TRL_SETTING_TLS_CERTIFICATE, TRL_SETTING_TLS_CERTIFICATE},
    {TRL_SETTING_TLS_KEY, TRL_SETTING_TLS_KEY},
    {TRL_SETTING_SERVER_NAME, TRL_SETTING_SERVER_NAME},
};

/* The setting `key` names, any case, or null if none. */
static const char *setting_of(const char *key) {
    char lower[TRL_KEY_BYTES];
    size_t i = 0U;
    for (; key[i] != '\0' && i + 1U < sizeof lower; ++i) {
        lower[i] = (char)tolower((unsigned char)key[i]);
    }
    lower[i] = '\0';
    for (size_t k = 0U; k < sizeof keys / sizeof keys[0]; ++k) {
        if (strcmp(lower, keys[k].key) == 0) {
            return keys[k].setting;
        }
    }
    return NULL;
}

/* What a connection is made of: the settings given, the data source's
   name, and a URI they are laid over. */
typedef struct asked {
    trl_options *given;
    char dsn[TRL_VALUE_BYTES];
    char uri[TRL_VALUE_BYTES];
} asked;

/* Takes `key`'s `value`: the DSN, the URI, a setting, or nothing. */
static bool take(asked *a, const char *key, const char *value) {
    if (strcmp(key, "DSN") == 0 || strcmp(key, "dsn") == 0) {
        trl_format(a->dsn, sizeof a->dsn, "%s", value);
        return true;
    }
    char lower[TRL_KEY_BYTES];
    trl_format(lower, sizeof lower, "%s", key);
    for (char *at = lower; *at != '\0'; ++at) {
        *at = (char)tolower((unsigned char)*at);
    }
    if (strcmp(lower, TRL_KEY_URI) == 0) {
        trl_format(a->uri, sizeof a->uri, "%s", value);
        return true;
    }
    if (strcmp(lower, "dsn") == 0) {
        trl_format(a->dsn, sizeof a->dsn, "%s", value);
        return true;
    }
    const char *setting = setting_of(key);
    /* DRIVER, DATABASE and keys of other drivers name nothing of this one. */
    return setting == NULL || trl_options_get(a->given, setting) != NULL ||
           trl_options_set(a->given, setting, value) == TRL_OK;
}

/* Reads a key of `text` from `*at`, to its `=` or `;`, into `key`. */
static size_t key_read(const char *text, size_t *at, char *key) {
    size_t k = 0U;
    while (text[*at] != '\0' && text[*at] != '=' && text[*at] != ';') {
        if (k + 1U < TRL_KEY_BYTES) {
            key[k++] = text[*at];
        }
        ++*at;
    }
    key[k] = '\0';
    return k;
}

/* Reads a value of `text` from `*at`, after its `=`: to its `;`, or
   within braces, into `value`. */
static void value_read(const char *text, size_t *at, char *value) {
    bool braced = text[*at] == '{';
    char end = braced ? '}' : ';';
    *at += braced ? 1U : 0U;
    size_t v = 0U;
    while (text[*at] != '\0' && text[*at] != end) {
        if (v + 1U < TRL_VALUE_BYTES) {
            value[v++] = text[*at];
        }
        ++*at;
    }
    *at += braced && text[*at] == '}' ? 1U : 0U;
    value[v] = '\0';
}

/* Reads one `key=value` or `key={value}` pair of `text` from `*at`. */
static bool pair_read(const char *text, size_t *at, char *key, char *value) {
    size_t k = key_read(text, at, key);
    value[0] = '\0';
    if (text[*at] == '=') {
        ++*at;
        value_read(text, at, value);
    }
    *at += text[*at] == ';' ? 1U : 0U;
    return k > 0U;
}

/* Takes each pair of a connection string. */
static bool string_taken(asked *a, const char *text) {
    char key[TRL_KEY_BYTES];
    char value[TRL_VALUE_BYTES];
    size_t at = 0U;
    while (text[at] != '\0') {
        if (pair_read(text, &at, key, value) && !take(a, key, value)) {
            return false;
        }
    }
    return true;
}

/* Takes each attribute of the data source `a` names, from odbc.ini. */
static bool source_taken(asked *a) {
    char value[TRL_VALUE_BYTES];
    if (a->dsn[0] == '\0') {
        return true;
    }
    if (a->uri[0] == '\0' &&
        SQLGetPrivateProfileString(a->dsn, "URI", "", value, (int)sizeof value, TRL_ODBC_INI) > 0) {
        trl_format(a->uri, sizeof a->uri, "%s", value);
    }
    for (size_t k = 0U; k < sizeof keys / sizeof keys[0]; ++k) {
        if (SQLGetPrivateProfileString(a->dsn, keys[k].key, "", value, (int)sizeof value, TRL_ODBC_INI) > 0 &&
            !take(a, keys[k].key, value)) {
            return false;
        }
    }
    return true;
}

/* Lays the URI under the settings given, as options laid in order. */
static trl_status laid(trl_odbc_dbc *dbc, asked *a, trl_options **out) {
    trl_status made = a->uri[0] == '\0' ? trl_options_new(out) : trl_options_from_uri(a->uri, out);
    if (made != TRL_OK) {
        return made;
    }
    static const char *const order[] = {
        TRL_SETTING_HOST,
        TRL_SETTING_PORT,
        TRL_SETTING_USER,
        TRL_SETTING_PASSWORD,
        TRL_SETTING_TLS_MODE,
        TRL_SETTING_CHANNEL_BINDING,
        TRL_SETTING_APPLICATION_NAME,
        TRL_SETTING_CONNECT_TIMEOUT,
        TRL_SETTING_SERVICE,
        TRL_SETTING_SERVICE_FILE,
        TRL_SETTING_PASSFILE,
        TRL_SETTING_TARGET,
        TRL_SETTING_LOAD_BALANCE,
        TRL_SETTING_TLS_ROOTS,
        TRL_SETTING_TLS_SYSTEM_ROOTS,
        TRL_SETTING_TLS_CERTIFICATE,
        TRL_SETTING_TLS_KEY,
        TRL_SETTING_SERVER_NAME,
    };
    for (size_t i = 0U; made == TRL_OK && i < sizeof order / sizeof order[0]; ++i) {
        const char *value = trl_options_get(a->given, order[i]);
        made = value != NULL ? trl_options_set(*out, order[i], value) : TRL_OK;
    }
    if (made == TRL_OK && dbc->login_timeout > 0U && trl_options_get(*out, TRL_SETTING_CONNECT_TIMEOUT) == NULL) {
        char seconds[TRL_NUMBER_BYTES];
        trl_format(seconds, sizeof seconds, "%lu", (unsigned long)dbc->login_timeout);
        made = trl_options_set(*out, TRL_SETTING_CONNECT_TIMEOUT, seconds);
    }
    return made;
}

/* Connects `dbc` as `a` says. */
static SQLRETURN connected(trl_odbc_dbc *dbc, asked *a) {
    trl_options *options = NULL;
    trl_settings *settings = NULL;
    trl_status made = laid(dbc, a, &options);
    if (made == TRL_FAILED) {
        SQLRETURN failed = trl_odbc_failed(&dbc->diag, trl_options_failure(options));
        trl_options_free(options);
        return failed;
    }
    made = made == TRL_OK ? trl_settings_resolve(options, &settings) : made;
    trl_options_free(options);
    trl_conn *conn = NULL;
    made = made == TRL_OK ? trl_settings_connect(settings, &conn) : made;
    SQLRETURN returned = SQL_SUCCESS;
    if (made == TRL_NO_MEMORY) {
        returned = trl_odbc_no_memory(&dbc->diag);
    } else if (made != TRL_OK) {
        returned = trl_odbc_failed(&dbc->diag, conn != NULL ? trl_conn_failure(conn) : trl_settings_failure(settings));
    }
    trl_settings_free(settings);
    if (returned != SQL_SUCCESS) {
        trl_conn_free(conn);
        return returned;
    }
    size_t length = strlen(a->dsn) + 1U;
    dbc->dsn = trl_malloc(length);
    if (dbc->dsn == NULL) {
        trl_conn_free(conn);
        return trl_odbc_no_memory(&dbc->diag);
    }
    memcpy(dbc->dsn, a->dsn, length);
    (void)trl_conn_set_autocommit(conn, dbc->autocommit);
    /* A connection whose server gave no cancel key cannot be cancelled. */
    if (trl_canceller_new(conn, &dbc->canceller) != TRL_OK) {
        dbc->canceller = NULL;
    }
    dbc->conn = conn;
    return SQL_SUCCESS;
}

/* Connects `dbc` by a connection string, a data source's name and its
   user and password; any may be null. */
SQLRETURN trl_odbc_connect(trl_odbc_dbc *dbc, const char *string, const char *const *credentials) {
    trl_odbc_clear(&dbc->diag);
    if (dbc->conn != NULL) {
        return trl_odbc_fail(&dbc->diag, "08002", "Connection name in use");
    }
    asked a;
    memset(&a, 0, sizeof a);
    if (trl_options_new(&a.given) != TRL_OK) {
        return trl_odbc_no_memory(&dbc->diag);
    }
    bool taken = string == NULL || string_taken(&a, string);
    taken = taken && (credentials[TRL_ODBC_DSN] == NULL || take(&a, "DSN", credentials[TRL_ODBC_DSN]));
    taken = taken && (credentials[TRL_ODBC_USER] == NULL || credentials[TRL_ODBC_USER][0] == '\0' ||
                      take(&a, "uid", credentials[TRL_ODBC_USER]));
    taken = taken && (credentials[TRL_ODBC_PASSWORD] == NULL || credentials[TRL_ODBC_PASSWORD][0] == '\0' ||
                      take(&a, "pwd", credentials[TRL_ODBC_PASSWORD]));
    taken = taken && source_taken(&a);
    SQLRETURN returned = taken ? connected(dbc, &a) : trl_odbc_no_memory(&dbc->diag);
    trl_options_free(a.given);
    return returned;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLConnect(SQLHDBC handle, SQLCHAR *dsn, SQLSMALLINT dsn_length, SQLCHAR *user,
                                             SQLSMALLINT user_length, SQLCHAR *password, SQLSMALLINT password_length) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *texts[TRL_ODBC_CREDENTIALS] = {
        trl_odbc_text(dsn, dsn_length),
        trl_odbc_text(user, user_length),
        trl_odbc_text(password, password_length),
    };
    bool had = (dsn == NULL || texts[TRL_ODBC_DSN] != NULL) && (user == NULL || texts[TRL_ODBC_USER] != NULL) &&
               (password == NULL || texts[TRL_ODBC_PASSWORD] != NULL);
    const char *const credentials[TRL_ODBC_CREDENTIALS] = {
        texts[TRL_ODBC_DSN],
        texts[TRL_ODBC_USER],
        texts[TRL_ODBC_PASSWORD],
    };
    SQLRETURN returned = had ? trl_odbc_connect(dbc, NULL, credentials) : trl_odbc_no_memory(&dbc->diag);
    for (size_t i = 0U; i < TRL_ODBC_CREDENTIALS; ++i) {
        free(texts[i]);
    }
    return returned;
}

// NOLINTNEXTLINE(readability-function-size): ODBC's own signature (RULES.md D2).
TRL_ODBC_EXPORT SQLRETURN SQL_API SQLDriverConnect(SQLHDBC handle, SQLHWND window, SQLCHAR *in, SQLSMALLINT in_length,
                                                   SQLCHAR *out, SQLSMALLINT capacity, SQLSMALLINT *out_length,
                                                   SQLUSMALLINT completion) {
    (void)window;
    (void)completion;
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    char *string = trl_odbc_text(in, in_length);
    if (string == NULL) {
        return in == NULL ? trl_odbc_fail(&dbc->diag, "HY009", "Invalid use of null pointer")
                          : trl_odbc_no_memory(&dbc->diag);
    }
    const char *const none[TRL_ODBC_CREDENTIALS] = {NULL, NULL, NULL};
    /* No dialog is shown: a connection string the driver cannot connect
       by fails as it would without one. */
    SQLRETURN returned = trl_odbc_connect(dbc, string, none);
    if (SQL_SUCCEEDED(returned)) {
        trl_odbc_out given = {out, capacity, out_length, NULL, NULL, false, false};
        returned = trl_odbc_return(&dbc->diag, trl_odbc_put(&dbc->diag, &given, string));
    }
    free(string);
    return returned;
}

TRL_ODBC_EXPORT SQLRETURN SQL_API SQLDisconnect(SQLHDBC handle) {
    trl_odbc_dbc *dbc = trl_odbc_dbc_of(handle);
    if (dbc == NULL) {
        return SQL_INVALID_HANDLE;
    }
    trl_odbc_clear(&dbc->diag);
    if (dbc->conn == NULL) {
        return trl_odbc_fail(&dbc->diag, "08003", "Connection not open");
    }
    if (!dbc->autocommit && trl_conn_in_transaction(dbc->conn)) {
        return trl_odbc_fail(&dbc->diag, "25000", "Invalid transaction state");
    }
    trl_odbc_free_children(dbc);
    trl_canceller_free(dbc->canceller);
    dbc->canceller = NULL;
    trl_status ended = trl_conn_terminate(dbc->conn);
    trl_conn_free(dbc->conn);
    dbc->conn = NULL;
    free(dbc->dsn);
    dbc->dsn = NULL;
    return ended == TRL_OK ? SQL_SUCCESS : trl_odbc_warn(&dbc->diag, "01002", "Disconnect error");
}
