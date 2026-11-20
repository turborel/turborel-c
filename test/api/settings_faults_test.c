/**
 * @file settings_faults_test.c
 * @brief Memory not had in connection settings (RULES.md R-MEM-2): a URI
 * read, settings laid over a service file and the environment, a password
 * looked up, and a connection tried at servers that cannot be reached,
 * each allocation failing in turn, never crashing or leaking. Built with
 * TRL_FAULTS alone.
 */
#include "faults.h"
#include "harness.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* The service file and password file of the scenario. */
#define SERVICES "settings-faults-services"
#define PASSWORDS "settings-faults-passwords"

/* The environment of the scenario: its service file and service. */
static const char *variable(void *context, const char *name) {
    (void)context;
    if (strcmp(name, "TURBOREL_SERVICE_FILE") == 0) {
        return SERVICES;
    }
    return strcmp(name, "TURBOREL_SERVICE") == 0 ? "prod" : NULL;
}

/* Whether a status is one memory not had allows, of settings laid. */
static bool laid_fine(trl_status status, const trl_settings *settings) {
    return status == TRL_OK || status == TRL_NO_MEMORY ||
           (status == TRL_FAILED && trl_failure_sqlcode(trl_settings_failure(settings)) == -918);
}

/* A URI read and laid, a password looked up, and a connection tried. */
static bool settings_session(void) {
    static const char uri[] = "turborel://ann@127.0.0.1:1,[::1]:2,%2Fno%2Fsuch.sock/?tls_mode=disable"
                              "&application_name=faults&connect_timeout=2&target=read-write&load_balance=random"
                              "&passfile=" PASSWORDS;
    trl_settings *settings = NULL;
    trl_status status = trl_settings_from_uri(uri, &settings);
    bool fine = laid_fine(status, settings);
    if (status == TRL_OK) {
        char password[32];
        (void)trl_settings_password_for(settings, 0U, password, sizeof password, NULL);
        trl_conn *conn = NULL;
        trl_status connected = trl_settings_connect(settings, &conn);
        fine = fine && (connected == TRL_FAILED || connected == TRL_NO_MEMORY);
        trl_conn_free(conn);
    }
    trl_settings_free(settings);
    trl_options *options = NULL;
    if (trl_options_new(&options) == TRL_OK) {
        trl_status set = trl_options_set(options, TRL_SETTING_TLS_MODE, "require");
        fine = fine && (set == TRL_OK || set == TRL_NO_MEMORY);
        settings = NULL;
        status = trl_settings_resolve_in(options, variable, NULL, &settings);
        fine = fine && laid_fine(status, settings);
        trl_settings_free(settings);
    }
    trl_options_free(options);
    return fine;
}

/**
 * Verifies: [TOOL] R-MEM-2 every allocation of a URI read, settings laid
 * over a service file and the environment, a password looked up and a
 * connection tried failing in turn fails the call as memory not had, or
 * as it fails without a fault, and leaks nothing.
 */
static void settings_survive_each_fault(trl_test *test) {
    bool written = trl_test_write_file(SERVICES, "[prod]\nuser = ann\nhost = 127.0.0.1:1\n") &&
                   trl_test_write_file(PASSWORDS, "127.0.0.1:1:ann:secret\n");
    TRL_CHECK(test, written && every_fault(settings_session));
    (void)remove(SERVICES);
    (void)remove(PASSWORDS);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(settings_survive_each_fault),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
