/**
 * @file files.c
 * @brief The files connection settings come from: a service file of named
 * sections of settings [CLIENT The service file], and a password file of
 * passwords by server and user [CLIENT The password file].
 *
 * A password file should be readable by its owner alone. As
 * turborel-client, which has no code of one platform, this reads it only
 * if it is a regular file, or a link to one, and its owner answers for
 * the rest.
 */
#include "codec/buffer.h"
#include "failure.h"
#include "format.h"
#include "memory.h"
#include "net/transport.h"
#include "settings/settings.h"
#include "turborel/turborel.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* What begins a comment line, in either file. */
#define TRL_COMMENT '#'
/* What matches any value in a password file, escapes the character after
   it, and separates its fields; and the fields of its line. */
#define TRL_ANY "*"
#define TRL_ESCAPE '\\'
#define TRL_SEPARATOR ':'
#define TRL_FIELDS 4U
/* The fields a line is matched by, the host, the port and the user, before its password. */
#define TRL_MATCHED 3U
/* The longest file read, a bound against a file of no end, and the
   octets read of it at a time. */
#define TRL_MOST_FILE_BYTES 16777216U
#define TRL_CHUNK_BYTES 4096U
/* The brackets of a section's name. */
#define TRL_BRACKETS 2U

/* Whether `c` is white space, as a line's ends are trimmed of. */
static bool white(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* `text` of `length` trimmed of white space at both ends: where it begins,
   and its length in `length`. */
static const char *trimmed(const char *text, size_t *length) {
    while (*length > 0U && white(text[0])) {
        ++text;
        --*length;
    }
    while (*length > 0U && white(text[*length - 1U])) {
        --*length;
    }
    return text;
}

/* A service file's reading: the section asked for, the file, the line
   under way, whether it is in a section and that one, and where the
   section's settings go. */
typedef struct reading {
    const char *name;
    const char *file;
    size_t n;
    bool in_section;
    bool chosen;
    bool found;
    trl_options *out;
    trl_failure *failure;
} reading;

/* Fails at the line under way: why. */
static bool at_line(const reading *r, const char *why) {
    char message[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(message, sizeof message, "%s line %lu: %s", r->file, (unsigned long)r->n, why);
    trl_settings_unable(r->failure, message);
    return false;
}

/* Fails at the line under way: `what` of the key `name`. */
static bool of_key(const reading *r, const char *before, const char *name, const char *after) {
    char why[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(why, sizeof why, "%s%s%s", before, name, after);
    return at_line(r, why);
}

/* Reads a line `setting = value` of the section under way. */
static bool setting_line(reading *r, const char *line, size_t length) {
    if (!r->in_section) {
        return at_line(r, "a setting comes before any section");
    }
    const char *equals = memchr(line, '=', length);
    if (equals == NULL) {
        return at_line(r, "a setting is <key> = <value>");
    }
    size_t key_length = (size_t)(equals - line);
    const char *key = trimmed(line, &key_length);
    size_t value_length = length - (size_t)(equals - line) - 1U;
    const char *value = trimmed(equals + 1, &value_length);
    char name[TRL_FAILURE_MESSAGE_BYTES];
    trl_format(name, sizeof name, "%.*s", (int)key_length, key);
    int at = trl_setting_of(name);
    if (at == TRL_KEY_SERVICE || at == TRL_KEY_SERVICE_FILE) {
        return of_key(r, "a service cannot set ", name, "");
    }
    if (at < 0) {
        return of_key(r, "", name, " is no connection setting");
    }
    if (r->chosen && !trl_options_put(r->out, (trl_key)at, value, value_length)) {
        trl_failure_no_memory(r->failure);
        return false;
    }
    return true;
}

/* Reads one line of the service file, trimmed. */
static bool service_line(reading *r, const char *line, size_t length) {
    if (length == 0U || line[0] == TRL_COMMENT) {
        return true;
    }
    if (line[0] != '[') {
        return setting_line(r, line, length);
    }
    if (line[length - 1U] != ']') {
        return at_line(r, "a section's name ends with ]");
    }
    size_t name_length = length - TRL_BRACKETS;
    const char *name = trimmed(line + 1, &name_length);
    r->in_section = true;
    r->chosen = strlen(r->name) == name_length && strncmp(r->name, name, name_length) == 0;
    r->found = r->found || r->chosen;
    return true;
}

bool trl_service_parse(const char *text, const char *name, const char *file, trl_options *out, trl_failure *failure) {
    reading r = {name, file, 0U, false, false, false, out, failure};
    for (const char *line = text; *line != '\0' || line == text;) {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
        ++r.n;
        const char *trim = trimmed(line, &length);
        if (!service_line(&r, trim, length)) {
            return false;
        }
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
    if (!r.found) {
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "%s has no service %s", file, name);
        trl_settings_unable(failure, message);
        return false;
    }
    return true;
}

/* The file at `path` opened to read; null, with `why`, the system's code,
   if it cannot be. */
static FILE *opened(const char *path, int *why) {
#ifdef _WIN32
    FILE *file = NULL;
    if (fopen_s(&file, path, "rb") != 0) {
        /* The system's own code, as the system describes it. */
        unsigned long code = 0U;
        *why = _get_doserrno(&code) == 0 ? (int)code : 0;
        return NULL;
    }
    *why = 0;
    return file;
#else
    FILE *file = fopen(path, "rb");
    *why = file == NULL ? errno : 0;
    return file;
#endif
}

/* The whole text of the file at `path`, the caller's to free; null, with
   `why`, the system's code, if it cannot be read, 0 if memory could not
   be had. */
static char *read_whole(const char *path, int *why) {
    FILE *file = opened(path, why);
    if (file == NULL) {
        return NULL;
    }
    trl_buffer text;
    trl_buffer_init(&text);
    char chunk[TRL_CHUNK_BYTES];
    bool more = true;
    while (more && text.length <= TRL_MOST_FILE_BYTES) {
        size_t got = fread(chunk, 1U, sizeof chunk, file);
        trl_put_raw(&text, (const uint8_t *)chunk, got);
        more = got == sizeof chunk;
    }
    *why = ferror(file) != 0 ? errno : 0;
    (void)fclose(file);
    trl_put_u8(&text, 0U);
    if (*why != 0 || text.state != TRL_BUFFER_OK) {
        trl_buffer_free(&text);
        return NULL;
    }
    /* The buffer's octets are the caller's now. */
    return (char *)text.data;
}

bool trl_service_read(const char *path, const char *name, trl_options *out, trl_failure *failure) {
    int why = 0;
    char *text = read_whole(path, &why);
    if (text == NULL && why == 0) {
        trl_failure_no_memory(failure);
        return false;
    }
    if (text == NULL) {
        trl_io_error said;
        trl_io_error_os(&said, why);
        char message[TRL_FAILURE_MESSAGE_BYTES];
        trl_format(message, sizeof message, "cannot read %s: %s", path, said.text);
        trl_settings_unable(failure, message);
        return false;
    }
    bool read = trl_service_parse(text, name, path, out, failure);
    free(text);
    return read;
}

/* A field of a password file's line, unescaped: its text, and whether it
   was written plain, so a `*` in it is a match of anything. */
typedef struct field {
    trl_buffer text;
    bool plain;
} field;

/* Splits a password file's line, of `length`, into its fields: the first
   three at each colon not escaped, the fourth the rest; how many. */
static size_t fields_of(const char *line, size_t length, field *out) {
    size_t count = 0U;
    bool escaped = false;
    out[0].plain = true;
    for (size_t i = 0U; i < length; ++i) {
        char c = line[i];
        if (escaped) {
            trl_put_u8(&out[count].text, (uint8_t)c);
            escaped = false;
            out[count].plain = false;
        } else if (c == TRL_ESCAPE) {
            escaped = true;
        } else if (c == TRL_SEPARATOR && count < TRL_FIELDS - 1U) {
            ++count;
            out[count].plain = true;
        } else {
            trl_put_u8(&out[count].text, (uint8_t)c);
        }
    }
    return count + 1U;
}

/* Whether a field matches `wanted`: a `*` written plain matches anything. */
static bool matches(const field *f, const char *wanted) {
    size_t length = strlen(wanted);
    if (f->plain && f->text.length == 1U && f->text.data[0] == '*') {
        return true;
    }
    return f->text.length == length && (length == 0U || memcmp(f->text.data, wanted, length) == 0);
}

/* Whether the line, of `length`, gives the password of `host`, `port`
   and `user`, appended to `out`. */
static bool line_gives(const char *line, size_t length, const char *const wanted[TRL_MATCHED], trl_buffer *out) {
    field fields[TRL_FIELDS];
    for (size_t i = 0U; i < TRL_FIELDS; ++i) {
        trl_buffer_init(&fields[i].text);
        fields[i].plain = true;
    }
    bool found = fields_of(line, length, fields) == TRL_FIELDS;
    for (size_t i = 0U; found && i < TRL_MATCHED; ++i) {
        found = matches(&fields[i], wanted[i]);
    }
    if (found) {
        trl_put_raw(out, fields[TRL_MATCHED].text.data, fields[TRL_MATCHED].text.length);
    }
    for (size_t i = 0U; i < TRL_FIELDS; ++i) {
        trl_buffer_free(&fields[i].text);
    }
    return found;
}

bool trl_password_find(const char *text, const char *host, const char *port, const char *user, trl_buffer *out) {
    const char *const wanted[TRL_MATCHED] = {host, port, user};
    for (const char *line = text; *line != '\0';) {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);
        if (length > 0U && line[length - 1U] == '\r') {
            --length;
        }
        const char *start = line;
        while (start < line + length && white(*start)) {
            ++start;
        }
        if ((start == line + length || *start != TRL_COMMENT) && line_gives(line, length, wanted, out)) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        line = end + 1;
    }
    return false;
}

char *trl_read_regular(const char *path) {
    /* Each platform declares a file's status in headers of its own, which
       the include checker does not know (R-LINT-3). */
    // NOLINTBEGIN(misc-include-cleaner)
#ifdef _WIN32
    struct _stat status;
    bool regular = _stat(path, &status) == 0 && (status.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat status;
    bool regular = stat(path, &status) == 0 && (status.st_mode & S_IFMT) == S_IFREG;
#endif
    // NOLINTEND(misc-include-cleaner)
    if (!regular) {
        return NULL;
    }
    int why = 0;
    return read_whole(path, &why);
}

char *trl_at_home(const char *name) {
#ifdef _WIN32
    const char *home = getenv("USERPROFILE"); // NOLINT(concurrency-mt-unsafe)
#else
    const char *home = getenv("HOME"); // NOLINT(concurrency-mt-unsafe)
#endif
    if (home == NULL || home[0] == '\0') {
        return NULL;
    }
    size_t length = strlen(home) + 1U + strlen(name) + 1U;
    char *path = trl_malloc(length);
    if (path != NULL) {
        trl_format(path, length, "%s/%s", home, name);
    }
    return path;
}
