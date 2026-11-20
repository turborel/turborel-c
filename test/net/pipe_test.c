/**
 * @file pipe_test.c
 * @brief Named pipes on Windows [CLIENT Timeouts] [CLIENT Connecting]:
 * one not there refused as the system says, and one every instance of
 * which is busy waited for within the connect timeout alone. CMake builds
 * it on Windows alone.
 */
#include "harness.h"
#include "turborel/turborel.h"

/* Windows declares its pipes in headers of its own, which the include
   checker does not know (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#define WIN32_LEAN_AND_MEAN
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define PIPE_BYTES 4096U
#define SHORT_MS 100U
#define NAME_BYTES 128U

/* Connects to the pipe `name` by trust, waiting `ms` while it is busy. */
static trl_status connect_pipe(const char *name, uint32_t ms, trl_conn **conn) {
    trl_target *target = NULL;
    trl_mechanism *trust = NULL;
    trl_conn_options *options = NULL;
    (void)trl_target_local(name, &target);
    (void)trl_mechanism_trust(&trust);
    (void)trl_conn_options_new(&options);
    (void)trl_conn_options_timeouts(options, ms, TRL_NO_LIMIT);
    trl_status made = trl_connect(target, "ann", trust, options, conn);
    trl_conn_options_free(options);
    trl_mechanism_free(trust);
    trl_target_free(target);
    return made;
}

/**
 * Verifies: [CLIENT Connecting] a named pipe not there is refused, as the
 * system says it (SQLSTATE 08006).
 */
static void a_pipe_not_there_is_refused(trl_test *test) {
    trl_conn *conn = NULL;
    TRL_CHECK(test, connect_pipe("\\\\.\\pipe\\turborel-c-none", SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, strncmp(trl_failure_message(trl_conn_failure(conn)), "cannot connect: ", 16U) == 0);
    TRL_CHECK(test, strcmp(trl_failure_sqlstate(trl_conn_failure(conn)), "08006") == 0);
    trl_conn_free(conn);
}

/**
 * Verifies: [CLIENT Timeouts] a named pipe every instance of which is
 * busy is waited for within the connect timeout, then refused, saying the
 * server did not answer in time.
 */
static void a_busy_pipe_is_waited_for_in_time(trl_test *test) {
    char name[NAME_BYTES];
    (void)snprintf(name, sizeof name, "\\\\.\\pipe\\turborel-c-busy-%lu", (unsigned long)GetCurrentProcessId());
    HANDLE server =
        CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1U, PIPE_BYTES, PIPE_BYTES, 0U, NULL);
    TRL_CHECK(test, server != INVALID_HANDLE_VALUE);
    HANDLE holder = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0U, NULL, OPEN_EXISTING, 0U, NULL);
    TRL_CHECK(test, holder != INVALID_HANDLE_VALUE);
    trl_conn *conn = NULL;
    TRL_CHECK(test, connect_pipe(name, SHORT_MS, &conn) == TRL_FAILED);
    TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "the server did not answer in time") != NULL);
    TRL_CHECK(test, strstr(trl_failure_message(trl_conn_failure(conn)), "every instance of the pipe is busy") != NULL);
    trl_conn_free(conn);
    (void)CloseHandle(holder);
    (void)CloseHandle(server);
}

int main(int argc, char **argv) {
    static const trl_test_case cases[] = {
        TRL_TEST_CASE(a_pipe_not_there_is_refused),
        TRL_TEST_CASE(a_busy_pipe_is_waited_for_in_time),
    };
    return trl_test_main(argc, argv, cases, TRL_TEST_COUNT(cases));
}
// NOLINTEND(misc-include-cleaner)
