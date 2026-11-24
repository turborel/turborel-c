/**
 * @file kept.c
 * @brief The debugger's events a connection keeps, the oldest first, as
 * turborel-client keeps them, until trl_debug_wait() gives them
 * [PROTOCOL Event 'W'].
 */
#include "conn/conn.h" // NOLINT(misc-include-cleaner): the connection's fields, which the checker does not count
#include "debug/debug.h"
#include "turborel/turborel.h"

#include <stddef.h>

void trl_conn_event_keep(trl_conn *conn, trl_debug_event *event) {
    event->next = NULL;
    if (conn->events_last != NULL) {
        conn->events_last->next = event;
    } else {
        conn->events_first = event;
    }
    conn->events_last = event;
}

trl_debug_event *trl_conn_event_take(trl_conn *conn) {
    trl_debug_event *first = conn->events_first;
    if (first != NULL) {
        conn->events_first = first->next;
        if (conn->events_first == NULL) {
            conn->events_last = NULL;
        }
        first->next = NULL;
    }
    return first;
}

void trl_conn_events_drop(trl_conn *conn) {
    for (trl_debug_event *event = trl_conn_event_take(conn); event != NULL; event = trl_conn_event_take(conn)) {
        trl_debug_event_free(event);
    }
}
