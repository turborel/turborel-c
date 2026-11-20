/**
 * @file delimited.c
 * @brief Delimited text as a source [CLIENT Importing]: CSV as RFC 4180
 * §2 defines it, and tab-separated text, a record a line, fields split at
 * a delimiter, read a line at a time, a record at most `max_record` bytes,
 * so input of any size is read in bounded memory, and a quote left open
 * is refused at the bound rather than read to the input's end.
 *
 * CSV: a record ends at a line break, CR LF or LF alone, the last one's
 * optional (rules 1, 2); a header may come first (rule 3); a field may be
 * enclosed in double quotes, and must be to hold the delimiter, a quote or
 * a line break, a quote in it written twice (rules 5 to 7). A quote in a
 * field not enclosed, or anything but the delimiter after a closing quote,
 * is refused. Tab-separated text has no quoting. A field written as the
 * dialect's text for null, not in quotes, is null; text is UTF-8, a byte
 * order mark beginning the input passed over.
 */
#include "codec/buffer.h"
#include "codec/utf8.h"
#include "format.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* RFC 4180 §2: what encloses a field, and what ends a line. */
#define TRL_QUOTE '"'
#define TRL_LF '\n'
#define TRL_CR '\r'
/* The UTF-8 byte order mark. */
#define TRL_BOM "\xef\xbb\xbf"
#define TRL_BOM_BYTES 3U
/* The bytes read from the input at a time. */
#define TRL_INPUT_BYTES 65536U
/* Room for a message of what is wrong. */
#define TRL_MESSAGE_BYTES 256U
/* The first byte past ASCII. */
#define TRL_PAST_ASCII 0x80U
/* The fields a record has room for at first, and how its room grows. */
#define TRL_FIRST_FIELDS 8U
#define TRL_GROWTH 2U

/* Where a scan of a CSV record is: so whether a line break ends it. */
typedef enum scan {
    SCAN_FIELD_START = 0,
    SCAN_PLAIN,
    SCAN_QUOTED,
    SCAN_QUOTE_IN_QUOTED,
} scan;

/* A field as written: where its text is in the fields' bytes, its length,
   and whether it was enclosed in quotes. */
typedef struct field {
    size_t at;
    size_t length;
    bool quoted;
} field;

struct trl_delimited {
    trl_text_read read;
    void *context;
    trl_dialect dialect;
    char *null;
    uint8_t *input;
    size_t input_at;
    size_t input_end;
    bool input_done;
    trl_buffer bytes;
    trl_buffer texts;
    field *fields;
    size_t field_count;
    size_t field_capacity;
    const char **names;
    uint64_t lines;
    uint64_t records;
    bool header_pending;
    bool started;
    bool done;
    char message[TRL_MESSAGE_BYTES];
};

trl_dialect trl_dialect_csv(void) {
    trl_dialect dialect = {',', true, false, "", TRL_DELIMITED_MAX_RECORD};
    return dialect;
}

trl_dialect trl_dialect_tsv(void) {
    trl_dialect dialect = {'\t', false, false, "", TRL_DELIMITED_MAX_RECORD};
    return dialect;
}

/* Whether `delimiter` is one ASCII character, not a quote or a line break. */
static bool delimiter_allowed(char delimiter) {
    unsigned char c = (unsigned char)delimiter;
    return c != 0U && c < TRL_PAST_ASCII && delimiter != TRL_QUOTE && delimiter != TRL_CR && delimiter != TRL_LF;
}

trl_status trl_delimited_new(trl_text_read read, void *context, const trl_dialect *dialect, trl_delimited **out) {
    if (out != NULL) {
        *out = NULL;
    }
    if (read == NULL || dialect == NULL || out == NULL || !delimiter_allowed(dialect->delimiter)) {
        return TRL_INVALID_ARGUMENT;
    }
    trl_delimited *d = trl_calloc(1U, sizeof *d);
    const char *null = dialect->null == NULL ? "" : dialect->null;
    size_t null_length = strlen(null);
    if (d != NULL) {
        d->null = trl_malloc(null_length + 1U);
        d->input = trl_malloc(TRL_INPUT_BYTES);
    }
    if (d == NULL || d->null == NULL || d->input == NULL) {
        trl_delimited_free(d);
        return TRL_NO_MEMORY;
    }
    memcpy(d->null, null, null_length + 1U);
    d->read = read;
    d->context = context;
    d->dialect = *dialect;
    d->dialect.null = d->null;
    d->header_pending = dialect->header;
    trl_buffer_init(&d->bytes);
    trl_buffer_init(&d->texts);
    *out = d;
    return TRL_OK;
}

void trl_delimited_free(trl_delimited *delimited) {
    if (delimited == NULL) {
        return;
    }
    free(delimited->null);
    free(delimited->input);
    free(delimited->fields);
    free((void *)delimited->names);
    trl_buffer_free(&delimited->bytes);
    trl_buffer_free(&delimited->texts);
    free(delimited);
}

/* Fails: says what is wrong. */
static bool wrong(trl_delimited *d, const char *message) {
    trl_format(d->message, sizeof d->message, "%s", message);
    return false;
}

/* Fails: says what is wrong of field `n`, from 1, as `why` with "%lu". */
static bool wrong_field(trl_delimited *d, const char *before, unsigned long n, const char *after) {
    trl_format(d->message, sizeof d->message, "%sfield %lu%s", before, n, after);
    return false;
}

/* Fills the input from the reader; false if it cannot be read. */
static bool fill(trl_delimited *d) {
    size_t got = 0U;
    if (!d->read(d->context, d->input, TRL_INPUT_BYTES, &got) || got > TRL_INPUT_BYTES) {
        return wrong(d, "[DESIGN] cannot read the input");
    }
    d->input_at = 0U;
    d->input_end = got;
    d->input_done = got == 0U;
    return true;
}

/* Appends to the record's bytes up to and including a line feed, at most
   `room` bytes; `read` gets how many were appended. */
static bool read_line(trl_delimited *d, size_t room, size_t *read) {
    *read = 0U;
    while (*read < room) {
        if (d->input_at == d->input_end && (d->input_done || !fill(d) || d->input_done)) {
            return d->message[0] == '\0' || d->input_done;
        }
        size_t left = d->input_end - d->input_at;
        size_t most = room - *read < left ? room - *read : left;
        const uint8_t *from = d->input + d->input_at;
        const uint8_t *lf = memchr(from, TRL_LF, most);
        size_t taken = lf == NULL ? most : (size_t)(lf - from) + 1U;
        trl_put_raw(&d->bytes, from, taken);
        d->input_at += taken;
        *read += taken;
        if (lf != NULL) {
            return true;
        }
    }
    return true;
}

/* Where the scan is after `b`. */
static scan next_scan(scan was, uint8_t b, char delimiter) {
    bool quote = b == TRL_QUOTE;
    bool split = b == (uint8_t)delimiter;
    if (was == SCAN_QUOTED) {
        return quote ? SCAN_QUOTE_IN_QUOTED : SCAN_QUOTED;
    }
    if (was == SCAN_PLAIN) {
        return split ? SCAN_FIELD_START : SCAN_PLAIN;
    }
    if (split) {
        return SCAN_FIELD_START;
    }
    return quote ? SCAN_QUOTED : SCAN_PLAIN;
}

/* Takes the record's line break off its bytes, and a byte order mark off
   the first record's. */
static void ended(trl_delimited *d) {
    trl_buffer *b = &d->bytes;
    if (b->length > 0U && b->data[b->length - 1U] == TRL_LF) {
        --b->length;
        if (b->length > 0U && b->data[b->length - 1U] == TRL_CR) {
            --b->length;
        }
    }
    if (!d->started && b->length >= TRL_BOM_BYTES && memcmp(b->data, TRL_BOM, TRL_BOM_BYTES) == 0) {
        memmove(b->data, b->data + TRL_BOM_BYTES, b->length - TRL_BOM_BYTES);
        b->length -= TRL_BOM_BYTES;
    }
    d->started = true;
}

/* Reads a line more of the record, at most what the record has room for;
   its bytes in `n`, 0 at the input's end. */
static bool line_more(trl_delimited *d, size_t *n) {
    size_t start = d->bytes.length;
    size_t room = d->dialect.max_record - (start < d->dialect.max_record ? start : d->dialect.max_record) + 1U;
    if (!read_line(d, room, n)) {
        return false;
    }
    if (d->bytes.state != TRL_BUFFER_OK) {
        return wrong(d, "[DESIGN] memory could not be had for the record");
    }
    if (d->bytes.length > d->dialect.max_record) {
        trl_format(d->message, sizeof d->message, "[DESIGN] the record is longer than %lu bytes",
                   (unsigned long)d->dialect.max_record);
        return false;
    }
    return true;
}

/* Reads the next record's bytes, its line break taken off; `more` false at
   the input's end. */
static bool raw(trl_delimited *d, bool *more) {
    d->bytes.length = 0U;
    scan at = SCAN_FIELD_START;
    for (;;) {
        size_t start = d->bytes.length;
        size_t n = 0U;
        if (!line_more(d, &n)) {
            return false;
        }
        if (n == 0U) {
            if (at == SCAN_QUOTED) {
                return wrong(d, "[RFC 4180 \xc2\xa7"
                                "2 rule 5] a quoted field is not closed");
            }
            *more = d->bytes.length > 0U;
            return true;
        }
        for (size_t i = start; d->dialect.quoted && i < d->bytes.length; ++i) {
            at = next_scan(at, d->bytes.data[i], d->dialect.delimiter);
        }
        ++d->lines;
        if (at != SCAN_QUOTED) {
            ended(d);
            *more = true;
            return true;
        }
    }
}

/* Adds a field of the bytes from `from`, of `length`, the index `n`. */
static bool add_field(trl_delimited *d, const uint8_t *from, size_t length, bool quoted) {
    size_t n = d->field_count;
    if (!trl_utf8_valid(from, length)) {
        return wrong_field(d, "[DESIGN] ", (unsigned long)(n + 1U), " is not UTF-8");
    }
    if (d->field_count == d->field_capacity) {
        size_t capacity = d->field_capacity == 0U ? TRL_FIRST_FIELDS : d->field_capacity * TRL_GROWTH;
        field *grown = trl_realloc(d->fields, capacity * sizeof *grown);
        if (grown == NULL) {
            return wrong(d, "[DESIGN] memory could not be had for the record");
        }
        d->fields = grown;
        d->field_capacity = capacity;
    }
    d->fields[n].at = d->texts.length;
    d->fields[n].length = length;
    d->fields[n].quoted = quoted;
    trl_put_raw(&d->texts, from, length);
    trl_put_u8(&d->texts, 0U);
    ++d->field_count;
    return true;
}

/* Splits a record not quoted at its delimiter. */
static bool split_plain(trl_delimited *d) {
    const uint8_t *bytes = d->bytes.data;
    size_t length = d->bytes.length;
    size_t begun = 0U;
    for (size_t i = 0U; i <= length; ++i) {
        if (i == length || bytes[i] == (uint8_t)d->dialect.delimiter) {
            if (!add_field(d, bytes + begun, i - begun, false)) {
                return false;
            }
            begun = i + 1U;
        }
    }
    return true;
}

/* Reads a field enclosed in quotes, its opening quote read, to its
   closing quote, a quote written twice one; `at` past it. */
static bool enclosed(trl_delimited *d, size_t *at, trl_buffer *written) {
    const uint8_t *bytes = d->bytes.data;
    size_t length = d->bytes.length;
    for (;;) {
        if (*at == length) {
            return wrong_field(d, "", (unsigned long)(d->field_count + 1U),
                               ": [RFC 4180 \xc2\xa7"
                               "2 rule 5] a quoted field is not closed");
        }
        uint8_t b = bytes[(*at)++];
        if (b == TRL_QUOTE) {
            if (*at == length || bytes[*at] != TRL_QUOTE) {
                return true;
            }
            ++*at;
        }
        trl_put_u8(written, b);
    }
}

/* Reads a field not enclosed in quotes to the delimiter, `at` at it; a
   quote in it is refused. */
static bool unenclosed(trl_delimited *d, size_t *at, trl_buffer *written) {
    const uint8_t *bytes = d->bytes.data;
    while (*at < d->bytes.length && bytes[*at] != (uint8_t)d->dialect.delimiter) {
        if (bytes[*at] == TRL_QUOTE) {
            return wrong_field(d,
                               "[RFC 4180 \xc2\xa7"
                               "2 rule 5] ",
                               (unsigned long)(d->field_count + 1U), ": a quote in a field not enclosed in quotes");
        }
        trl_put_u8(written, bytes[(*at)++]);
    }
    return true;
}

/* Splits a CSV record into its fields. */
static bool split_quoted(trl_delimited *d) {
    const uint8_t *bytes = d->bytes.data;
    size_t length = d->bytes.length;
    size_t at = 0U;
    for (;;) {
        trl_buffer written;
        trl_buffer_init(&written);
        bool quoted = at < length && bytes[at] == TRL_QUOTE;
        at += quoted ? 1U : 0U;
        bool read = quoted ? enclosed(d, &at, &written) : unenclosed(d, &at, &written);
        read = read && add_field(d, written.length == 0U ? (const uint8_t *)"" : written.data, written.length, quoted);
        trl_buffer_free(&written);
        if (!read || at == length) {
            return read;
        }
        if (bytes[at] != (uint8_t)d->dialect.delimiter) {
            return wrong_field(d,
                               "[RFC 4180 \xc2\xa7"
                               "2 rule 5] ",
                               (unsigned long)d->field_count, ": a quoted field's closing quote is followed by more");
        }
        ++at;
    }
}

/* Reads the next record's fields; `more` false at the input's end. A
   failure fills `error`, at `record`. */
static bool fields(trl_delimited *d, uint64_t record, bool *more, trl_source_error *error) {
    uint64_t line = d->lines + 1U;
    d->field_count = 0U;
    d->texts.length = 0U;
    d->message[0] = '\0';
    bool read = raw(d, more) && (!*more || (d->dialect.quoted ? split_quoted(d) : split_plain(d)));
    if (read && d->texts.state != TRL_BUFFER_OK) {
        read = wrong(d, "[DESIGN] memory could not be had for the record");
    }
    if (!read) {
        d->done = true;
        error->position.record = record;
        error->position.line = line;
        error->message = d->message;
        return false;
    }
    d->done = !*more;
    return true;
}

/* Reads the header, if one is still to be read. */
static trl_source_read header(trl_delimited *d, trl_headings *out, trl_source_error *error) {
    out->names = NULL;
    out->count = 0U;
    if (!d->header_pending) {
        return TRL_READ_OK;
    }
    d->header_pending = false;
    bool more = false;
    if (!fields(d, 0U, &more, error)) {
        return TRL_READ_FAILED;
    }
    if (!more) {
        (void)wrong(d, "[DESIGN] there is no header");
        error->position.record = 0U;
        error->position.line = 1U;
        error->message = d->message;
        return TRL_READ_FAILED;
    }
    const char **names = (const char **)trl_calloc(d->field_count == 0U ? 1U : d->field_count, sizeof *names);
    char *texts = trl_malloc(d->texts.length == 0U ? 1U : d->texts.length);
    if (names == NULL || texts == NULL) {
        free((void *)names);
        free(texts);
        (void)wrong(d, "[DESIGN] memory could not be had for the header");
        error->position.record = 0U;
        error->position.line = 1U;
        error->message = d->message;
        return TRL_READ_FAILED;
    }
    memcpy(texts, d->texts.data, d->texts.length);
    for (size_t i = 0U; i < d->field_count; ++i) {
        names[i] = texts + d->fields[i].at;
    }
    free((void *)d->names);
    d->names = names;
    out->names = names;
    out->count = d->field_count;
    return TRL_READ_OK;
}

/* The source's headings. */
static trl_source_read source_headings(void *context, trl_headings *out, trl_source_error *error) {
    return header(context, out, error);
}

/* The source's next record. */
static trl_source_read source_record(void *context, trl_params *values, trl_position *at, trl_source_error *error) {
    trl_delimited *d = context;
    trl_headings ignored;
    if (header(d, &ignored, error) == TRL_READ_FAILED) {
        return TRL_READ_FAILED;
    }
    if (d->done) {
        return TRL_READ_END;
    }
    uint64_t number = d->records + 1U;
    uint64_t line = d->lines + 1U;
    bool more = false;
    if (!fields(d, number, &more, error)) {
        return TRL_READ_FAILED;
    }
    if (!more) {
        return TRL_READ_END;
    }
    d->records = number;
    at->record = number;
    at->line = line;
    for (size_t i = 0U; i < d->field_count; ++i) {
        const char *text = (const char *)d->texts.data + d->fields[i].at;
        bool null = !d->fields[i].quoted && strcmp(text, d->null) == 0;
        trl_status added = null ? trl_params_add_null(values) : trl_params_add_text(values, text, d->fields[i].length);
        if (added != TRL_OK) {
            (void)wrong(d, "[DESIGN] memory could not be had for the record");
            error->position = *at;
            error->message = d->message;
            return TRL_READ_FAILED;
        }
    }
    return TRL_READ_OK;
}

trl_source trl_delimited_source(trl_delimited *delimited) {
    trl_source source = {delimited, source_headings, source_record};
    return source;
}
