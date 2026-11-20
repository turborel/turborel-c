/**
 * @file settings.c
 * @brief Connection settings laid [CLIENT Where settings come from]: the
 * program's layer, then the service it or the environment names, then
 * the environment, then the defaults, each setting taken from the first
 * layer that has it, then read: the servers, each with its port, as whom,
 * how TLS is used, and what a server must be. A password none gives is
 * looked up in the password file [CLIENT The password file].
 */
#include "settings/settings.h"

#include "codec/buffer.h"
#include "codec/error.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The service file and the password file read when none is named, in the
   home directory. */
#define TRL_DEFAULT_SERVICE_FILE ".turborel_service.conf"
#define TRL_DEFAULT_PASSFILE ".turborel_pass"
/* How long each step of connecting waits unless told, in seconds, and a
   second's milliseconds. */
#define TRL_CONNECT_WITHIN_SECONDS 30U
#define TRL_MS_SECOND 1000U
/* The base of a decimal digit's value, and the largest port. */
#define TRL_TEN 10U
#define TRL_MOST_PORT 65535U
/* What separates the hosts of a list, and their ports. */
#define TRL_LIST ','
/* The octets of an address's closing bracket and the colon after it. */
#define TRL_BRACKET_COLON 2U
/* Room for a port's text. */
#define TRL_PORT_BYTES 8U

/* A word a setting takes, and its value. */
typedef struct word {
    const char *text;
    int value;
} word;

/* The words a setting takes. */
typedef struct words {
    const word *list;
    size_t count;
} words;

/* The words of `list`, an array. */
#define TRL_WORDS(list) ((words){(list), sizeof(list) / sizeof((list)[0])})

static const word tls_modes[] = {
    {"disable", TRL_TLS_DISABLE},     {"prefer", TRL_TLS_PREFER},           {"require", TRL_TLS_REQUIRE},
    {"verify-ca", TRL_TLS_VERIFY_CA}, {"verify-full", TRL_TLS_VERIFY_FULL},
};
static const word channel_bindings[] = {
    {"disable", TRL_CHANNEL_BINDING_DISABLE},
    {"prefer", TRL_CHANNEL_BINDING_PREFER},
    {"require", TRL_CHANNEL_BINDING_REQUIRE},
};
static const word target_roles[] = {
    {"any", TRL_TARGET_ANY},
    {"read-write", TRL_TARGET_READ_WRITE},
    {"read-only", TRL_TARGET_READ_ONLY},
};
static const word load_balances[] = {{"disable", TRL_LOAD_BALANCE_DISABLE}, {"random", TRL_LOAD_BALANCE_RANDOM}};
static const word booleans[] = {{"true", 1}, {"false", 0}};

/* The value of the setting at `key` as laid, a word among `taken`; the
   default where none is laid; false, failing, of a word not among them. */
static bool word_of(const trl_layer *laid, trl_key key, words taken, int *out, trl_failure *failure) {
    const char *text = laid->values[key];
    if (text == NULL) {
        return true;
    }
    for (size_t i = 0U; i < taken.count; ++i) {
        if (strcmp(taken.list[i].text, text) == 0) {
            *out = taken.list[i].value;
            return true;
        }
    }
    char known[TRL_FAILURE_MESSAGE_BYTES] = "";
    for (size_t i = 0U; i < taken.count; ++i) {
        size_t used = strlen(known);
        trl_format(known + used, sizeof known - used, "%s%s", i == 0U ? "" : ", ", taken.list[i].text);
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "%s is %s, not one of %s", trl_setting_name(key), text, known);
    trl_settings_unable(failure, message);
    return false;
}

/* `length` octets of `text` trimmed of spaces at both ends. */
static const char *trim(const char *text, size_t *length) {
    while (*length > 0U && (text[0] == ' ' || text[0] == '\t')) {
        ++text;
        --*length;
    }
    while (*length > 0U && (text[*length - 1U] == ' ' || text[*length - 1U] == '\t')) {
        --*length;
    }
    return text;
}

/* The whole number `length` octets of `text` are, trimmed, at most `most`:
   digits, a plus before them allowed. */
static bool number(const char *text, size_t length, uint64_t most, uint64_t *out) {
    const char *at = trim(text, &length);
    if (length > 0U && at[0] == '+') {
        ++at;
        --length;
    }
    uint64_t n = 0U;
    for (size_t i = 0U; i < length; ++i) {
        if (at[i] < '0' || at[i] > '9' || n > (most - (uint64_t)(at[i] - '0')) / TRL_TEN) {
            return false;
        }
        n = (n * TRL_TEN) + (uint64_t)(at[i] - '0');
    }
    *out = n;
    return length > 0U;
}

/* The port `length` octets of `text` are, the value of `key`. */
static bool port_of(trl_key key, const char *text, size_t length, uint16_t *out, trl_failure *failure) {
    uint64_t n = 0U;
    if (number(text, length, TRL_MOST_PORT, &n)) {
        *out = (uint16_t)n;
        return true;
    }
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "%s is %.*s, no port", trl_setting_name(key), (int)length, text);
    trl_settings_unable(failure, message);
    return false;
}

/* Sets `host` to `length` octets of `name`, and its port and kind. */
static bool host_named(trl_host *host, const char *name, size_t length, uint16_t port, bool local) {
    host->name = trl_malloc(length + 1U);
    if (host->name == NULL) {
        return false;
    }
    memcpy(host->name, name, length);
    host->name[length] = '\0';
    host->port = local ? 0U : port;
    host->local = local;
    return true;
}

/* Notes memory not had, of a host not named; whether it was named. */
static bool named_or_not(bool named, trl_failure *failure) {
    if (!named) {
        trl_failure_no_memory(failure);
    }
    return named;
}

/* The host `[address]` or `[address]:port` of `length` is, of IPv6. */
static bool bracketed(const char *name, size_t length, uint16_t fallback, trl_host *out, trl_failure *failure) {
    const char *close = memchr(name, ']', length);
    if (close == NULL) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "%.*s has no ]", (int)length, name);
        trl_settings_unable(failure, message);
        return false;
    }
    uint16_t port = fallback;
    size_t after = length - (size_t)(close - name) - 1U;
    if (after > 0U && close[1] == ':' &&
        !port_of(TRL_KEY_HOST, close + TRL_BRACKET_COLON, after - 1U, &port, failure)) {
        return false;
    }
    return named_or_not(host_named(out, name + 1, (size_t)(close - name) - 1U, port, false), failure);
}

/* The host `name` of `length` is: a path, of a local socket; `[address]`
   or `[address]:port`, of IPv6; `name:port`; or a name, at `fallback`. */
static bool host_of(const char *name, size_t length, uint16_t fallback, trl_host *out, trl_failure *failure) {
    if (length == 0U) {
        trl_settings_unable(failure, "a host of the list is empty");
        return false;
    }
    if (name[0] == '/' || name[0] == '\\') {
        return named_or_not(host_named(out, name, length, 0U, true), failure);
    }
    if (name[0] == '[') {
        return bracketed(name, length, fallback, out, failure);
    }
    uint16_t port = fallback;
    const char *colon = memchr(name, ':', length);
    size_t head = colon == NULL ? length : (size_t)(colon - name);
    /* A name of two colons or more is an IPv6 address unbracketed, of no
       port. */
    if (colon != NULL && memchr(colon + 1, ':', length - head - 1U) == NULL) {
        if (!port_of(TRL_KEY_HOST, colon + 1, length - head - 1U, &port, failure)) {
            return false;
        }
        return named_or_not(host_named(out, name, head, port, false), failure);
    }
    return named_or_not(host_named(out, name, length, port, false), failure);
}

/* The ports `ports` lists, those not empty, into `out`, of `capacity`;
   how many in `count`. */
static bool ports_of(const char *ports, uint16_t *out, size_t capacity, size_t *count, trl_failure *failure) {
    *count = 0U;
    for (const char *at = ports; at != NULL;) {
        const char *end = strchr(at, TRL_LIST);
        size_t length = end == NULL ? strlen(at) : (size_t)(end - at);
        size_t trimmed = length;
        (void)trim(at, &trimmed);
        if (trimmed > 0U) {
            uint16_t port = 0U;
            if (!port_of(TRL_KEY_PORT, at, length, &port, failure)) {
                return false;
            }
            if (*count < capacity) {
                out[*count] = port;
            }
            ++*count;
        }
        at = end == NULL ? NULL : end + 1;
    }
    return true;
}

/* How many items a list of `text`, apart by commas, has. */
static size_t items_of(const char *text) {
    size_t count = 1U;
    for (const char *at = text; *at != '\0'; ++at) {
        count += *at == TRL_LIST ? 1U : 0U;
    }
    return count;
}

/* The port of the host at `i` that carries none: that of the same place
   of the ports listed, or the one port listed, or the default. */
static uint16_t port_for(const uint16_t *listed, size_t count, size_t i) {
    if (i < count) {
        return listed[i];
    }
    return count == 1U ? listed[0] : (uint16_t)TRL_DEFAULT_PORT;
}

/* The hosts `hosts` names, each of its own port, or the port of the same
   place in `ports`, or the one port `ports` gives, or the default. */
static bool hosts_of(trl_settings *settings, const char *hosts, const char *ports) {
    size_t count = items_of(hosts);
    size_t port_capacity = items_of(ports);
    uint16_t *listed = trl_calloc(port_capacity, sizeof *listed);
    settings->hosts = trl_calloc(count, sizeof *settings->hosts);
    if (listed == NULL || settings->hosts == NULL) {
        free(listed);
        trl_failure_no_memory(&settings->failure);
        return false;
    }
    size_t port_count = 0U;
    bool read = ports_of(ports, listed, port_capacity, &port_count, &settings->failure);
    const char *at = hosts;
    for (size_t i = 0U; read && i < count; ++i) {
        const char *end = strchr(at, TRL_LIST);
        size_t length = end == NULL ? strlen(at) : (size_t)(end - at);
        const char *name = trim(at, &length);
        uint16_t fallback = port_for(listed, port_count, i);
        read = host_of(name, length, fallback, &settings->hosts[i], &settings->failure);
        settings->host_count += read ? 1U : 0U;
        at = end == NULL ? at : end + 1;
    }
    free(listed);
    return read;
}

/* The time `text` is, in whole seconds, as milliseconds; 0 for no limit. */
static bool seconds_of(const char *text, uint32_t *out, trl_failure *failure) {
    if (text == NULL) {
        return true;
    }
    uint64_t n = 0U;
    if (!number(text, strlen(text), UINT64_MAX, &n)) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "%s is %s, not a whole number of seconds",
                   trl_setting_name(TRL_KEY_CONNECT_TIMEOUT), text);
        trl_settings_unable(failure, message);
        return false;
    }
    /* Milliseconds a connection holds at most, about 49 days. */
    *out = n > UINT32_MAX / TRL_MS_SECOND ? UINT32_MAX : (uint32_t)(n * TRL_MS_SECOND);
    return true;
}

/* Reads the words of the settings laid. */
static bool words_of(trl_settings *settings, const trl_layer *laid) {
    int mode = (int)settings->tls_mode;
    int binding = (int)settings->channel_binding;
    int target = (int)settings->target;
    int balance = (int)settings->load_balance;
    int roots = 0;
    trl_failure *failure = &settings->failure;
    bool read = word_of(laid, TRL_KEY_TLS_MODE, TRL_WORDS(tls_modes), &mode, failure) &&
                word_of(laid, TRL_KEY_CHANNEL_BINDING, TRL_WORDS(channel_bindings), &binding, failure) &&
                word_of(laid, TRL_KEY_TARGET, TRL_WORDS(target_roles), &target, failure) &&
                word_of(laid, TRL_KEY_LOAD_BALANCE, TRL_WORDS(load_balances), &balance, failure) &&
                word_of(laid, TRL_KEY_TLS_SYSTEM_ROOTS, TRL_WORDS(booleans), &roots, failure);
    settings->tls_mode = (trl_tls_mode)mode;
    settings->channel_binding = (trl_channel_binding)binding;
    settings->target = (trl_target_role)target;
    settings->load_balance = (trl_load_balance)balance;
    settings->system_roots = roots != 0;
    return read;
}

/* Keeps a copy of each setting laid; false if memory cannot be had. */
static bool keep_laid(trl_settings *settings, const trl_layer *laid) {
    for (size_t i = 0U; i < TRL_SETTING_COUNT; ++i) {
        if (laid->values[i] == NULL) {
            continue;
        }
        size_t length = strlen(laid->values[i]);
        settings->laid[i] = trl_malloc(length + 1U);
        if (settings->laid[i] == NULL) {
            return false;
        }
        memcpy(settings->laid[i], laid->values[i], length + 1U);
    }
    return true;
}

/* Reads the settings laid, the defaults for the rest. */
static bool settings_of(trl_settings *settings, const trl_layer *laid) {
    if (!keep_laid(settings, laid)) {
        trl_failure_no_memory(&settings->failure);
        return false;
    }
    if (laid->values[TRL_KEY_USER] == NULL) {
        trl_settings_unable(&settings->failure, "no user is named: by the program, a service, or TURBOREL_USER");
        return false;
    }
    const char *hosts = laid->values[TRL_KEY_HOST] == NULL ? TRL_DEFAULT_HOST : laid->values[TRL_KEY_HOST];
    const char *ports = laid->values[TRL_KEY_PORT] == NULL ? "" : laid->values[TRL_KEY_PORT];
    if (!hosts_of(settings, hosts, ports) || !words_of(settings, laid) ||
        !seconds_of(laid->values[TRL_KEY_CONNECT_TIMEOUT], &settings->connect_ms, &settings->failure)) {
        return false;
    }
    const char *passfile = laid->values[TRL_KEY_PASSFILE];
    settings->passfile = passfile == NULL ? trl_at_home(TRL_DEFAULT_PASSFILE) : NULL;
    if (passfile != NULL) {
        size_t length = strlen(passfile);
        settings->passfile = trl_malloc(length + 1U);
        if (settings->passfile != NULL) {
            memcpy(settings->passfile, passfile, length + 1U);
        }
    }
    return true;
}

/* The process's environment, as the C library gives it. */
static const char *process_variable(void *context, const char *name) {
    (void)context;
    /* Read as the settings are laid, on the thread that lays them; the
       library sets no variable. */
    return getenv(name); // NOLINT(concurrency-mt-unsafe)
}

/* Settings, empty, of their defaults; null if memory cannot be had. */
static trl_settings *made(void) {
    trl_settings *settings = trl_calloc(1U, sizeof *settings);
    if (settings == NULL) {
        return NULL;
    }
    trl_failure_init(&settings->failure);
    settings->tls_mode = TRL_TLS_PREFER;
    settings->channel_binding = TRL_CHANNEL_BINDING_PREFER;
    settings->connect_ms = TRL_CONNECT_WITHIN_SECONDS * TRL_MS_SECOND;
    settings->target = TRL_TARGET_ANY;
    settings->load_balance = TRL_LOAD_BALANCE_DISABLE;
#ifdef TRL_HAVE_TLS
    if (trl_key_cache_new(&settings->key_cache) != TRL_OK) {
        free(settings);
        return NULL;
    }
#endif
    return settings;
}

/* The service the layers name, read into `service`; true if none is named. */
static bool service_of(const trl_layer *named, trl_options *service, trl_failure *failure) {
    const char *name = named->values[TRL_KEY_SERVICE];
    if (name == NULL) {
        return true;
    }
    const char *file = named->values[TRL_KEY_SERVICE_FILE];
    char *home = file == NULL ? trl_at_home(TRL_DEFAULT_SERVICE_FILE) : NULL;
    const char *path = file != NULL ? file : home;
    bool read = trl_service_read(path != NULL ? path : "", name, service, failure);
    free(home);
    return read;
}

/* Ends a resolve: TRL_OK, or why not; memory not had frees what was made. */
static trl_status resolved(trl_settings *settings, bool laid, trl_settings **out) {
    *out = settings;
    if (laid) {
        return TRL_OK;
    }
    if (trl_failure_sqlcode(&settings->failure) == TRL_SQLCODE_RESOURCES) {
        trl_settings_free(settings);
        *out = NULL;
        return TRL_NO_MEMORY;
    }
    return TRL_FAILED;
}

trl_status trl_settings_resolve_in(const trl_options *options, trl_variable variable, void *context,
                                   trl_settings **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (variable == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_settings *settings = made();
    trl_options *environment = NULL;
    trl_options *service = NULL;
    if (settings == NULL || trl_options_new(&environment) != TRL_OK || trl_options_new(&service) != TRL_OK ||
        !trl_options_environment(environment, variable, context)) {
        trl_options_free(environment);
        trl_options_free(service);
        trl_settings_free(settings);
        return TRL_NO_MEMORY;
    }
    trl_layer program;
    trl_layer from_environment;
    trl_layer named;
    trl_layer_of(&program, options);
    trl_layer_of(&from_environment, environment);
    trl_layer_over(&named, &program, &from_environment);
    bool laid = service_of(&named, service, &settings->failure);
    if (laid) {
        trl_layer from_service;
        trl_layer lower;
        trl_layer all;
        trl_layer_of(&from_service, service);
        trl_layer_over(&lower, &from_service, &from_environment);
        trl_layer_over(&all, &program, &lower);
        laid = settings_of(settings, &all);
    }
    trl_options_free(environment);
    trl_options_free(service);
    return resolved(settings, laid, out);
}

trl_status trl_settings_resolve(const trl_options *options, trl_settings **out) {
    return trl_settings_resolve_in(options, process_variable, NULL, out);
}

trl_status trl_settings_from_uri(const char *uri, trl_settings **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (uri == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_options *options = NULL;
    trl_status read = trl_options_from_uri(uri, &options);
    if (read == TRL_FAILED) {
        trl_settings *settings = made();
        if (settings != NULL) {
            trl_failure_copy(&settings->failure, &options->failure);
        }
        trl_options_free(options);
        *out = settings;
        return settings == NULL ? TRL_NO_MEMORY : TRL_FAILED;
    }
    trl_status laid = read == TRL_OK ? trl_settings_resolve(options, out) : read;
    trl_options_free(options);
    return laid;
}

const trl_failure *trl_settings_failure(const trl_settings *settings) {
    return settings == NULL ? NULL : &settings->failure;
}

size_t trl_settings_host_count(const trl_settings *settings) {
    return settings == NULL ? 0U : settings->host_count;
}

const char *trl_settings_host(const trl_settings *settings, size_t host, uint16_t *port, bool *local) {
    if (settings == NULL || host >= settings->host_count) {
        return NULL;
    }
    if (port != NULL) {
        *port = settings->hosts[host].port;
    }
    if (local != NULL) {
        *local = settings->hosts[host].local;
    }
    return settings->hosts[host].name;
}

const char *trl_settings_user(const trl_settings *settings) {
    return settings == NULL ? NULL : settings->laid[TRL_KEY_USER];
}

const char *trl_settings_get(const trl_settings *settings, const char *key) {
    int at = settings == NULL || key == NULL ? -1 : trl_setting_of(key);
    return at < 0 || at == TRL_KEY_PASSWORD ? NULL : settings->laid[at];
}

trl_tls_mode trl_settings_tls_mode(const trl_settings *settings) {
    return settings == NULL ? TRL_TLS_PREFER : settings->tls_mode;
}

trl_channel_binding trl_settings_channel_binding(const trl_settings *settings) {
    return settings == NULL ? TRL_CHANNEL_BINDING_PREFER : settings->channel_binding;
}

uint32_t trl_settings_connect_timeout(const trl_settings *settings) {
    return settings == NULL ? 0U : settings->connect_ms;
}

trl_target_role trl_settings_target(const trl_settings *settings) {
    return settings == NULL ? TRL_TARGET_ANY : settings->target;
}

trl_load_balance trl_settings_load_balance(const trl_settings *settings) {
    return settings == NULL ? TRL_LOAD_BALANCE_DISABLE : settings->load_balance;
}

bool trl_settings_tls_system_roots(const trl_settings *settings) {
    return settings != NULL && settings->system_roots;
}

trl_key_cache *trl_settings_key_cache(const trl_settings *settings) {
    return settings == NULL ? NULL : settings->key_cache;
}

/* Gives `text` to `out` of `capacity`, ended by a null, cut short to fit. */
static void give(const uint8_t *text, size_t length, char *out, size_t capacity, size_t *whole) {
    if (whole != NULL) {
        *whole = length;
    }
    if (capacity > 0U) {
        size_t kept = length < capacity - 1U ? length : capacity - 1U;
        if (kept > 0U) {
            memcpy(out, text, kept);
        }
        out[kept] = '\0';
    }
}

bool trl_settings_password_for(const trl_settings *settings, size_t host, char *out, size_t capacity, size_t *length) {
    if (settings == NULL || host >= settings->host_count || (out == NULL && capacity > 0U)) {
        return false;
    }
    const char *given = settings->laid[TRL_KEY_PASSWORD];
    if (given != NULL) {
        give((const uint8_t *)given, strlen(given), out, capacity, length);
        return true;
    }
    char *text = settings->passfile == NULL ? NULL : trl_read_regular(settings->passfile);
    if (text == NULL) {
        return false;
    }
    const trl_host *at = &settings->hosts[host];
    char port[TRL_PORT_BYTES];
    trl_format(port, sizeof port, "%u", at->local ? (unsigned int)TRL_DEFAULT_PORT : (unsigned int)at->port);
    trl_buffer password;
    trl_buffer_init(&password);
    bool found = trl_password_find(text, at->name, port, settings->laid[TRL_KEY_USER], &password) &&
                 password.state == TRL_BUFFER_OK;
    if (found) {
        give(password.data, password.length, out, capacity, length);
    }
    trl_buffer_free(&password);
    free(text);
    return found;
}

void trl_settings_free(trl_settings *settings) {
    if (settings == NULL) {
        return;
    }
    for (size_t i = 0U; i < TRL_SETTING_COUNT; ++i) {
        free(settings->laid[i]);
    }
    for (size_t i = 0U; i < settings->host_count; ++i) {
        free(settings->hosts[i].name);
    }
    free(settings->hosts);
    free(settings->passfile);
    trl_key_cache_free(settings->key_cache);
    trl_failure_clear(&settings->failure);
    free(settings);
}
