/**
 * @file render.c
 * @brief A reply written as text, each field it holds, for the sample
 * frames' tests to compare with what a sample means [PROTOCOL Replies]:
 * a type as trl_type_to_text() writes it, a value as trl_value_to_text()
 * does, with the fields its text does not show beside it.
 */
#include "codec/buffer.h"
#include "codec/debug.h"
#include "codec/fields.h"
#include "codec/kinds.h"
#include "codec/replies.h"
#include "codec/tags.h"
#include "codec/types.h"
#include "codec/values.h"
#include "format.h"
#include "samples.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bytes a number's text takes at most; the first tag of diagnostics. */
#define NUMBER_BYTES 48U
#define FIRST_DIAGNOSTIC 7U

/* Text being written, cut short at its capacity. */
typedef struct text {
    char *out;
    size_t capacity;
    size_t length;
} text;

static void put(text *t, const char *s) {
    size_t n = strlen(s);
    size_t room = t->capacity - 1U - t->length;
    n = n < room ? n : room;
    memcpy(t->out + t->length, s, n);
    t->length += n;
    t->out[t->length] = '\0';
}

static void put_slice(text *t, trl_slice s) {
    char held[REPLY_TEXT_BYTES];
    size_t n = s.length < sizeof held - 1U ? s.length : sizeof held - 1U;
    if (n > 0U) {
        memcpy(held, s.bytes, n);
    }
    held[n] = '\0';
    put(t, held);
}

static void put_hex(text *t, trl_slice s) {
    char two[3];
    for (size_t i = 0U; i < s.length; ++i) {
        trl_format(two, sizeof two, "%02X", (unsigned int)s.bytes[i]);
        put(t, two);
    }
}

static void put_u64(text *t, uint64_t n) {
    char number[NUMBER_BYTES];
    trl_format(number, sizeof number, "%llu", (unsigned long long)n);
    put(t, number);
}

static void put_i64(text *t, int64_t n) {
    char number[NUMBER_BYTES];
    trl_format(number, sizeof number, "%lld", (long long)n);
    put(t, number);
}

static void put_i128(text *t, trl_int128 n) {
    char number[TRL_DECIMAL_TEXT_BYTES];
    trl_decimal whole = {n, 0U};
    (void)trl_decimal_text(&whole, number, sizeof number);
    put(t, number);
}

static void put_name(text *t, const trl_name *name) {
    put_slice(t, name->schema);
    put(t, ".");
    put_slice(t, name->name);
}

/* A structured type's name and proper supertypes. */
static void put_names(text *t, const trl_type_names *names) {
    put_name(t, &names->name);
    for (uint32_t i = 0U; i < names->supertype_count; ++i) {
        put(t, i == 0U ? " under " : ",");
        put_name(t, &names->supertypes[i]);
    }
}

/* The fields of a type its text does not show, by its tag. */
static void put_type_fields(text *t, const trl_data_type *type) {
    switch (type->tag) {
    case TRL_TAG_PLUGIN:
        put(t, " [");
        put_slice(t, type->as.plugin.name);
        put(t, " of ");
        put_u64(t, type->as.plugin.param_count);
        put(t, " parameters]");
        return;
    case TRL_TAG_CLOB:
    case TRL_TAG_BLOB:
        put(t, " [units ");
        put_u64(t, type->as.lob.units);
        put(t, "]");
        return;
    case TRL_TAG_STRUCTURED:
        put(t, " [");
        put_names(t, &type->as.structured);
        put(t, "]");
        return;
    case TRL_TAG_REFERENCE:
        put(t, " [");
        put_names(t, &type->as.reference.referenced);
        put(t, type->as.reference.scoped ? " scoped]" : "]");
        return;
    case TRL_TAG_XML:
        put(t, type->as.xml.has_element ? " [element]" : "");
        return;
    default:
        return;
    }
}

static void put_type(text *t, const trl_data_type *type) {
    char written[REPLY_TEXT_BYTES];
    (void)trl_type_to_text(type, written, sizeof written, NULL);
    put(t, written);
    put_type_fields(t, type);
}

/* A datetime's fields its text does not show. */
static void put_datetime(text *t, const trl_datetime *d) {
    put(t, d->zoned ? " zoned zone=" : " zone=");
    put_i64(t, d->zone);
    put(t, " precision=");
    put_u64(t, d->precision);
    put(t, " utc=");
    put_i64(t, d->utc);
}

static void put_interval(text *t, const trl_interval *i) {
    put(t, " qualifier=");
    put_u64(t, i->qualifier.start);
    put(t, "/");
    put_u64(t, i->qualifier.end);
    put(t, "/");
    put_u64(t, i->qualifier.leading);
    put(t, "/");
    put_u64(t, i->qualifier.fraction);
    put(t, " units=");
    put_i128(t, i->units);
}

static void put_locator(text *t, const trl_value *v) {
    put(t, " kind=");
    put_u64(t, v->as.locator.kind);
    put(t, " locator=");
    put_u64(t, v->as.locator.locator);
    put(t, " octets=");
    put_u64(t, v->as.locator.octets);
    put(t, " characters=");
    put_u64(t, v->as.locator.characters);
}

/* The fields of a value of a number, a plug-in's or a large object its text does not show. */
static void put_other_fields(text *t, const trl_value *v) {
    if (v->tag == TRL_TAG_APPROXIMATE) {
        char bits[NUMBER_BYTES];
        trl_format(bits, sizeof bits, " bits=%016llX", (unsigned long long)v->as.approximate);
        put(t, bits);
    } else if (v->tag == TRL_TAG_DECFLOAT) {
        put(t, " coefficient=");
        put_i128(t, v->as.decfloat.coefficient);
        put(t, " exponent=");
        put_i64(t, v->as.decfloat.exponent);
    } else if (v->tag == TRL_TAG_PLUGIN) {
        put(t, " type=");
        put_slice(t, v->as.plugin.type_name);
        put(t, " bytes=");
        put_hex(t, v->as.plugin.bytes);
    } else if (v->tag == TRL_TAG_LOB_WHOLE) {
        put(t, " kind=");
        put_u64(t, v->as.lob.kind);
    } else if (v->tag == TRL_TAG_LOB_LOCATOR) {
        put_locator(t, v);
    }
}

/* The fields of a value its text does not show, by its tag. */
static void put_value_fields(text *t, const trl_value *v) {
    switch (v->tag) {
    case TRL_TAG_DATE:
    case TRL_TAG_TIME:
    case TRL_TAG_TIMESTAMP:
        put_datetime(t, &v->as.datetime);
        return;
    case TRL_TAG_INTERVAL:
        put_interval(t, &v->as.interval);
        return;
    case TRL_TAG_JSON:
        put(t, " stored=");
        put_u64(t, v->as.bytes.length);
        return;
    default:
        put_other_fields(t, v);
        return;
    }
}

/* A value: its tag, its text, and what its text does not show. */
static void put_value(text *t, const trl_value *v) {
    char written[REPLY_TEXT_BYTES];
    put_u64(t, v->tag);
    put(t, ":");
    if (trl_value_to_text(v, written, sizeof written, NULL) == TRL_OK) {
        put(t, written);
    }
    put_value_fields(t, v);
}

static void put_rows(text *t, const trl_reply *r) {
    put(t, "Rows");
    for (uint32_t i = 0U; i < r->as.rows.count; ++i) {
        const trl_row *row = &r->as.rows.rows[i];
        put(t, i == 0U ? " (" : " | (");
        for (uint32_t c = 0U; c < row->count; ++c) {
            put(t, c == 0U ? "" : ", ");
            put_value(t, &row->values[c]);
        }
        put(t, ")");
    }
}

/* A column: its name, or ? for none, its type, not null, and its declared type. */
static void put_column(text *t, const trl_column *c) {
    if (c->named) {
        put_slice(t, c->name);
    } else {
        put(t, "?");
    }
    put(t, " ");
    put_type(t, &c->type);
    put(t, c->not_null ? " not null" : "");
    if (c->declared) {
        put(t, " declared ");
        put_slice(t, c->declared_name);
        put(t, "/");
        put_slice(t, c->declared_written);
    }
}

static void put_columns(text *t, const trl_column *columns, uint32_t count) {
    put(t, "(");
    for (uint32_t i = 0U; i < count; ++i) {
        put(t, i == 0U ? "" : "; ");
        put_column(t, &columns[i]);
    }
    put(t, ")");
}

/* Tail 20: the procedure, then each `?`'s mode, ordinal and type. */
static void put_called(text *t, const trl_reply *r) {
    static const char *const modes[] = {"", "IN", "INOUT", "", "OUT"};
    put(t, " called ");
    put_slice(t, r->as.prepared.procedure_schema);
    put(t, ".");
    put_slice(t, r->as.prepared.procedure_name);
    put(t, " (");
    for (uint32_t i = 0U; i < r->as.prepared.marker_count; ++i) {
        const trl_marker *m = &r->as.prepared.markers[i];
        put(t, i == 0U ? "" : "; ");
        put(t, m->mode < sizeof modes / sizeof modes[0] ? modes[m->mode] : "?");
        put(t, " ");
        put_u64(t, m->ordinal);
        put(t, m->parameter.varying ? " varying " : " ");
        put_type(t, &m->parameter.type);
    }
    put(t, ")");
}

static void put_prepared(text *t, const trl_reply *r) {
    put(t, "Prepared ");
    put_u64(t, r->as.prepared.statement);
    put(t, " parameters (");
    for (uint32_t i = 0U; i < r->as.prepared.parameter_count; ++i) {
        put(t, i == 0U ? "" : "; ");
        put(t, r->as.prepared.parameters[i].varying ? "varying " : "");
        put_type(t, &r->as.prepared.parameters[i].type);
    }
    put(t, ") columns ");
    put_columns(t, r->as.prepared.columns, r->as.prepared.column_count);
    if (r->as.prepared.called) {
        put_called(t, r);
    }
}

static void put_ready(text *t, const trl_reply *r) {
    put(t, "Ready ");
    put_slice(t, r->as.ready.authorization);
    put(t, " key=");
    put_hex(t, r->as.ready.key);
    put(t, " capabilities=");
    for (uint32_t i = 0U; i < r->as.ready.capabilities.count; ++i) {
        put(t, i == 0U ? "" : ",");
        put_slice(t, r->as.ready.capabilities.items[i]);
    }
    put(t, " server=");
    put_slice(t, r->as.ready.server);
}

/* Diagnostics, each told one as its tag and text. */
static void put_diagnostics(text *t, const trl_diagnostics *d) {
    for (unsigned int i = 0U; i < TRL_DIAGNOSTICS; ++i) {
        if (d->told[i]) {
            put(t, " ");
            put_u64(t, FIRST_DIAGNOSTIC + i);
            put(t, "=");
            put_slice(t, d->text[i]);
        }
    }
}

/* A failure: SQLCODE, SQLSTATE, message, its rule and place if told, active, diagnostics. */
static void put_failure(text *t, const trl_wire_failure *f) {
    put_i64(t, f->sqlcode);
    put(t, " ");
    put_slice(t, f->sqlstate);
    put(t, " '");
    put_slice(t, f->message);
    put(t, "'");
    if (f->cited) {
        put(t, " rule='");
        put_slice(t, f->rule);
        put(t, "'");
    }
    if (f->placed) {
        put(t, " at=");
        put_u64(t, f->position);
    }
    put(t, f->active ? " active" : " inactive");
    put_diagnostics(t, &f->diagnostics);
}

/* A Complete's tail, each tag told. */
static void put_complete_tail(text *t, const trl_completed *c) {
    if (c->has_position) {
        put(t, " position=");
        put_u64(t, c->position);
    }
    put(t, c->more ? " more" : "");
    if (c->has_rows) {
        put(t, " rows=");
        put_u64(t, c->rows);
    }
    for (uint32_t i = 0U; i < c->generated.count; ++i) {
        put(t, i == 0U ? " generated=" : ",");
        put_slice(t, c->generated.items[i]);
    }
    for (uint32_t i = 0U; i < c->condition_count; ++i) {
        put(t, " condition=[");
        put_failure(t, &c->conditions[i]);
        put(t, "]");
    }
    if (c->has_time) {
        put(t, " time=");
        put_u64(t, c->time[TRL_TIME_TOTAL]);
        put(t, "/");
        put_u64(t, c->time[TRL_TIME_QUEUED]);
        put(t, "/");
        put_u64(t, c->time[TRL_TIME_LOCKS]);
    }
}

static void put_complete(text *t, const trl_reply *r) {
    const trl_completed *c = &r->as.complete;
    put(t, "Complete ");
    put_i64(t, c->sqlcode);
    put(t, " ");
    put_slice(t, c->sqlstate);
    put(t, c->active ? " active" : " inactive");
    put_complete_tail(t, c);
}

static void put_lob(text *t, const trl_reply *r) {
    if (r->as.lob.what == TRL_LOB_LOCATOR) {
        put(t, "Lob locator ");
        put_u64(t, r->as.lob.locator);
        return;
    }
    put(t, "Lob data ");
    put_hex(t, r->as.lob.data);
}

static void put_notice(text *t, const trl_reply *r) {
    put(t, "Notice ");
    put_slice(t, r->as.notice.sqlstate);
    put(t, " '");
    put_slice(t, r->as.notice.message);
    put(t, "'");
    put_diagnostics(t, &r->as.notice.diagnostics);
}

/* A breakpoint: its number, routine, line and condition. */
static void put_breakpoint(text *t, const trl_wire_breakpoint *b) {
    put_u64(t, b->number);
    put(t, " ");
    put_slice(t, b->schema);
    put(t, ".");
    put_slice(t, b->name);
    put(t, " line ");
    put_u64(t, b->line);
    if (b->conditioned) {
        put(t, " if ");
        put_slice(t, b->condition);
    }
}

/* A frame's parameters or variables: each name, type and value. */
static void put_named(text *t, const char *what, const trl_wire_named *named, uint32_t count) {
    put(t, what);
    put(t, " (");
    for (uint32_t i = 0U; i < count; ++i) {
        put(t, i == 0U ? "" : "; ");
        put_slice(t, named[i].name);
        put(t, " ");
        put_slice(t, named[i].type);
        put(t, " ");
        put_value(t, &named[i].value);
    }
    put(t, ")");
}

/* A frame: its origin, visibility, place, line, and what it holds. */
static void put_frame(text *t, const trl_wire_frame *f) {
    static const char *const kinds[] = {"procedure", "function", "trigger"};
    if (f->has_origin) {
        put(t, f->kind < sizeof kinds / sizeof kinds[0] ? kinds[f->kind] : "?");
        put(t, " ");
        put_slice(t, f->specific_schema);
        put(t, ".");
        put_slice(t, f->specific_name);
        put(t, "/");
        put_slice(t, f->schema);
        put(t, ".");
        put_slice(t, f->name);
    } else {
        put(t, "direct");
    }
    put(t, f->visible ? " visible" : " hidden");
    if (f->has_place) {
        put(t, " place ");
        put_u64(t, f->place);
    }
    if (f->has_line) {
        put(t, " line ");
        put_u64(t, f->line);
        put(t, ":");
        put_u64(t, f->column);
    }
    put_named(t, " parameters", f->parameters, f->parameter_count);
    put_named(t, " variables", f->variables, f->variable_count);
    put(t, " cursors (");
    for (uint32_t i = 0U; i < f->cursor_count; ++i) {
        put(t, i == 0U ? "" : "; ");
        put_slice(t, f->cursors[i].name);
        put(t, f->cursors[i].open ? " open '" : " closed '");
        put_slice(t, f->cursors[i].query);
        put(t, "'");
    }
    put(t, ")");
}

/* A pause: its reason, then its frames, the outermost first. */
static void put_pause(text *t, const trl_wire_pause *p) {
    if (p->reason == 0U) {
        put(t, "breakpoint ");
        put_u64(t, p->breakpoint);
    } else {
        put(t, "step");
    }
    put(t, " frames (");
    for (uint32_t i = 0U; i < p->frame_count; ++i) {
        put(t, i == 0U ? "" : "; ");
        put_frame(t, &p->frames[i]);
    }
    put(t, ")");
}

static void put_debugged(text *t, const trl_reply *r) {
    const trl_debugged *d = &r->as.debugged;
    put(t, "Debugged ");
    if (d->what == TRL_DEBUGGED_CONNECTION) {
        put(t, "connection ");
        put_u64(t, d->connection);
    } else if (d->what == TRL_DEBUGGED_BREAKPOINT) {
        put(t, "breakpoint ");
        put_breakpoint(t, &d->breakpoint);
    } else if (d->what == TRL_DEBUGGED_BREAKPOINTS) {
        put(t, "breakpoints (");
        for (uint32_t i = 0U; i < d->breakpoint_count; ++i) {
            put(t, i == 0U ? "" : "; ");
            put_breakpoint(t, &d->breakpoints[i]);
        }
        put(t, ")");
    } else if (d->paused) {
        put(t, "stack ");
        put_pause(t, &d->pause);
    } else {
        put(t, "running");
    }
}

static void put_event(text *t, const trl_reply *r) {
    static const char *const said[] = {"paused ", "timed out", "cancelled", "ended"};
    put(t, "Event ");
    put(t, r->as.event.what < sizeof said / sizeof said[0] ? said[r->as.event.what] : "?");
    if (r->as.event.what == TRL_EVENT_PAUSED) {
        put_pause(t, &r->as.event.pause);
    }
}

/* The replies of a header alone, and those of a few fields. */
static void put_simple(text *t, const trl_reply *r) {
    switch (r->kind) {
    case TRL_KIND_CHALLENGE:
        put(t, "Challenge ");
        put_slice(t, r->as.challenge);
        return;
    case TRL_KIND_SUSPENDED:
        put(t, "Suspended");
        return;
    case TRL_KIND_PONG:
        put(t, "Pong");
        return;
    case TRL_KIND_STATUS:
        put(t, "Status ");
        put_slice(t, r->as.status.name);
        put(t, "=");
        put_slice(t, r->as.status.value);
        return;
    case TRL_KIND_ERROR:
        put(t, "Error ");
        put_failure(t, &r->as.error);
        return;
    default:
        put(t, "?");
        return;
    }
}

void reply_text(const trl_reply *reply, char *out, size_t capacity) {
    text t = {out, capacity, 0U};
    out[0] = '\0';
    switch (reply->kind) {
    case TRL_KIND_READY:
        put_ready(&t, reply);
        return;
    case TRL_KIND_PREPARED:
        put_prepared(&t, reply);
        return;
    case TRL_KIND_DESCRIPTION:
        put(&t, "Description ");
        put_columns(&t, reply->as.description.columns, reply->as.description.count);
        return;
    case TRL_KIND_ROWS:
        put_rows(&t, reply);
        return;
    case TRL_KIND_COMPLETE:
        put_complete(&t, reply);
        return;
    case TRL_KIND_LOB_REPLY:
        put_lob(&t, reply);
        return;
    case TRL_KIND_NOTICE:
        put_notice(&t, reply);
        return;
    case TRL_KIND_DEBUGGED:
        put_debugged(&t, reply);
        return;
    case TRL_KIND_EVENT:
        put_event(&t, reply);
        return;
    default:
        put_simple(&t, reply);
        return;
    }
}
