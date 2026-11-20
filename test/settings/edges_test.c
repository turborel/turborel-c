/**
 * @file edges_test.c
 * @brief Connection settings at their edges: spaces, signs and bounds,
 * either case of percent-encoding, line ends of CR LF, and hosts put in
 * an order drawn at random.
 */
#include "failure.h"
#include "harness.h"
#include "settings/settings.h"
#include "settings_common.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Verifies: [CLIENT Where settings come from] spaces about a list's items
 * and ports are passed over, a port may be written with a plus and is at
 * most 65535, a connect timeout of 0 sets no limit and a long one is
 * kept at most about 49 days.
 */
static void edges_are_read_as_turborel_client_reads_them(trl_test *test) {
    trl_options *options = NULL;
    (void)trl_options_new(&options);
    (void)trl_options_set(options, TRL_SETTING_USER, "u");
    (void)trl_options_set(options, TRL_SETTING_HOST, " a , \tb ");
    (void)trl_options_set(options, TRL_SETTING_PORT, " +1 , 2\t");
    (void)trl_options_set(options, TRL_SETTING_CONNECT_TIMEOUT, "0");
    environment none = {{NULL}, {NULL}, 0U};
    trl_settings *settings = laid(options, &none);
    TRL_CHECK(test, host_is(settings, 0U, "a", 1U, false) && host_is(settings, 1U, "b", 2U, false));
    TRL_CHECK(test,
              trl_settings_connect_timeout(settings) == 0U && trl_settings_host(settings, 0U, NULL, NULL) != NULL);
    TRL_CHECK(test, trl_options_get(options, "colour") == NULL && trl_settings_get(settings, "colour") == NULL);
    trl_settings_free(settings);
    (void)trl_options_set(options, TRL_SETTING_PORT, "70000");
    settings = laid(options, &none);
    TRL_CHECK(test, unable(trl_settings_failure(settings), "port is 70000, no port"));
    trl_settings_free(settings);
    (void)trl_options_set(options, TRL_SETTING_PORT, "99999999999999999999999");
    settings = laid(options, &none);
    TRL_CHECK(test, unable(trl_settings_failure(settings), "no port"));
    trl_settings_free(settings);
    (void)trl_options_set(options, TRL_SETTING_PORT, "1");
    (void)trl_options_set(options, TRL_SETTING_CONNECT_TIMEOUT, "99999999");
    settings = laid(options, &none);
    TRL_CHECK(test, trl_settings_connect_timeout(settings) == UINT32_MAX);
    trl_settings_free(settings);
    trl_options_free(options);
}

/**
 * Verifies: [CLIENT The connection URI] a URI's percent-encoding takes
 * either case of hexadecimal digit, an empty pair of its query passed
 * over, and a password given back only by trl_settings_password_for(),
 * whole or cut short [CLIENT The password file];
 * a service file's lines may end in CR LF, a section named twice its
 * settings merged, the later prevailing [CLIENT The service file]; a
 * password file's lines may end in CR LF, a comment indented.
 */
static void uris_and_files_at_their_edges(trl_test *test) {
    trl_options *options = NULL;
    environment none = {{NULL}, {NULL}, 0U};
    TRL_CHECK(test, trl_options_from_uri("turborel://%2fs%2Ftmp@/", &options) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_USER), "/s/tmp") == 0);
    trl_options_free(options);
    TRL_CHECK(test, trl_options_from_uri("turborel://a@b/?host=c%2", &options) == TRL_FAILED);
    trl_options_free(options);
    TRL_CHECK(test, trl_options_from_uri("turborel://%g1@b", &options) == TRL_FAILED);
    trl_options_free(options);
    TRL_CHECK(test, trl_options_from_uri("turborel://a:p%41ss@b/?user=c&&", &options) == TRL_OK);
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_PASSWORD), "pAss") == 0);
    trl_settings *given = NULL;
    TRL_CHECK(test, trl_settings_resolve_in(options, variable, &none, &given) == TRL_OK);
    size_t whole = 0U;
    char cut[3];
    TRL_CHECK(test, trl_settings_password_for(given, 0U, NULL, 0U, &whole) && whole == 4U);
    TRL_CHECK(test, trl_settings_password_for(given, 0U, cut, sizeof cut, NULL) && strcmp(cut, "pA") == 0);
    TRL_CHECK(test, trl_settings_get(given, TRL_SETTING_PASSWORD) == NULL);
    TRL_CHECK(test, strcmp(trl_settings_get(given, TRL_SETTING_USER), "c") == 0);
    trl_settings_free(given);
    trl_options_free(options);
    (void)trl_options_new(&options);
    trl_failure failure;
    trl_failure_init(&failure);
    TRL_CHECK(test, trl_service_parse("\v # c\f\r\n[ a ]\r\nuser = x\r\n[b]\r\n[a]\r\nhost = h\r\nuser = y", "a",
                                      "services.conf", options, &failure));
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_USER), "y") == 0);
    TRL_CHECK(test, strcmp(trl_options_get(options, TRL_SETTING_HOST), "h") == 0);
    trl_failure_clear(&failure);
    trl_options_free(options);
    TRL_CHECK(test, password_is("  # db:1:u:no\r\ndb:1:u:yes\r\n", "db", "1", "u", "yes"));
    TRL_CHECK(test, password_is("db:1:u:p", "db", "1", "u", "p"));
    TRL_CHECK(test, password_is("", "db", "1", "u", NULL));
}

/**
 * Verifies: [CLIENT Several servers] hosts ordered at random are each
 * host once, in an order the seed decides, the same for the same seed;
 * different seeds give different orders; one host or none stays as it is.
 */
static void hosts_are_shuffled_by_their_seed(trl_test *test) {
    size_t first[8];
    size_t again[8];
    size_t distinct = 0U;
    size_t previous[8] = {0};
    for (uint64_t seed = 0U; seed < 16U; ++seed) {
        for (size_t i = 0U; i < 8U; ++i) {
            first[i] = i;
            again[i] = i;
        }
        trl_shuffle(first, 8U, seed);
        trl_shuffle(again, 8U, seed);
        TRL_CHECK(test, memcmp(first, again, sizeof first) == 0);
        size_t seen = 0U;
        for (size_t i = 0U; i < 8U; ++i) {
            seen |= (size_t)1U << first[i];
        }
        TRL_CHECK(test, seen == 0xFFU);
        distinct += memcmp(first, previous, sizeof first) != 0 ? 1U : 0U;
        memcpy(previous, first, sizeof first);
    }
    TRL_CHECK(test, distinct > 8U);
    size_t one[1] = {0U};
    trl_shuffle(one, 1U, 3U);
    trl_shuffle(one, 0U, 3U);
    TRL_CHECK(test, one[0] == 0U);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(edges_are_read_as_turborel_client_reads_them),
        TRL_TEST_CASE(uris_and_files_at_their_edges),
        TRL_TEST_CASE(hosts_are_shuffled_by_their_seed),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
