/**
 * @file made.c
 * @brief What the server sent of the routine debugger, copied out of a
 * reply's arena into structures of their own, so a pause, an event or a
 * list of breakpoints outlives the reply it came in, each string
 * null-ended, each value copied whole [CLIENT Debugging routines].
 */
#include "codec/arena.h"
#include "codec/buffer.h"
#include "codec/debug.h"
#include "data/params.h"
#include "debug/debug.h"
#include "memory.h"
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A copy of `text` in `arena`, null-ended; null if memory cannot be had. */
static const char *text_of(trl_arena *arena, trl_slice text) {
    char *copy = trl_arena_alloc(arena, text.length + 1U, 1U);
    if (copy != NULL) {
        if (text.length > 0U) {
            memcpy(copy, text.bytes, text.length);
        }
        copy[text.length] = '\0';
    }
    return copy;
}

/* Copies `count` named values into `arena`; whether it could. */
static bool named_made(trl_arena *arena, const trl_wire_named *from, uint32_t count, trl_debug_named **out) {
    *out = trl_arena_alloc(arena, (size_t)count + 1U, sizeof **out);
    for (uint32_t i = 0U; *out != NULL && i < count; ++i) {
        trl_debug_named *to = &(*out)[i];
        to->name = text_of(arena, from[i].name);
        to->type = text_of(arena, from[i].type);
        if (to->name == NULL || to->type == NULL || !trl_value_copy(arena, &from[i].value, &to->value, 0U)) {
            return false;
        }
    }
    return *out != NULL;
}

/* Copies a frame's cursors into `arena`; whether it could. */
static bool cursors_made(trl_arena *arena, const trl_wire_frame *from, trl_debug_frame *to) {
    to->cursors = trl_arena_alloc(arena, (size_t)from->cursor_count + 1U, sizeof *to->cursors);
    to->cursor_count = from->cursor_count;
    for (uint32_t i = 0U; to->cursors != NULL && i < from->cursor_count; ++i) {
        to->cursors[i].name = text_of(arena, from->cursors[i].name);
        to->cursors[i].open = from->cursors[i].open;
        to->cursors[i].query = text_of(arena, from->cursors[i].query);
        if (to->cursors[i].name == NULL || to->cursors[i].query == NULL) {
            return false;
        }
    }
    return to->cursors != NULL;
}

/* Copies a frame's origin, if it has one; whether it could. */
static bool origin_made(trl_arena *arena, const trl_wire_frame *from, trl_debug_frame *to) {
    to->has_origin = from->has_origin;
    if (!from->has_origin) {
        return true;
    }
    to->kind = (trl_debug_kind)from->kind;
    to->specific_schema = text_of(arena, from->specific_schema);
    to->specific_name = text_of(arena, from->specific_name);
    to->schema = text_of(arena, from->schema);
    to->name = text_of(arena, from->name);
    return to->specific_schema != NULL && to->specific_name != NULL && to->schema != NULL && to->name != NULL;
}

/* Copies a frame into `arena`; whether it could. */
static bool frame_made(trl_arena *arena, const trl_wire_frame *from, trl_debug_frame *to) {
    to->visible = from->visible;
    to->has_place = from->has_place;
    to->place = from->place;
    to->has_line = from->has_line;
    to->line = from->line;
    to->column = from->column;
    to->named_count[TRL_DEBUG_PARAMETERS] = from->parameter_count;
    to->named_count[TRL_DEBUG_VARIABLES] = from->variable_count;
    return origin_made(arena, from, to) &&
           named_made(arena, from->parameters, from->parameter_count, &to->named[TRL_DEBUG_PARAMETERS]) &&
           named_made(arena, from->variables, from->variable_count, &to->named[TRL_DEBUG_VARIABLES]) &&
           cursors_made(arena, from, to);
}

trl_debug_pause *trl_debug_pause_made(const trl_wire_pause *wire) {
    trl_debug_pause *pause = trl_calloc(1U, sizeof *pause);
    if (pause == NULL) {
        return NULL;
    }
    trl_arena_init(&pause->arena);
    pause->reason = (trl_debug_reason)wire->reason;
    pause->breakpoint = wire->reason == TRL_DEBUG_AT_BREAKPOINT ? wire->breakpoint : 0U;
    pause->frame_count = wire->frame_count;
    pause->frames = trl_arena_alloc(&pause->arena, (size_t)wire->frame_count + 1U, sizeof *pause->frames);
    bool made = pause->frames != NULL;
    for (uint32_t i = 0U; made && i < wire->frame_count; ++i) {
        made = frame_made(&pause->arena, &wire->frames[i], &pause->frames[i]);
    }
    if (!made) {
        trl_debug_pause_free(pause);
        return NULL;
    }
    return pause;
}

trl_debug_event *trl_debug_event_made(const trl_event *wire) {
    trl_debug_event *event = trl_calloc(1U, sizeof *event);
    if (event == NULL) {
        return NULL;
    }
    event->said = (trl_debug_said)wire->what;
    if (wire->what == TRL_EVENT_PAUSED) {
        event->pause = trl_debug_pause_made(&wire->pause);
        if (event->pause == NULL) {
            free(event);
            return NULL;
        }
    }
    return event;
}

trl_debug_breakpoint_list *trl_debug_list_made(const trl_wire_breakpoint *items, uint32_t count) {
    trl_debug_breakpoint_list *list = trl_calloc(1U, sizeof *list);
    if (list == NULL) {
        return NULL;
    }
    trl_arena_init(&list->arena);
    list->count = count;
    list->items = trl_arena_alloc(&list->arena, (size_t)count + 1U, sizeof *list->items);
    bool made = list->items != NULL;
    for (uint32_t i = 0U; made && i < count; ++i) {
        trl_debug_breakpoint *to = &list->items[i];
        to->number = items[i].number;
        to->line = items[i].line;
        to->schema = text_of(&list->arena, items[i].schema);
        to->name = text_of(&list->arena, items[i].name);
        to->condition = items[i].conditioned ? text_of(&list->arena, items[i].condition) : NULL;
        made = to->schema != NULL && to->name != NULL && (!items[i].conditioned || to->condition != NULL);
    }
    if (!made) {
        trl_debug_breakpoint_list_free(list);
        return NULL;
    }
    return list;
}

void trl_debug_pause_free(trl_debug_pause *pause) {
    if (pause != NULL) {
        trl_arena_free(&pause->arena);
        free(pause);
    }
}

void trl_debug_event_free(trl_debug_event *event) {
    if (event != NULL) {
        trl_debug_pause_free(event->pause);
        free(event);
    }
}

void trl_debug_breakpoint_list_free(trl_debug_breakpoint_list *list) {
    if (list != NULL) {
        trl_arena_free(&list->arena);
        free(list);
    }
}
