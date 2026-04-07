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

/* ── Subtasks ────────────────────────────────────────────────── */

/* Fetch all direct children of a task. Caller must call
 * db_task_list_free(*out_children, *out_count) */
todoc_err_t db_task_get_children(int64_t parent_id, task_t **out_children, int *out_count);

/* Count children of a task that are NOT in a terminal status
 * (done, cancelled, abandoned). Returns -1 on error. */
int db_task_count_open_children(int64_t parent_id);

/* Set or clear a task's parent. Pass 0 to make a task top-level. */
todoc_err_t db_task_set_parent(int64_t id, int64_t parent_id);

/* Replace a task's project assignments with exactly the projects
 * in project_ids[0..n-1]. Pass project_ids=NULL/n=0 to remove all
 * assignments. Atomic — uses a transaction so list is all-or-nothing. */
todoc_err_t db_task_set_projects(int64_t task_id, const int64_t *project_ids, int n);

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

/* ── Label CRUD ─────────────────────────────────────────────── */

todoc_err_t db_label_insert(const label_t *label, int64_t *out_id);
todoc_err_t db_label_get_by_name(const char *name, label_t *out_label);
todoc_err_t db_label_delete_by_name(const char *name);
todoc_err_t db_label_list(label_t **out_labels, int *out_count);
void db_label_list_free(label_t *labels, int count);

/* Get-or-create a label by name. *out_id is set to the label id. */
todoc_err_t db_label_ensure(const char *name, int64_t *out_id);

/* ── Task-Label junction ────────────────────────────────────── */

todoc_err_t db_task_attach_label(int64_t task_id, int64_t label_id);
todoc_err_t db_task_detach_label(int64_t task_id, int64_t label_id);

/* Fetch the labels attached to a task. Caller frees with
 * db_label_list_free(*out_labels, *out_count). */
todoc_err_t db_task_get_labels(int64_t task_id, label_t **out_labels, int *out_count);

/* Last SQLite error message (valid until next db_* call) */
const char *db_last_error(void);

#endif
