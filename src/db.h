#ifndef TODOC_DB_H
#define TODOC_DB_H

#include "model.h"

#include <sqlite3.h>
#include <stdint.h>

/* Open the database at ~/.todoc/todoc.db (creates dir if needed) */
todoc_err_t db_open(void);

/* Close the database connection */
void db_close(void);

/* Get the raw sqlite3 handle (for the migration runner) */
sqlite3 *db_get_handle(void);

/* Run all pending migrations. Called by `todoc init` */
todoc_err_t db_init_schema(void);

/* ── CRUD ────────────────────────────────────────────────────── */

/* Insert a new task. On success, *out_id is set to the new row id */
todoc_err_t db_task_insert(const task_t *task, int64_t *out_id);

/* Fetch a single task by id. Caller must call task_free(out_task) */
todoc_err_t db_task_get(int64_t id, task_t *out_task);

/* Update an existing task (all fields overwritten) */
todoc_err_t db_task_update(const task_t *task);

/* Delete a task by id */
todoc_err_t db_task_delete(int64_t id);

/* ── Queries ─────────────────────────────────────────────────── */

/* List tasks matching filter. Returns heap-allocated array in *out_tasks.
 * Caller must call db_task_list_free(*out_tasks, *out_count) */
todoc_err_t db_task_list(const task_filter_t *filter, task_t **out_tasks, int *out_count);

/* Free an array returned by db_task_list */
void db_task_list_free(task_t *tasks, int count);

/* Shortcut: set only the status of a task */
todoc_err_t db_task_set_status(int64_t id, status_t status);

/* ── Statistics ──────────────────────────────────────────────── */

/* Pass project_name=NULL for global stats, or a project name for scoped stats */
todoc_err_t db_task_stats(task_stats_t *out_stats, const char *project_name);

/* ── Project CRUD ───────────────────────────────────────────── */

todoc_err_t db_project_insert(const project_t *project, int64_t *out_id);
todoc_err_t db_project_get(int64_t id, project_t *out_project);
todoc_err_t db_project_get_by_name(const char *name, project_t *out_project);
todoc_err_t db_project_update(const project_t *project);
todoc_err_t db_project_delete(int64_t id);
todoc_err_t db_project_list(const project_filter_t *filter, project_t **out_projects,
                            int *out_count);
void db_project_list_free(project_t *projects, int count);
int db_project_task_count(int64_t project_id);

/* ── Task-Project junction ──────────────────────────────────── */

todoc_err_t db_task_assign_project(int64_t task_id, int64_t project_id);
todoc_err_t db_task_unassign_project(int64_t task_id, int64_t project_id);

/* Last SQLite error message (valid until next db_* call) */
const char *db_last_error(void);

#endif
