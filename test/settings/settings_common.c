/**
 * @file settings_common.c
 * @brief What the settings tests share.
 */
#include "settings_common.h"

#include "codec/buffer.h"
#include "harness.h"
#include "settings/settings.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

const char *variable(void *context, const char *name) {
    const environment *e = context;
    for (size_t i = 0U; i < e->count; ++i) {
        if (strcmp(e->names[i], name) == 0) {
            return e->values[i];
        }
    }
    return NULL;
}

/* A scratch file of `text`, named for `test`, in the build directory;
   its path into `path`. */
bool scratch(const char *test, const char *text, char *path) {
    (void)snprintf(path, PATH_BYTES, "settings-%s-%d", test, (int)getpid());
    return trl_test_write_file(path, text);
}

/* The settings `options` give, laid in `env`. */
trl_settings *laid(const trl_options *options, environment *env) {
    trl_settings *settings = NULL;
    (void)trl_settings_resolve_in(options, variable, env, &settings);
    return settings;
}

/* Whether host `i` of `settings` is `name` at `port`, local if `local`. */
bool host_is(const trl_settings *settings, size_t i, const char *name, uint16_t port, bool local) {
    uint16_t at = 1U;
    bool is_local = !local;
    const char *got = trl_settings_host(settings, i, &at, &is_local);
    bool same = got != NULL && strcmp(got, name) == 0 && at == port && is_local == local;
    if (!same) {
        (void)fprintf(stderr, "host %zu: %s:%u\n", i, got == NULL ? "(none)" : got, (unsigned int)at);
    }
    return same;
}

/* Whether `failure` is SQLSTATE 08001, its message holding `part`. */
bool unable(const trl_failure *failure, const char *part) {
    bool same = failure != NULL && strcmp(trl_failure_sqlstate(failure), "08001") == 0 &&
                strstr(trl_failure_message(failure), part) != NULL;
    if (!same) {
        (void)fprintf(stderr, "%s: %s\n", trl_failure_sqlstate(failure), trl_failure_message(failure));
    }
    return same;
}

/* Whether the password file `text` gives `expected` to `user` at `host`
   and `port`, or none if `expected` is null. */
bool password_is(const char *text, const char *host, const char *port, const char *user, const char *expected) {
    trl_buffer out;
    trl_buffer_init(&out);
    bool found = trl_password_find(text, host, port, user, &out);
    trl_put_u8(&out, 0U);
    bool same = expected == NULL ? !found : found && strcmp((const char *)out.data, expected) == 0;
    trl_buffer_free(&out);
    return same;
}
