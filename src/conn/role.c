/**
 * @file role.c
 * @brief Whether a server takes changes [CLIENT Several servers]: a
 * server alone does, and of a replicated group its primary; a replica
 * following the group's log takes none. A server that grants `status`
 * tells every session, as SERVER_READ_ONLY, so its role is read with no
 * query, whoever the session's user is. Of one that does not, it is read
 * from INFORMATION_SCHEMA.REPLICAS, which shows a replica's role to the
 * database's owner alone; a server alone has no such table.
 */
#include "turborel/turborel.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* The query of a server's own role in its group, the status of whether it
   takes no changes, its word for one that takes none, and the role
   REPLICAS shows of the group's primary. */
#define TRL_ROLE_QUERY "SELECT ROLE FROM INFORMATION_SCHEMA.REPLICAS WHERE IS_SELF = 'YES'"
#define TRL_STATUS_READ_ONLY "SERVER_READ_ONLY"
#define TRL_YES "YES"
#define TRL_PRIMARY "PRIMARY"
/* The octets of an SQLSTATE's class. */
#define TRL_CLASS_BYTES 2U

/* Whether a failure is of a table that does not exist, as a server alone
   answers the query: of class 42, or 2A, as an older server answered. */
static bool no_such_table(const trl_failure *failure) {
    const char *state = trl_failure_sqlstate(failure);
    return strncmp(state, "2A", TRL_CLASS_BYTES) == 0 || strncmp(state, "42", TRL_CLASS_BYTES) == 0;
}

/* The role a row of the query shows: PRIMARY takes changes, its spaces
   at the end aside; any other role none. */
static trl_server_role role_shown(const trl_value *value) {
    size_t length = 0U;
    const char *text = trl_value_text(value, &length);
    while (text != NULL && length > 0U && text[length - 1U] == ' ') {
        --length;
    }
    bool primary = text != NULL && length == strlen(TRL_PRIMARY) && memcmp(text, TRL_PRIMARY, length) == 0;
    return primary ? TRL_SERVER_READ_WRITE : TRL_SERVER_READ_ONLY;
}

trl_status trl_conn_role(trl_conn *conn, trl_server_role *out) {
    if (conn == NULL || out == NULL) {
        return TRL_INVALID_ARGUMENT;
    }
    const char *told = trl_conn_setting(conn, TRL_STATUS_READ_ONLY);
    if (told != NULL) {
        *out = strcmp(told, TRL_YES) == 0 ? TRL_SERVER_READ_ONLY : TRL_SERVER_READ_WRITE;
        return TRL_OK;
    }
    /* Asked as a statement that is its own transaction, so it leaves none
       open. */
    bool autocommit = trl_conn_autocommit(conn);
    (void)trl_conn_set_autocommit(conn, true);
    trl_result *result = NULL;
    trl_status asked = trl_query(conn, TRL_ROLE_QUERY, &result);
    (void)trl_conn_set_autocommit(conn, autocommit);
    if (asked != TRL_OK) {
        if (!trl_conn_is_lost(conn) && no_such_table(trl_conn_failure(conn))) {
            *out = TRL_SERVER_READ_WRITE;
            return TRL_OK;
        }
        return asked;
    }
    *out = trl_result_row_count(result) == 0U ? TRL_SERVER_UNKNOWN : role_shown(trl_result_value(result, 0U, 0U));
    trl_result_free(result);
    return TRL_OK;
}
