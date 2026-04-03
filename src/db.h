#ifndef TODOC_DB_H
#define TODOC_DB_H

#include "model.h"

#include <stdint.h>

/* Open the database at ~/.todoc/todoc.db (creates dir if needed) */
todoc_err_t db_open(void);

/* Close the database connection */
void db_close(void);

/* Create the schema (tasks table + triggers). Called by `todoc init` */
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

todoc_err_t db_task_stats(task_stats_t *out_stats);

/* Last SQLite error message (valid until next db_* call) */
const char *db_last_error(void);

#endif
