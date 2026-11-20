/**
 * @file server.c
 * @brief The live tests' server, started and stopped by the test that
 * uses it, on Linux and macOS, where turboreld is (R-PLAT-4): Windows
 * runs no live test.
 */
#ifndef __APPLE__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
#define _POSIX_C_SOURCE 200809L
#endif

#include "server.h"

#include "certs.h"

/* The platform's and the TLS library's headers declare more than the
   include checker can map to their own names (R-LINT-3). */
// NOLINTBEGIN(misc-include-cleaner)
#include "format.h"

#include <arpa/inet.h> // IWYU pragma: keep
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h> // IWYU pragma: keep
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define MAX_WORDS 64U
#define WORD_BYTES 4096U
#define POLL_NS 50000000L
#define START_POLLS 1200U
#define STOP_POLLS 200U
#define DIR_MODE 0700

/* Waits a poll's while. */
static void pause_a_while(void) {
    struct timespec wait = {0, POLL_NS};
    (void)nanosleep(&wait, NULL);
}

void server_path(const server *s, const char *name, char *out, uint32_t capacity) {
    trl_format(out, capacity, "%s/%s", s->dir, name);
}

/* Whether the server takes TCP connections, and its socket is there. */
static bool listening(const server *s) {
    struct stat made;
    if (stat(s->socket, &made) != 0) {
        return false;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool connected = fd >= 0 && connect(fd, (const struct sockaddr *)&address, sizeof address) == 0;
    if (fd >= 0) {
        (void)close(fd);
    }
    return connected;
}

/* A command's words being built, each a copy in its storage. */
typedef struct words {
    char *of[MAX_WORDS];
    size_t count;
    char storage[WORD_BYTES];
    size_t used;
} words;

/* Appends `word`. */
static void add_word(words *w, const char *word) {
    size_t length = strlen(word);
    if (w->count + 1U >= MAX_WORDS || w->used + length + 1U > WORD_BYTES) {
        return;
    }
    memcpy(w->storage + w->used, word, length + 1U);
    w->of[w->count++] = w->storage + w->used;
    w->used += length + 1U;
}

/* Appends `--set NAME=VALUE`; a VALUE written `@file` is that file of the
   server's directory. */
static void add_setting(const server *s, words *w, const char *setting) {
    add_word(w, "--set");
    char text[SERVER_PATH_BYTES * 2U];
    const char *equals = strchr(setting, '=');
    if (equals != NULL && equals[1] == '@') {
        trl_format(text, sizeof text, "%.*s%s/%s", (int)(equals - setting + 1), setting, s->dir, equals + 2);
    } else {
        trl_format(text, sizeof text, "%s", setting);
    }
    add_word(w, text);
}

/* The command's words, split at spaces, then the server's options. */
static void command(const server *s, const char *named, words *w) {
    w->count = 0U;
    w->used = 0U;
    size_t length = strlen(named);
    size_t begun = 0U;
    for (size_t i = 0U; i <= length; ++i) {
        if (i == length || named[i] == ' ') {
            char word[WORD_BYTES];
            if (i > begun && i - begun < sizeof word) {
                memcpy(word, named + begun, i - begun);
                word[i - begun] = '\0';
                add_word(w, word);
            }
            begun = i + 1U;
        }
    }
    static const char *const settings[] = {
        "SERVER.HOST=127.0.0.1",
        "SERVER.PORT=17438",
        "SERVER.SOCKET=@turborel.sock",
        "SERVER.TLS=ON",
        "SERVER.TLS_CERTIFICATE=@server.pem",
        "SERVER.TLS_KEY=@server.key",
        "SERVER.TLS_CLIENT_CA=@ca.pem",
        "SERVER.TRUST_LOCAL=ON",
        "SERVER.LOGIN_DELAY_MS=0",
        "SERVER.SCRAM_WITHOUT_TLS=ON",
    };
    add_word(w, "--memory");
    add_word(w, "--owner");
    add_word(w, "ann");
    for (size_t i = 0U; i < sizeof settings / sizeof settings[0]; ++i) {
        add_setting(s, w, settings[i]);
    }
    w->of[w->count] = NULL;
}

/* Spawns the command, its input a pipe whose other end `input` gets, its
   output and errors to the log. */
static bool spawn(server *s, char **argv, int *input) {
    int ends[2];
    if (pipe(ends) != 0) {
        return false;
    }
    char log[SERVER_PATH_BYTES];
    server_path(s, "server.log", log, sizeof log);
    posix_spawn_file_actions_t actions;
    (void)posix_spawn_file_actions_init(&actions);
    (void)posix_spawn_file_actions_adddup2(&actions, ends[0], STDIN_FILENO);
    (void)posix_spawn_file_actions_addclose(&actions, ends[1]);
    (void)posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    (void)posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t pid = 0;
    int spawned = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    (void)posix_spawn_file_actions_destroy(&actions);
    (void)close(ends[0]);
    if (spawned != 0) {
        (void)close(ends[1]);
        return false;
    }
    s->pid = (long)pid;
    *input = ends[1];
    return true;
}

/* Waits for the server to listen, or to stop. */
static bool wait_listening(server *s, const char **why) {
    for (unsigned int i = 0U; i < START_POLLS; ++i) {
        int status = 0;
        if (waitpid((pid_t)s->pid, &status, WNOHANG) == (pid_t)s->pid) {
            s->pid = 0;
            *why = "the server stopped as it started; its log says why";
            return false;
        }
        if (listening(s)) {
            return true;
        }
        pause_a_while();
    }
    *why = "the server did not listen in time";
    server_stop(s);
    return false;
}

bool server_start(server *s, const char *dir, const char **why) {
    memset(s, 0, sizeof *s);
    const char *named = getenv("TRL_TEST_SERVER"); // NOLINT(concurrency-mt-unsafe)
    if (named == NULL || named[0] == '\0') {
        *why = "TRL_TEST_SERVER names no server, so the live tests are skipped";
        return false;
    }
    trl_format(s->dir, sizeof s->dir, "%s", dir);
    server_path(s, "turborel.sock", s->socket, sizeof s->socket);
    if ((mkdir(s->dir, DIR_MODE) != 0 && errno != EEXIST) || (unlink(s->socket) != 0 && errno != ENOENT)) {
        *why = "the server's directory cannot be made";
        return false;
    }
    if (!certs_make(s->dir)) {
        *why = "the test certificates cannot be made";
        return false;
    }
    static words w;
    command(s, named, &w);
    int input = -1;
    if (!spawn(s, w.of, &input)) {
        *why = "the server cannot be started";
        return false;
    }
    static const char password[] = SERVER_PASSWORD "\n";
    bool told = write(input, password, sizeof password - 1U) == (ssize_t)(sizeof password - 1U);
    (void)close(input);
    if (!told) {
        *why = "the owner's password cannot be given";
        server_stop(s);
        return false;
    }
    return wait_listening(s, why);
}

void server_stop(server *s) {
    if (s->pid <= 0) {
        return;
    }
    pid_t pid = (pid_t)s->pid;
    (void)kill(pid, SIGTERM);
    for (unsigned int i = 0U; i < STOP_POLLS; ++i) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            s->pid = 0;
            return;
        }
        pause_a_while();
    }
    (void)kill(pid, SIGKILL);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    s->pid = 0;
}
// NOLINTEND(misc-include-cleaner)
