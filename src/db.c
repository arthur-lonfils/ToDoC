#include "db.h"
#include "migrate.h"
#include "util.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Module state ────────────────────────────────────────────── */

static sqlite3 *g_db = NULL;

/* ── Helpers ─────────────────────────────────────────────────── */

const char *db_last_error(void)
{
    if (g_db) {
        return sqlite3_errmsg(g_db);
    }
    return "database not open";
}

sqlite3 *db_get_handle(void)
{
    return g_db;
}

/* Read a nullable TEXT column as a strdup'd string (or NULL) */
static char *col_text_dup(sqlite3_stmt *stmt, int col)
{
    const unsigned char *val = sqlite3_column_text(stmt, col);
    if (val) {
        return todoc_strdup((const char *)val);
    }
    return NULL;
}

/* Populate a task_t from the current row of a SELECT * style statement.
 * Column order must match: id, title, description, type, priority, status,
 *                          scope, due_date, created_at, updated_at */
static void row_to_task(sqlite3_stmt *stmt, task_t *task)
{
    task->id = sqlite3_column_int64(stmt, 0);
    task->title = col_text_dup(stmt, 1);
    task->description = col_text_dup(stmt, 2);
    task->type = (task_type_t)sqlite3_column_int(stmt, 3);
    task->priority = (priority_t)sqlite3_column_int(stmt, 4);
    task->status = (status_t)sqlite3_column_int(stmt, 5);
    task->scope = col_text_dup(stmt, 6);
    task->due_date = col_text_dup(stmt, 7);
    task->created_at = col_text_dup(stmt, 8);
    task->updated_at = col_text_dup(stmt, 9);
}

/* ── Open / Close ────────────────────────────────────────────── */

todoc_err_t db_open(void)
{
    if (g_db) {
        return TODOC_OK;
    }

    char *dir = todoc_dir_path();
    if (!dir) {
        return TODOC_ERR_IO;
    }

    if (todoc_ensure_dir(dir) != 0) {
        free(dir);
        return TODOC_ERR_IO;
    }
    free(dir);

    char *path = todoc_db_path();
    if (!path) {
        return TODOC_ERR_IO;
    }

    int rc = sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    free(path);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "todoc: cannot open database: %s\n", sqlite3_errmsg(g_db));
        sqlite3_close(g_db);
        g_db = NULL;
        return TODOC_ERR_DB;
    }

    /* Enable WAL mode for better performance */
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    /* Enable foreign keys */
    sqlite3_exec(g_db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    return TODOC_OK;
}

void db_close(void)
{
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

/* ── Schema (delegates to migration system) ──────────────────── */

todoc_err_t db_init_schema(void)
{
    return migrate_run_all();
}

/* ── Insert ──────────────────────────────────────────────────── */

todoc_err_t db_task_insert(const task_t *task, int64_t *out_id)
{
    const char *sql =
        "INSERT INTO tasks (title, description, type, priority, status, scope, due_date) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, task->title, -1, SQLITE_STATIC);
    if (task->description) {
        sqlite3_bind_text(stmt, 2, task->description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_int(stmt, 3, (int)task->type);
    sqlite3_bind_int(stmt, 4, (int)task->priority);
    sqlite3_bind_int(stmt, 5, (int)task->status);
    if (task->scope) {
        sqlite3_bind_text(stmt, 6, task->scope, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 6);
    }
    if (task->due_date) {
        sqlite3_bind_text(stmt, 7, task->due_date, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return TODOC_ERR_DB;
    }

    if (out_id) {
        *out_id = sqlite3_last_insert_rowid(g_db);
    }
    return TODOC_OK;
}

/* ── Get ─────────────────────────────────────────────────────── */

todoc_err_t db_task_get(int64_t id, task_t *out_task)
{
    const char *sql = "SELECT id, title, description, type, priority, status, "
                      "       scope, due_date, created_at, updated_at "
                      "FROM tasks WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        memset(out_task, 0, sizeof(*out_task));
        row_to_task(stmt, out_task);
        sqlite3_finalize(stmt);
        return TODOC_OK;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_ERR_DB;
}

/* ── Update ──────────────────────────────────────────────────── */

todoc_err_t db_task_update(const task_t *task)
{
    const char *sql = "UPDATE tasks SET title=?, description=?, type=?, priority=?, "
                      "status=?, scope=?, due_date=? WHERE id=?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, task->title, -1, SQLITE_STATIC);
    if (task->description) {
        sqlite3_bind_text(stmt, 2, task->description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_int(stmt, 3, (int)task->type);
    sqlite3_bind_int(stmt, 4, (int)task->priority);
    sqlite3_bind_int(stmt, 5, (int)task->status);
    if (task->scope) {
        sqlite3_bind_text(stmt, 6, task->scope, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 6);
    }
    if (task->due_date) {
        sqlite3_bind_text(stmt, 7, task->due_date, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 7);
    }
    sqlite3_bind_int64(stmt, 8, task->id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return TODOC_ERR_DB;
    }

    if (sqlite3_changes(g_db) == 0) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_OK;
}

/* ── Delete ──────────────────────────────────────────────────── */

todoc_err_t db_task_delete(int64_t id)
{
    const char *sql = "DELETE FROM tasks WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return TODOC_ERR_DB;
    }

    if (sqlite3_changes(g_db) == 0) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_OK;
}

/* ── List ────────────────────────────────────────────────────── */

todoc_err_t db_task_list(const task_filter_t *filter, task_t **out_tasks, int *out_count)
{
    *out_tasks = NULL;
    *out_count = 0;

    /* Build query dynamically based on filters */
    char sql[512];
    int sql_len = snprintf(sql, sizeof(sql),
                           "SELECT id, title, description, type, priority, status, "
                           "       scope, due_date, created_at, updated_at "
                           "FROM tasks");

    /* Collect WHERE clauses */
    int has_where = 0;
    int param_idx = 0;

    /* We'll bind parameters after preparation, track what to bind */
    int bind_status = 0;
    int bind_priority = 0;
    int bind_type = 0;
    int bind_scope = 0;

    if (filter) {
        if (filter->status) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, " WHERE status = ?");
            has_where = 1;
            bind_status = ++param_idx;
        }
        if (filter->priority) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s priority = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_priority = ++param_idx;
        }
        if (filter->type) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s type = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_type = ++param_idx;
        }
        if (filter->scope) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s scope = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_scope = ++param_idx;
        }
    }

    /* Default ordering: by priority (critical first), then by created_at desc */
    sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len,
                        " ORDER BY priority ASC, created_at DESC");

    if (filter && filter->limit > 0) {
        sql_len +=
            snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, " LIMIT %d", filter->limit);
    }

    snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, ";");

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    /* Bind filter parameters */
    if (bind_status) {
        sqlite3_bind_int(stmt, bind_status, (int)*filter->status);
    }
    if (bind_priority) {
        sqlite3_bind_int(stmt, bind_priority, (int)*filter->priority);
    }
    if (bind_type) {
        sqlite3_bind_int(stmt, bind_type, (int)*filter->type);
    }
    if (bind_scope) {
        sqlite3_bind_text(stmt, bind_scope, filter->scope, -1, SQLITE_STATIC);
    }

    /* Collect results */
    int capacity = 16;
    task_t *tasks = todoc_calloc((size_t)capacity, sizeof(task_t));
    int count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            tasks = todoc_realloc(tasks, (size_t)capacity * sizeof(task_t));
            memset(&tasks[count], 0, (size_t)(capacity - count) * sizeof(task_t));
        }
        row_to_task(stmt, &tasks[count]);
        count++;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        db_task_list_free(tasks, count);
        return TODOC_ERR_DB;
    }

    *out_tasks = tasks;
    *out_count = count;
    return TODOC_OK;
}

void db_task_list_free(task_t *tasks, int count)
{
    if (!tasks) {
        return;
    }
    for (int i = 0; i < count; i++) {
        task_free(&tasks[i]);
    }
    free(tasks);
}

/* ── Set status shortcut ─────────────────────────────────────── */

todoc_err_t db_task_set_status(int64_t id, status_t status)
{
    const char *sql = "UPDATE tasks SET status = ? WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int(stmt, 1, (int)status);
    sqlite3_bind_int64(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return TODOC_ERR_DB;
    }

    if (sqlite3_changes(g_db) == 0) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_OK;
}

/* ── Statistics ──────────────────────────────────────────────── */

todoc_err_t db_task_stats(task_stats_t *out_stats)
{
    memset(out_stats, 0, sizeof(*out_stats));

    /* Total count */
    const char *sql_total = "SELECT COUNT(*) FROM tasks;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql_total, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_stats->total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    /* By status */
    const char *sql_status = "SELECT status, COUNT(*) FROM tasks GROUP BY status;";
    rc = sqlite3_prepare_v2(g_db, sql_status, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int s = sqlite3_column_int(stmt, 0);
        if (s >= 0 && s < STATUS_COUNT) {
            out_stats->by_status[s] = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);

    /* By priority */
    const char *sql_prio = "SELECT priority, COUNT(*) FROM tasks GROUP BY priority;";
    rc = sqlite3_prepare_v2(g_db, sql_prio, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int p = sqlite3_column_int(stmt, 0);
        if (p >= 0 && p < PRIORITY_COUNT) {
            out_stats->by_priority[p] = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);

    /* By type */
    const char *sql_type = "SELECT type, COUNT(*) FROM tasks GROUP BY type;";
    rc = sqlite3_prepare_v2(g_db, sql_type, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int t = sqlite3_column_int(stmt, 0);
        if (t >= 0 && t < TASK_TYPE_COUNT) {
            out_stats->by_type[t] = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);

    /* Overdue (status not done/cancelled, due_date < today) */
    const char *sql_overdue = "SELECT COUNT(*) FROM tasks "
                              "WHERE due_date IS NOT NULL "
                              "  AND due_date < date('now','localtime') "
                              "  AND status NOT IN (?, ?);";
    rc = sqlite3_prepare_v2(g_db, sql_overdue, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_int(stmt, 1, (int)STATUS_DONE);
    sqlite3_bind_int(stmt, 2, (int)STATUS_CANCELLED);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_stats->overdue = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return TODOC_OK;
}
