/**
 * @file settings_test.c
 * @brief Connection settings, as turborel-client's own tests read them
 * (RULES.md R-TEST-9): the URI, the service file, the password file, the
 * environment, and the order the layers are laid in.
 */
#include "failure.h"
#include "harness.h"
#include "settings/settings.h"
#include "settings_common.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

/**
 * Verifies: [CLIENT The connection URI] a URI gives its user, password,
 * hosts each of its port, an IPv6 address in brackets, a local socket
 * percent-encoded, and its query's settings; percent-encoding is decoded
 * in each part; the password is never given back as a setting.
 */
static void a_uri_gives_its_user_hosts_and_settings(trl_test *test) {
    static const char uri[] = "turborel://ann:p%40ss@db1:7000,[::1]:7001,db3,%2Frun%2Fs.sock/"
                              "?tls_mode=verify-full&target=read-write&connect_timeout=5";
    trl_options *options = NULL;
    TRL_CHECK(test, trl_options_from_uri(uri, &options) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_USER), "ann") == 0);
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_PASSWORD), "p@ss") == 0);
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = laid(options, &none);
    TRL_CHECK(test, settings != NULL && trl_settings_host_count(settings) == 4U);
    TRL_CHECK(test, host_is(settings, 0U, "db1", 7000U, false) && host_is(settings, 1U, "::1", 7001U, false));
    TRL_CHECK(test,
              host_is(settings, 2U, "db3", TRL_DEFAULT_PORT, false) && host_is(settings, 3U, "/run/s.sock", 0U, true));
    TRL_CHECK(test, trl_settings_tls_mode(settings) == TRL_TLS_VERIFY_FULL);
    TRL_CHECK(test, trl_settings_target(settings) == TRL_TARGET_READ_WRITE);
    TRL_CHECK(test, trl_settings_connect_timeout(settings) == 5000U);
    TRL_CHECK(test, trl_settings_get(settings, TRL_SETTING_PASSWORD) == NULL);
    char password[16];
    TRL_CHECK(test, trl_settings_password_for(settings, 0U, password, sizeof password, NULL) &&
                        strcmp(password, "p@ss") == 0);
    trl_settings_free(settings);
    trl_options_free(options);
    TRL_CHECK(test, trl_settings_from_uri("turborel://ann@/", &settings) == TRL_OK);
    TRL_CHECK(test, trl_settings_host_count(settings) == 1U &&
                        host_is(settings, 0U, TRL_DEFAULT_HOST, TRL_DEFAULT_PORT, false));
    TRL_CHECK(test,
              strcmp(trl_settings_user(settings), "ann") == 0 && trl_settings_connect_timeout(settings) == 30000U);
    trl_settings_free(settings);
}

/**
 * Verifies: [CLIENT The connection URI] a URI of another scheme, of a
 * path, of a key that names no setting, of a pair without `=`, or of a
 * `%` not followed by two hexadecimal digits is refused (SQLSTATE 08001);
 * so is a word no setting takes, a port that is no number, and no user
 * [CLIENT Where settings come from].
 */
static void a_malformed_uri_or_setting_is_refused(trl_test *test) {
    static const char *const unread[] = {
        "other://a@b",      "turborel://a@b/db", "turborel://a@b/?colour=red", "turborel://a@b/?tls_mode",
        "turborel://a%4@b", "turborel://a%ff@b",
    };
    for (size_t i = 0U; i < sizeof unread / sizeof unread[0]; ++i) {
        trl_options *options = NULL;
        TRL_CHECK(test, trl_options_from_uri(unread[i], &options) == TRL_FAILED);
        TRL_CHECK(test, unable(trl_options_failure(options), ""));
        trl_options_free(options);
    }
    static const char *const unlaid[] = {
        "turborel://a@b/?tls_mode=always",
        "turborel://a@b:port",
        "turborel://a@b/?connect_timeout=-1",
        "turborel://b",
    };
    environment none = {{NULL}, {NULL}, 0U};
    for (size_t i = 0U; i < sizeof unlaid / sizeof unlaid[0]; ++i) {
        trl_options *options = NULL;
        TRL_CHECK(test, trl_options_from_uri(unlaid[i], &options) == TRL_OK);
        trl_settings *settings = NULL;
        TRL_CHECK(test, trl_settings_resolve_in(options, variable, &none, &settings) == TRL_FAILED);
        TRL_CHECK(test, unable(trl_settings_failure(settings), ""));
        trl_settings_free(settings);
        trl_options_free(options);
    }
    trl_settings *settings = NULL;
    TRL_CHECK(test, trl_settings_from_uri("other://a@b", &settings) == TRL_FAILED);
    TRL_CHECK(test, unable(trl_settings_failure(settings), "a connection URI begins with turborel://"));
    trl_settings_free(settings);
    TRL_CHECK(test, trl_settings_from_uri("turborel://b", &settings) == TRL_FAILED);
    TRL_CHECK(test, unable(trl_settings_failure(settings), "no user is named"));
    trl_settings_free(settings);
}

/**
 * Verifies: [CLIENT Where settings come from] a setting the program gives
 * prevails over the service's, which prevails over the environment's,
 * which prevails over the default; the environment names the service and
 * its file when the program does not; an empty variable gives nothing.
 */
static void settings_are_laid_program_service_environment_default(trl_test *test) {
    char file[PATH_BYTES];
    TRL_CHECK(test,
              scratch("precedence",
                      "# servers\n[prod]\nhost = prod-db\nport = 7500\ntls_mode = require\n\n[test]\nhost = test-db\n",
                      file));
    environment env = {
        {
            "TURBOREL_SERVICE",
            "TURBOREL_SERVICE_FILE",
            "TURBOREL_USER",
            "TURBOREL_PORT",
            "TURBOREL_TLS_MODE",
            "TURBOREL_APPLICATION_NAME",
        },
        {
            "prod",
            file,
            "env-user",
            "7600",
            "disable",
            "",
        },
        6U,
    };
    trl_options *program = NULL;
    (void)trl_options_new(&program);
    TRL_CHECK(test, trl_options_set(program, TRL_SETTING_TLS_MODE, "verify-ca") == TRL_OK);
    trl_settings *settings = laid(program, &env);
    TRL_CHECK(test, settings != NULL && trl_settings_host_count(settings) == 1U);
    TRL_CHECK(test, host_is(settings, 0U, "prod-db", 7500U, false));
    TRL_CHECK(test, trl_settings_tls_mode(settings) == TRL_TLS_VERIFY_CA);
    TRL_CHECK(test, strcmp(trl_settings_user(settings), "env-user") == 0);
    TRL_CHECK(test, trl_settings_get(settings, TRL_SETTING_APPLICATION_NAME) == NULL);
    TRL_CHECK(test, trl_settings_channel_binding(settings) == TRL_CHANNEL_BINDING_PREFER);
    trl_settings_free(settings);
    TRL_CHECK(test, trl_options_set(program, TRL_SETTING_SERVICE, "test") == TRL_OK);
    settings = laid(program, &env);
    TRL_CHECK(test, settings != NULL && host_is(settings, 0U, "test-db", 7600U, false));
    trl_settings_free(settings);
    trl_options_free(program);
    (void)remove(file);
}

/* Whether the service file `text`'s section `name` fails, saying `why`. */
static bool service_refused(const char *text, const char *name, const char *why) {
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    trl_failure failure;
    trl_failure_init(&failure);
    bool refused = !trl_service_parse(text, name, "services.conf", options, &failure) && unable(&failure, why);
    trl_failure_clear(&failure);
    trl_options_free(options);
    return refused;
}

/**
 * Verifies: [CLIENT The service file] a service file's section is its
 * lines' settings; a section that is not there, a setting before any
 * section, a line without `=`, a key that names no setting, and a service
 * or service file set by a service are refused, naming the line; a file
 * that cannot be read is refused.
 */
static void a_service_file_is_read_by_section(trl_test *test) {
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    trl_failure failure;
    trl_failure_init(&failure);
    TRL_CHECK(test, trl_service_parse("[a]\nuser=ann\n[b]\nuser = bob\n  host =  h  \n", "b", "services.conf", options,
                                      &failure));
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_USER), "bob") == 0);
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_HOST), "h") == 0);
    trl_options_free(options);
    TRL_CHECK(test, service_refused("[a]\nuser=ann\n", "b", "services.conf has no service b"));
    TRL_CHECK(test, service_refused("user=ann\n", "a", "line 1: a setting comes before any section"));
    TRL_CHECK(test, service_refused("[a]\nuser\n", "a", "line 2: a setting is <key> = <value>"));
    TRL_CHECK(test, service_refused("[a]\ncolour=red\n", "a", "line 2: colour is no connection setting"));
    TRL_CHECK(test, service_refused("[a]\nservice=b\n", "a", "line 2: a service cannot set service"));
    TRL_CHECK(test, service_refused("[a\n", "a", "line 1: a section's name ends with ]"));
    char missing[PATH_BYTES];
    (void)snprintf(missing, sizeof missing, "settings-no-service-%d", (int)getpid());
    (void)trl_options_new(&options);
    TRL_CHECK(test, !trl_service_read(missing, "s", options, &failure) && unable(&failure, "cannot read"));
    trl_options_free(options);
    trl_failure_clear(&failure);
}

/**
 * Verifies: [CLIENT The password file] the password file gives the
 * password of its first line matching host, port and user, `*` matching
 * any, `\:` and `\\` taken as they are, an escaped `\*` matching only
 * `*`; comments and short lines are skipped; the settings' own password
 * prevails over it; a file that cannot be read gives none.
 */
static void a_password_file_matches_by_host_port_and_user(trl_test *test) {
    static const char text[] =
        "# passwords\nshort:line\ndb1:7422:ann:first\n*:*:ann:any\\:host\ndb\\:2:*:bob:c:\\\\d\n\\*:1:carl:star\n";
    TRL_CHECK(test, password_is(text, "db1", "7422", "ann", "first"));
    TRL_CHECK(test, password_is(text, "db9", "1", "ann", "any:host"));
    TRL_CHECK(test, password_is(text, "db:2", "7", "bob", "c:\\d"));
    TRL_CHECK(test, password_is(text, "x", "1", "carl", NULL));
    TRL_CHECK(test, password_is(text, "*", "1", "carl", "star"));
    TRL_CHECK(test, password_is(text, "db1", "7422", "nobody", NULL));
    char file[PATH_BYTES];
    TRL_CHECK(test, scratch("passfile", text, file));
    trl_options *program = NULL;
    (void)trl_options_new(&program);
    (void)trl_options_set(program, TRL_SETTING_USER, "ann");
    (void)trl_options_set(program, TRL_SETTING_PASSFILE, file);
    (void)trl_options_set(program, TRL_SETTING_HOST, "db1");
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = laid(program, &none);
    char password[32];
    size_t length = 0U;
    TRL_CHECK(test, trl_settings_password_for(settings, 0U, password, sizeof password, &length) &&
                        strcmp(password, "first") == 0 && length == 5U);
    (void)trl_options_set(program, TRL_SETTING_PASSWORD, "given");
    trl_settings *given = laid(program, &none);
    TRL_CHECK(test,
              trl_settings_password_for(given, 0U, password, sizeof password, NULL) && strcmp(password, "given") == 0);
    (void)remove(file);
    TRL_CHECK(test, !trl_settings_password_for(settings, 0U, password, sizeof password, NULL));
    TRL_CHECK(test, !trl_settings_password_for(settings, 1U, password, sizeof password, NULL));
    trl_settings_free(given);
    trl_settings_free(settings);
    trl_options_free(program);
}

/**
 * Verifies: [CLIENT Where settings come from] every variable of the
 * environment gives its setting.
 */
static void the_environment_and_words_are_read(trl_test *test) {
    static const char *const variables[] = {
        "TURBOREL_HOST",
        "TURBOREL_PORT",
        "TURBOREL_USER",
        "TURBOREL_PASSWORD",
        "TURBOREL_TLS_MODE",
        "TURBOREL_APPLICATION_NAME",
        "TURBOREL_CONNECT_TIMEOUT",
        "TURBOREL_SERVICE",
        "TURBOREL_SERVICE_FILE",
        "TURBOREL_PASSFILE",
    };
    static const char *const keys[] = {
        TRL_SETTING_HOST,
        TRL_SETTING_PORT,
        TRL_SETTING_USER,
        TRL_SETTING_PASSWORD,
        TRL_SETTING_TLS_MODE,
        TRL_SETTING_APPLICATION_NAME,
        TRL_SETTING_CONNECT_TIMEOUT,
        TRL_SETTING_SERVICE,
        TRL_SETTING_SERVICE_FILE,
        TRL_SETTING_PASSFILE,
    };
    environment every = {{NULL}, {NULL}, 0U};
    for (size_t i = 0U; i < sizeof variables / sizeof variables[0]; ++i) {
        every.names[i] = variables[i];
        every.values[i] = "v";
        ++every.count;
    }
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    TRL_CHECK(test, trl_options_environment(options, variable, &every));
    for (size_t i = 0U; i < sizeof keys / sizeof keys[0]; ++i) {
        TRL_CHECK(test, strcmp(trl_options_get(options, keys[i]), "v") == 0);
    }
    trl_options_free(options);
}

/**
 * Verifies: [CLIENT Where settings come from] the words of each kind are
 * read as named, one no setting takes refused, naming those it takes; a
 * list of hosts takes a list of ports, one each; a key that names no
 * setting is refused (SQLSTATE 08001).
 */
static void words_and_lists_are_read(trl_test *test) {
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    (void)trl_options_set(options, TRL_SETTING_HOST, "a,b");
    (void)trl_options_set(options, TRL_SETTING_PORT, "1,2");
    (void)trl_options_set(options, TRL_SETTING_USER, "u");
    (void)trl_options_set(options, TRL_SETTING_TLS_SYSTEM_ROOTS, "true");
    (void)trl_options_set(options, TRL_SETTING_CHANNEL_BINDING, "require");
    (void)trl_options_set(options, TRL_SETTING_LOAD_BALANCE, "random");
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = laid(options, &none);
    TRL_CHECK(test, host_is(settings, 0U, "a", 1U, false) && host_is(settings, 1U, "b", 2U, false));
    TRL_CHECK(test, trl_settings_tls_system_roots(settings));
    TRL_CHECK(test, trl_settings_channel_binding(settings) == TRL_CHANNEL_BINDING_REQUIRE);
    TRL_CHECK(test, trl_settings_load_balance(settings) == TRL_LOAD_BALANCE_RANDOM);
    trl_settings_free(settings);
    (void)trl_options_set(options, TRL_SETTING_TARGET, "primary");
    settings = laid(options, &none);
    TRL_CHECK(test, unable(trl_settings_failure(settings), "target is primary, not one of any, read-write, read-only"));
    trl_settings_free(settings);
    TRL_CHECK(test, trl_options_set(options, "colour", "red") == TRL_FAILED);
    TRL_CHECK(test, unable(trl_options_failure(options), "colour is no connection setting"));
    trl_options_free(options);
}

/* Whether settings of `pairs`, a user given, fail as unable to connect,
   or give `hosts` hosts. */
static bool hosts_laid(const char *const pairs[][2], size_t count, size_t *hosts) {
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    (void)trl_options_set(options, TRL_SETTING_USER, "u");
    for (size_t i = 0U; i < count; ++i) {
        (void)trl_options_set(options, pairs[i][0], pairs[i][1]);
    }
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = NULL;
    trl_status status = trl_settings_resolve_in(options, variable, &none, &settings);
    *hosts = trl_settings_host_count(settings);
    bool fine = status == TRL_OK || unable(trl_settings_failure(settings), "");
    trl_settings_free(settings);
    trl_options_free(options);
    return fine && status == TRL_OK;
}

/**
 * Verifies: [CLIENT Where settings come from] a list of hosts with an
 * empty one, an IPv6 address whose bracket is not closed or whose port is
 * no number, a list of ports with one that is no number, and a word no
 * channel binding, target, load balance or truth takes are each refused
 * (SQLSTATE 08001); several hosts of one port each take it.
 */
static void malformed_hosts_and_words_are_refused(trl_test *test) {
    size_t hosts = 0U;
    static const char *const empty[][2] = {{TRL_SETTING_HOST, "a,,b"}};
    static const char *const open_bracket[][2] = {{TRL_SETTING_HOST, "[::1"}};
    static const char *const bad_port[][2] = {{TRL_SETTING_HOST, "[::1]:x"}};
    static const char *const bad_ports[][2] = {{TRL_SETTING_HOST, "a"}, {TRL_SETTING_PORT, "1,x"}};
    TRL_CHECK(test, !hosts_laid(empty, 1U, &hosts) && !hosts_laid(open_bracket, 1U, &hosts));
    TRL_CHECK(test, !hosts_laid(bad_port, 1U, &hosts) && !hosts_laid(bad_ports, 2U, &hosts));
    static const char *const words[] = {
        TRL_SETTING_CHANNEL_BINDING,
        TRL_SETTING_TARGET,
        TRL_SETTING_LOAD_BALANCE,
        TRL_SETTING_TLS_SYSTEM_ROOTS,
    };
    for (size_t i = 0U; i < sizeof words / sizeof words[0]; ++i) {
        const char *pair[][2] = {{words[i], "sometimes"}};
        TRL_CHECK(test, !hosts_laid(pair, 1U, &hosts));
    }
    static const char *const one_port[][2] = {{TRL_SETTING_HOST, "a,b,[::1]"}, {TRL_SETTING_PORT, "9"}};
    TRL_CHECK(test, hosts_laid(one_port, 2U, &hosts) && hosts == 3U);
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    (void)trl_options_set(options, TRL_SETTING_USER, "u");
    (void)trl_options_set(options, TRL_SETTING_HOST, "a,b,[::1]");
    (void)trl_options_set(options, TRL_SETTING_PORT, "9");
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = laid(options, &none);
    TRL_CHECK(test, host_is(settings, 0U, "a", 9U, false) && host_is(settings, 2U, "::1", 9U, false));
    trl_settings_free(settings);
    trl_options_free(options);
}

/**
 * Verifies: [CLIENT The password file] the password file is matched for a
 * local socket by its path and the default port.
 */
static void a_password_file_matches_a_local_socket_by_its_path(trl_test *test) {
    char file[PATH_BYTES];
    TRL_CHECK(test, scratch("passfile-local", "/run/s.sock:7422:ann:local\n", file));
    trl_options *program = NULL;
    (void)trl_options_new(&program);
    (void)trl_options_set(program, TRL_SETTING_USER, "ann");
    (void)trl_options_set(program, TRL_SETTING_PASSFILE, file);
    (void)trl_options_set(program, TRL_SETTING_HOST, "/run/s.sock");
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = laid(program, &none);
    char password[16];
    TRL_CHECK(test, trl_settings_password_for(settings, 0U, password, sizeof password, NULL) &&
                        strcmp(password, "local") == 0);
    trl_settings_free(settings);
    trl_options_free(program);
    (void)remove(file);
}

/**
 * Verifies: [DESIGN] D9 null arguments are refused, and accessors of null
 * settings give their defaults.
 */
static void null_arguments_are_refused(trl_test *test) {
    trl_options *options = NULL;
    trl_settings *settings = NULL;
    TRL_CHECK(test, trl_options_new(NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_options_set(NULL, "host", "a") == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_options_get(NULL, "host") == NULL && trl_options_failure(NULL) == NULL);
    TRL_CHECK(test, trl_options_from_uri(NULL, &options) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_settings_resolve_in(NULL, NULL, NULL, &settings) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_settings_from_uri(NULL, &settings) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_settings_connect(NULL, NULL) == TRL_INVALID_ARGUMENT);
    TRL_CHECK(test, trl_settings_host_count(NULL) == 0U && trl_settings_host(NULL, 0U, NULL, NULL) == NULL);
    TRL_CHECK(test, trl_settings_user(NULL) == NULL && trl_settings_get(NULL, "host") == NULL);
    TRL_CHECK(test, trl_settings_tls_mode(NULL) == TRL_TLS_PREFER && trl_settings_target(NULL) == TRL_TARGET_ANY);
    TRL_CHECK(test, trl_settings_channel_binding(NULL) == TRL_CHANNEL_BINDING_PREFER);
    TRL_CHECK(test,
              trl_settings_load_balance(NULL) == TRL_LOAD_BALANCE_DISABLE && !trl_settings_tls_system_roots(NULL));
    TRL_CHECK(test, trl_settings_connect_timeout(NULL) == 0U && trl_settings_key_cache(NULL) == NULL);
    TRL_CHECK(test, trl_settings_failure(NULL) == NULL && !trl_settings_password_for(NULL, 0U, NULL, 0U, NULL));
    trl_server_role role = TRL_SERVER_UNKNOWN;
    TRL_CHECK(test, trl_conn_role(NULL, &role) == TRL_INVALID_ARGUMENT);
    trl_options_free(NULL);
    trl_settings_free(NULL);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_uri_gives_its_user_hosts_and_settings),
        TRL_TEST_CASE(a_malformed_uri_or_setting_is_refused),
        TRL_TEST_CASE(settings_are_laid_program_service_environment_default),
        TRL_TEST_CASE(a_service_file_is_read_by_section),
        TRL_TEST_CASE(a_password_file_matches_by_host_port_and_user),
        TRL_TEST_CASE(the_environment_and_words_are_read),
        TRL_TEST_CASE(words_and_lists_are_read),
        TRL_TEST_CASE(malformed_hosts_and_words_are_refused),
        TRL_TEST_CASE(a_password_file_matches_a_local_socket_by_its_path),
        TRL_TEST_CASE(null_arguments_are_refused),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
