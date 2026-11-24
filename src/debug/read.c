/**
 * @file read.c
 * @brief What the routine debugger's events, pauses, frames and
 * breakpoints hold, read [CLIENT Debugging routines]: each borrowed from
 * what holds it, null or 0 of a null handle or a place past the last.
 */
#include "debug/debug.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A breakpoint of `list`, or null past the last. */
static const trl_debug_breakpoint *breakpoint_at(const trl_debug_breakpoint_list *list, size_t index) {
    return list == NULL || index >= list->count ? NULL : &list->items[index];
}

size_t trl_debug_breakpoint_count(const trl_debug_breakpoint_list *list) {
    return list == NULL ? 0U : list->count;
}

uint32_t trl_debug_breakpoint_number(const trl_debug_breakpoint_list *list, size_t index) {
    const trl_debug_breakpoint *b = breakpoint_at(list, index);
    return b == NULL ? 0U : b->number;
}

const char *trl_debug_breakpoint_schema(const trl_debug_breakpoint_list *list, size_t index) {
    const trl_debug_breakpoint *b = breakpoint_at(list, index);
    return b == NULL ? NULL : b->schema;
}

const char *trl_debug_breakpoint_name(const trl_debug_breakpoint_list *list, size_t index) {
    const trl_debug_breakpoint *b = breakpoint_at(list, index);
    return b == NULL ? NULL : b->name;
}

uint32_t trl_debug_breakpoint_line(const trl_debug_breakpoint_list *list, size_t index) {
    const trl_debug_breakpoint *b = breakpoint_at(list, index);
    return b == NULL ? 0U : b->line;
}

const char *trl_debug_breakpoint_condition(const trl_debug_breakpoint_list *list, size_t index) {
    const trl_debug_breakpoint *b = breakpoint_at(list, index);
    return b == NULL ? NULL : b->condition;
}

trl_debug_said trl_debug_event_said(const trl_debug_event *event) {
    return event == NULL ? TRL_DEBUG_ENDED : event->said;
}

const trl_debug_pause *trl_debug_event_pause(const trl_debug_event *event) {
    return event == NULL ? NULL : event->pause;
}

trl_debug_reason trl_debug_pause_reason(const trl_debug_pause *pause) {
    return pause == NULL ? TRL_DEBUG_AT_STEP : pause->reason;
}

uint32_t trl_debug_pause_breakpoint(const trl_debug_pause *pause) {
    return pause == NULL ? 0U : pause->breakpoint;
}

size_t trl_debug_pause_frame_count(const trl_debug_pause *pause) {
    return pause == NULL ? 0U : pause->frame_count;
}

const trl_debug_frame *trl_debug_pause_frame(const trl_debug_pause *pause, size_t index) {
    return pause == NULL || index >= pause->frame_count ? NULL : &pause->frames[index];
}

bool trl_debug_frame_has_origin(const trl_debug_frame *frame) {
    return frame != NULL && frame->has_origin;
}

trl_debug_kind trl_debug_frame_kind(const trl_debug_frame *frame) {
    return trl_debug_frame_has_origin(frame) ? frame->kind : TRL_DEBUG_PROCEDURE;
}

const char *trl_debug_frame_specific_schema(const trl_debug_frame *frame) {
    return trl_debug_frame_has_origin(frame) ? frame->specific_schema : NULL;
}

const char *trl_debug_frame_specific_name(const trl_debug_frame *frame) {
    return trl_debug_frame_has_origin(frame) ? frame->specific_name : NULL;
}

const char *trl_debug_frame_schema(const trl_debug_frame *frame) {
    return trl_debug_frame_has_origin(frame) ? frame->schema : NULL;
}

const char *trl_debug_frame_name(const trl_debug_frame *frame) {
    return trl_debug_frame_has_origin(frame) ? frame->name : NULL;
}

bool trl_debug_frame_visible(const trl_debug_frame *frame) {
    return frame != NULL && frame->visible;
}

bool trl_debug_frame_place(const trl_debug_frame *frame, uint64_t *out) {
    if (frame == NULL || !frame->has_place) {
        return false;
    }
    if (out != NULL) {
        *out = frame->place;
    }
    return true;
}

bool trl_debug_frame_line(const trl_debug_frame *frame, uint32_t *line, uint32_t *column) {
    if (frame == NULL || !frame->has_line) {
        return false;
    }
    if (line != NULL) {
        *line = frame->line;
    }
    if (column != NULL) {
        *column = frame->column;
    }
    return true;
}

/* A parameter or a variable of `frame`, or null past the last. */
static const trl_debug_named *named_at(const trl_debug_frame *frame, trl_debug_values which, size_t index) {
    if (frame == NULL || (which != TRL_DEBUG_PARAMETERS && which != TRL_DEBUG_VARIABLES) ||
        index >= frame->named_count[which]) {
        return NULL;
    }
    return &frame->named[which][index];
}

size_t trl_debug_frame_value_count(const trl_debug_frame *frame, trl_debug_values which) {
    if (frame == NULL || (which != TRL_DEBUG_PARAMETERS && which != TRL_DEBUG_VARIABLES)) {
        return 0U;
    }
    return frame->named_count[which];
}

const char *trl_debug_frame_value_name(const trl_debug_frame *frame, trl_debug_values which, size_t index) {
    const trl_debug_named *n = named_at(frame, which, index);
    return n == NULL ? NULL : n->name;
}

const char *trl_debug_frame_value_type(const trl_debug_frame *frame, trl_debug_values which, size_t index) {
    const trl_debug_named *n = named_at(frame, which, index);
    return n == NULL ? NULL : n->type;
}

const trl_value *trl_debug_frame_value(const trl_debug_frame *frame, trl_debug_values which, size_t index) {
    const trl_debug_named *n = named_at(frame, which, index);
    return n == NULL ? NULL : &n->value;
}

size_t trl_debug_frame_cursor_count(const trl_debug_frame *frame) {
    return frame == NULL ? 0U : frame->cursor_count;
}

const char *trl_debug_frame_cursor_name(const trl_debug_frame *frame, size_t index) {
    return frame == NULL || index >= frame->cursor_count ? NULL : frame->cursors[index].name;
}

bool trl_debug_frame_cursor_open(const trl_debug_frame *frame, size_t index) {
    return frame != NULL && index < frame->cursor_count && frame->cursors[index].open;
}

const char *trl_debug_frame_cursor_query(const trl_debug_frame *frame, size_t index) {
    return frame == NULL || index >= frame->cursor_count ? NULL : frame->cursors[index].query;
}
