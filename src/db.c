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
 *                          scope, due_date, created_at, updated_at, parent_id */
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
    task->parent_id =
        sqlite3_column_type(stmt, 10) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 10);
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
    const char *sql = "INSERT INTO tasks (title, description, type, priority, status, scope, "
                      "due_date, parent_id) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

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
    if (task->parent_id > 0) {
        sqlite3_bind_int64(stmt, 8, task->parent_id);
    } else {
        sqlite3_bind_null(stmt, 8);
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
                      "       scope, due_date, created_at, updated_at, parent_id "
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
                      "status=?, scope=?, due_date=?, parent_id=? WHERE id=?;";

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
    if (task->parent_id > 0) {
        sqlite3_bind_int64(stmt, 8, task->parent_id);
    } else {
        sqlite3_bind_null(stmt, 8);
    }
    sqlite3_bind_int64(stmt, 9, task->id);

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
    char sql[1024];
    int sql_len = 0;

    sql_len = snprintf(sql, sizeof(sql),
                       "SELECT t.id, t.title, t.description, t.type, t.priority, t.status, "
                       "       t.scope, t.due_date, t.created_at, t.updated_at, t.parent_id "
                       "FROM tasks t");

    if (filter && filter->project) {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len,
                            " INNER JOIN task_projects tp ON t.id = tp.task_id"
                            " INNER JOIN projects p ON tp.project_id = p.id");
    }
    if (filter && filter->label) {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len,
                            " INNER JOIN task_labels tl ON t.id = tl.task_id"
                            " INNER JOIN labels lbl ON tl.label_id = lbl.id");
    }

    /* Collect WHERE clauses */
    int has_where = 0;
    int param_idx = 0;

    int bind_project = 0;
    int bind_label = 0;
    int bind_status = 0;
    int bind_priority = 0;
    int bind_type = 0;
    int bind_scope = 0;

    if (filter) {
        if (filter->project) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, " WHERE p.name = ?");
            has_where = 1;
            bind_project = ++param_idx;
        }
        if (filter->label) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s lbl.name = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_label = ++param_idx;
        }
        if (filter->status) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s t.status = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_status = ++param_idx;
        }
        if (filter->priority) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s t.priority = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_priority = ++param_idx;
        }
        if (filter->type) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s t.type = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_type = ++param_idx;
        }
        if (filter->scope) {
            sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, "%s t.scope = ?",
                                has_where ? " AND" : " WHERE");
            has_where = 1;
            bind_scope = ++param_idx;
        }
    }

    /* Default ordering: by priority (critical first), then by created_at desc */
    sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len,
                        " ORDER BY t.priority ASC, t.created_at DESC");

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
    if (bind_project) {
        sqlite3_bind_text(stmt, bind_project, filter->project, -1, SQLITE_STATIC);
    }
    if (bind_label) {
        sqlite3_bind_text(stmt, bind_label, filter->label, -1, SQLITE_STATIC);
    }
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

/* ── Subtasks ────────────────────────────────────────────────── */

todoc_err_t db_task_get_children(int64_t parent_id, task_t **out_children, int *out_count)
{
    *out_children = NULL;
    *out_count = 0;

    const char *sql = "SELECT id, title, description, type, priority, status, "
                      "       scope, due_date, created_at, updated_at, parent_id "
                      "FROM tasks WHERE parent_id = ? "
                      "ORDER BY priority ASC, created_at DESC;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_int64(stmt, 1, parent_id);

    int capacity = 8;
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

    *out_children = tasks;
    *out_count = count;
    return TODOC_OK;
}

int db_task_count_open_children(int64_t parent_id)
{
    const char *sql = "SELECT COUNT(*) FROM tasks "
                      "WHERE parent_id = ? "
                      "AND status NOT IN (?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, parent_id);
    sqlite3_bind_int(stmt, 2, (int)STATUS_DONE);
    sqlite3_bind_int(stmt, 3, (int)STATUS_CANCELLED);
    sqlite3_bind_int(stmt, 4, (int)STATUS_ABANDONED);

    int n = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

todoc_err_t db_task_set_parent(int64_t id, int64_t parent_id)
{
    const char *sql = "UPDATE tasks SET parent_id = ? WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    if (parent_id > 0) {
        sqlite3_bind_int64(stmt, 1, parent_id);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
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

todoc_err_t db_task_set_projects(int64_t task_id, const int64_t *project_ids, int n)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(g_db, "BEGIN;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return TODOC_ERR_DB;
    }

    /* Wipe existing assignments */
    sqlite3_stmt *del_stmt = NULL;
    if (sqlite3_prepare_v2(g_db, "DELETE FROM task_projects WHERE task_id = ?;", -1, &del_stmt,
                           NULL) != SQLITE_OK) {
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return TODOC_ERR_DB;
    }
    sqlite3_bind_int64(del_stmt, 1, task_id);
    if (sqlite3_step(del_stmt) != SQLITE_DONE) {
        sqlite3_finalize(del_stmt);
        sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
        return TODOC_ERR_DB;
    }
    sqlite3_finalize(del_stmt);

    /* Insert the new ones */
    if (project_ids && n > 0) {
        sqlite3_stmt *ins_stmt = NULL;
        if (sqlite3_prepare_v2(g_db,
                               "INSERT OR IGNORE INTO task_projects (task_id, project_id) "
                               "VALUES (?, ?);",
                               -1, &ins_stmt, NULL) != SQLITE_OK) {
            sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
            return TODOC_ERR_DB;
        }
        for (int i = 0; i < n; i++) {
            sqlite3_reset(ins_stmt);
            sqlite3_bind_int64(ins_stmt, 1, task_id);
            sqlite3_bind_int64(ins_stmt, 2, project_ids[i]);
            if (sqlite3_step(ins_stmt) != SQLITE_DONE) {
                sqlite3_finalize(ins_stmt);
                sqlite3_exec(g_db, "ROLLBACK;", NULL, NULL, NULL);
                return TODOC_ERR_DB;
            }
        }
        sqlite3_finalize(ins_stmt);
    }

    rc = sqlite3_exec(g_db, "COMMIT;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return TODOC_ERR_DB;
    }
    return TODOC_OK;
}

/* ── Statistics ──────────────────────────────────────────────── */

todoc_err_t db_task_stats(task_stats_t *out_stats, const char *project_name)
{
    memset(out_stats, 0, sizeof(*out_stats));

    /* Build FROM clause — with or without project join */
    const char *from_clause = "FROM tasks t";
    const char *join_clause = "";
    const char *where_prefix = "";

    if (project_name) {
        join_clause = " INNER JOIN task_projects tp ON t.id = tp.task_id"
                      " INNER JOIN projects p ON tp.project_id = p.id";
        where_prefix = " WHERE p.name = ?";
    }

    char sql[512];
    sqlite3_stmt *stmt = NULL;
    int rc;

    /* Total count */
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) %s%s%s;", from_clause, join_clause, where_prefix);
    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    if (project_name) {
        sqlite3_bind_text(stmt, 1, project_name, -1, SQLITE_STATIC);
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_stats->total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    /* By status */
    snprintf(sql, sizeof(sql), "SELECT t.status, COUNT(*) %s%s%s GROUP BY t.status;", from_clause,
             join_clause, where_prefix);
    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    if (project_name) {
        sqlite3_bind_text(stmt, 1, project_name, -1, SQLITE_STATIC);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int s = sqlite3_column_int(stmt, 0);
        if (s >= 0 && s < STATUS_COUNT) {
            out_stats->by_status[s] = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);

    /* By priority */
    snprintf(sql, sizeof(sql), "SELECT t.priority, COUNT(*) %s%s%s GROUP BY t.priority;",
             from_clause, join_clause, where_prefix);
    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    if (project_name) {
        sqlite3_bind_text(stmt, 1, project_name, -1, SQLITE_STATIC);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int p = sqlite3_column_int(stmt, 0);
        if (p >= 0 && p < PRIORITY_COUNT) {
            out_stats->by_priority[p] = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);

    /* By type */
    snprintf(sql, sizeof(sql), "SELECT t.type, COUNT(*) %s%s%s GROUP BY t.type;", from_clause,
             join_clause, where_prefix);
    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    if (project_name) {
        sqlite3_bind_text(stmt, 1, project_name, -1, SQLITE_STATIC);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int t = sqlite3_column_int(stmt, 0);
        if (t >= 0 && t < TASK_TYPE_COUNT) {
            out_stats->by_type[t] = sqlite3_column_int(stmt, 1);
        }
    }
    sqlite3_finalize(stmt);

    /* Overdue (status not done/cancelled, due_date < today) */
    if (project_name) {
        snprintf(sql, sizeof(sql),
                 "SELECT COUNT(*) %s%s%s AND t.due_date IS NOT NULL"
                 " AND t.due_date < date('now','localtime')"
                 " AND t.status NOT IN (?, ?);",
                 from_clause, join_clause, where_prefix);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT COUNT(*) %s%s WHERE t.due_date IS NOT NULL"
                 " AND t.due_date < date('now','localtime')"
                 " AND t.status NOT IN (?, ?);",
                 from_clause, join_clause);
    }
    rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    if (project_name) {
        sqlite3_bind_text(stmt, 1, project_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, (int)STATUS_DONE);
        sqlite3_bind_int(stmt, 3, (int)STATUS_CANCELLED);
    } else {
        sqlite3_bind_int(stmt, 1, (int)STATUS_DONE);
        sqlite3_bind_int(stmt, 2, (int)STATUS_CANCELLED);
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out_stats->overdue = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    return TODOC_OK;
}

/* ── Project helpers ────────────────────────────────────────── */

static void row_to_project(sqlite3_stmt *stmt, project_t *project)
{
    project->id = sqlite3_column_int64(stmt, 0);
    project->name = col_text_dup(stmt, 1);
    project->description = col_text_dup(stmt, 2);
    project->color = col_text_dup(stmt, 3);
    project->status = (project_status_t)sqlite3_column_int(stmt, 4);
    project->due_date = col_text_dup(stmt, 5);
    project->created_at = col_text_dup(stmt, 6);
    project->updated_at = col_text_dup(stmt, 7);
}

/* ── Project CRUD ───────────────────────────────────────────── */

todoc_err_t db_project_insert(const project_t *project, int64_t *out_id)
{
    const char *sql = "INSERT INTO projects (name, description, color, status, due_date) "
                      "VALUES (?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, project->name, -1, SQLITE_STATIC);
    if (project->description) {
        sqlite3_bind_text(stmt, 2, project->description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    if (project->color) {
        sqlite3_bind_text(stmt, 3, project->color, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, (int)project->status);
    if (project->due_date) {
        sqlite3_bind_text(stmt, 5, project->due_date, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 5);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (sqlite3_extended_errcode(g_db) == SQLITE_CONSTRAINT_UNIQUE) {
            return TODOC_ERR_INVALID;
        }
        return TODOC_ERR_DB;
    }

    if (out_id) {
        *out_id = sqlite3_last_insert_rowid(g_db);
    }
    return TODOC_OK;
}

todoc_err_t db_project_get(int64_t id, project_t *out_project)
{
    const char *sql = "SELECT id, name, description, color, status, "
                      "       due_date, created_at, updated_at "
                      "FROM projects WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        memset(out_project, 0, sizeof(*out_project));
        row_to_project(stmt, out_project);
        sqlite3_finalize(stmt);
        return TODOC_OK;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_ERR_DB;
}

todoc_err_t db_project_get_by_name(const char *name, project_t *out_project)
{
    const char *sql = "SELECT id, name, description, color, status, "
                      "       due_date, created_at, updated_at "
                      "FROM projects WHERE name = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        memset(out_project, 0, sizeof(*out_project));
        row_to_project(stmt, out_project);
        sqlite3_finalize(stmt);
        return TODOC_OK;
    }

    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_ERR_DB;
}

todoc_err_t db_project_update(const project_t *project)
{
    const char *sql = "UPDATE projects SET name=?, description=?, color=?, status=?, "
                      "due_date=? WHERE id=?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_text(stmt, 1, project->name, -1, SQLITE_STATIC);
    if (project->description) {
        sqlite3_bind_text(stmt, 2, project->description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    if (project->color) {
        sqlite3_bind_text(stmt, 3, project->color, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, (int)project->status);
    if (project->due_date) {
        sqlite3_bind_text(stmt, 5, project->due_date, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 5);
    }
    sqlite3_bind_int64(stmt, 6, project->id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (sqlite3_extended_errcode(g_db) == SQLITE_CONSTRAINT_UNIQUE) {
            return TODOC_ERR_INVALID;
        }
        return TODOC_ERR_DB;
    }

    if (sqlite3_changes(g_db) == 0) {
        return TODOC_ERR_NOT_FOUND;
    }
    return TODOC_OK;
}

todoc_err_t db_project_delete(int64_t id)
{
    const char *sql = "DELETE FROM projects WHERE id = ?;";

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

todoc_err_t db_project_list(const project_filter_t *filter, project_t **out_projects,
                            int *out_count)
{
    *out_projects = NULL;
    *out_count = 0;

    char sql[256];
    int sql_len = snprintf(sql, sizeof(sql),
                           "SELECT id, name, description, color, status, "
                           "       due_date, created_at, updated_at "
                           "FROM projects");

    int bind_status = 0;
    if (filter && filter->status) {
        sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, " WHERE status = ?");
        bind_status = 1;
    }

    sql_len += snprintf(sql + sql_len, sizeof(sql) - (size_t)sql_len, " ORDER BY name ASC");

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

    if (bind_status) {
        sqlite3_bind_int(stmt, 1, (int)*filter->status);
    }

    int capacity = 8;
    project_t *projects = todoc_calloc((size_t)capacity, sizeof(project_t));
    int count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            projects = todoc_realloc(projects, (size_t)capacity * sizeof(project_t));
            memset(&projects[count], 0, (size_t)(capacity - count) * sizeof(project_t));
        }
        row_to_project(stmt, &projects[count]);
        count++;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        db_project_list_free(projects, count);
        return TODOC_ERR_DB;
    }

    *out_projects = projects;
    *out_count = count;
    return TODOC_OK;
}

void db_project_list_free(project_t *projects, int count)
{
    if (!projects) {
        return;
    }
    for (int i = 0; i < count; i++) {
        project_free(&projects[i]);
    }
    free(projects);
}

int db_project_task_count(int64_t project_id)
{
    const char *sql = "SELECT COUNT(*) FROM task_projects WHERE project_id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, project_id);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ── Task-Project junction ──────────────────────────────────── */

todoc_err_t db_task_assign_project(int64_t task_id, int64_t project_id)
{
    const char *sql = "INSERT OR IGNORE INTO task_projects (task_id, project_id) VALUES (?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int64(stmt, 1, task_id);
    sqlite3_bind_int64(stmt, 2, project_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return TODOC_ERR_DB;
    }
    return TODOC_OK;
}

todoc_err_t db_task_unassign_project(int64_t task_id, int64_t project_id)
{
    const char *sql = "DELETE FROM task_projects WHERE task_id = ? AND project_id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int64(stmt, 1, task_id);
    sqlite3_bind_int64(stmt, 2, project_id);

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

/* ── Label CRUD ─────────────────────────────────────────────── */

static void row_to_label(sqlite3_stmt *stmt, label_t *label)
{
    label->id = sqlite3_column_int64(stmt, 0);
    label->name = col_text_dup(stmt, 1);
    label->color = col_text_dup(stmt, 2);
    label->created_at = col_text_dup(stmt, 3);
}

todoc_err_t db_label_insert(const label_t *label, int64_t *out_id)
{
    const char *sql = "INSERT INTO labels (name, color) VALUES (?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_text(stmt, 1, label->name, -1, SQLITE_STATIC);
    if (label->color) {
        sqlite3_bind_text(stmt, 2, label->color, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        /* UNIQUE constraint failure */
        return TODOC_ERR_INVALID;
    }
    if (out_id) {
        *out_id = sqlite3_last_insert_rowid(g_db);
    }
    return TODOC_OK;
}

todoc_err_t db_label_get_by_name(const char *name, label_t *out_label)
{
    const char *sql = "SELECT id, name, color, created_at FROM labels WHERE name = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        memset(out_label, 0, sizeof(*out_label));
        row_to_label(stmt, out_label);
        sqlite3_finalize(stmt);
        return TODOC_OK;
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? TODOC_ERR_NOT_FOUND : TODOC_ERR_DB;
}

todoc_err_t db_label_delete_by_name(const char *name)
{
    const char *sql = "DELETE FROM labels WHERE name = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
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

todoc_err_t db_label_list(label_t **out_labels, int *out_count)
{
    *out_labels = NULL;
    *out_count = 0;

    const char *sql = "SELECT id, name, color, created_at FROM labels ORDER BY name;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    int capacity = 8;
    label_t *labels = todoc_calloc((size_t)capacity, sizeof(label_t));
    int count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            labels = todoc_realloc(labels, (size_t)capacity * sizeof(label_t));
            memset(&labels[count], 0, (size_t)(capacity - count) * sizeof(label_t));
        }
        row_to_label(stmt, &labels[count]);
        count++;
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        db_label_list_free(labels, count);
        return TODOC_ERR_DB;
    }

    *out_labels = labels;
    *out_count = count;
    return TODOC_OK;
}

void db_label_list_free(label_t *labels, int count)
{
    if (!labels) {
        return;
    }
    for (int i = 0; i < count; i++) {
        label_free(&labels[i]);
    }
    free(labels);
}

todoc_err_t db_label_ensure(const char *name, int64_t *out_id)
{
    label_t existing = {0};
    todoc_err_t err = db_label_get_by_name(name, &existing);
    if (err == TODOC_OK) {
        if (out_id) {
            *out_id = existing.id;
        }
        label_free(&existing);
        return TODOC_OK;
    }
    if (err != TODOC_ERR_NOT_FOUND) {
        return err;
    }

    label_t fresh = {0};
    fresh.name = (char *)name; /* borrowed */
    return db_label_insert(&fresh, out_id);
}

/* ── Task-Label junction ────────────────────────────────────── */

todoc_err_t db_task_attach_label(int64_t task_id, int64_t label_id)
{
    const char *sql = "INSERT OR IGNORE INTO task_labels (task_id, label_id) VALUES (?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_int64(stmt, 1, task_id);
    sqlite3_bind_int64(stmt, 2, label_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE ? TODOC_OK : TODOC_ERR_DB;
}

todoc_err_t db_task_detach_label(int64_t task_id, int64_t label_id)
{
    const char *sql = "DELETE FROM task_labels WHERE task_id = ? AND label_id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_int64(stmt, 1, task_id);
    sqlite3_bind_int64(stmt, 2, label_id);

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

todoc_err_t db_task_get_labels(int64_t task_id, label_t **out_labels, int *out_count)
{
    *out_labels = NULL;
    *out_count = 0;

    const char *sql = "SELECT l.id, l.name, l.color, l.created_at "
                      "FROM labels l "
                      "INNER JOIN task_labels tl ON tl.label_id = l.id "
                      "WHERE tl.task_id = ? "
                      "ORDER BY l.name;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }
    sqlite3_bind_int64(stmt, 1, task_id);

    int capacity = 4;
    label_t *labels = todoc_calloc((size_t)capacity, sizeof(label_t));
    int count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            labels = todoc_realloc(labels, (size_t)capacity * sizeof(label_t));
            memset(&labels[count], 0, (size_t)(capacity - count) * sizeof(label_t));
        }
        row_to_label(stmt, &labels[count]);
        count++;
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        db_label_list_free(labels, count);
        return TODOC_ERR_DB;
    }

    *out_labels = labels;
    *out_count = count;
    return TODOC_OK;
}
