#include "commands.h"
#include "db.h"
#include "display.h"
#include "export.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* ── init ────────────────────────────────────────────────────── */

todoc_err_t cmd_init(const cli_args_t *args)
{
    (void)args;

    todoc_err_t err = db_open();
    if (err != TODOC_OK) {
        display_error("Failed to open database: %s", db_last_error());
        return err;
    }

    err = db_init_schema();
    if (err != TODOC_OK) {
        display_error("Failed to initialize schema: %s", db_last_error());
        return err;
    }

    char *path = todoc_db_path();
    display_success("Database initialized at %s", path ? path : "~/.todoc/todoc.db");
    free(path);
    return TODOC_OK;
}

/* ── add ─────────────────────────────────────────────────────── */

todoc_err_t cmd_add(const cli_args_t *args)
{
    if (!args->title || args->title[0] == '\0') {
        display_error("Task title is required.");
        display_info("Usage: todoc add \"title\" [--type bug --priority high ...]");
        return TODOC_ERR_INVALID;
    }

    task_t task = {0};
    task.title = args->title; /* borrowed, not freed by task_free here */
    task.description = args->description;
    task.type = args->type ? *args->type : TASK_TYPE_FEATURE;
    task.priority = args->priority ? *args->priority : PRIORITY_MEDIUM;
    task.status = STATUS_TODO;
    task.scope = args->scope;
    task.due_date = args->due_date;

    int64_t new_id = 0;
    todoc_err_t err = db_task_insert(&task, &new_id);
    if (err != TODOC_OK) {
        display_error("Failed to add task: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld created.", (long long)new_id);
    return TODOC_OK;
}

/* ── list ────────────────────────────────────────────────────── */

todoc_err_t cmd_list(const cli_args_t *args)
{
    task_t *tasks = NULL;
    int count = 0;

    todoc_err_t err = db_task_list(&args->filter, &tasks, &count);
    if (err != TODOC_OK) {
        display_error("Failed to list tasks: %s", db_last_error());
        return err;
    }

    display_task_list(tasks, count);
    db_task_list_free(tasks, count);
    return TODOC_OK;
}

/* ── show ────────────────────────────────────────────────────── */

todoc_err_t cmd_show(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc show <id>");
        return TODOC_ERR_INVALID;
    }

    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch task: %s", db_last_error());
        return err;
    }

    display_task_detail(&task);
    task_free(&task);
    return TODOC_OK;
}

/* ── edit ────────────────────────────────────────────────────── */

todoc_err_t cmd_edit(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc edit <id> [--title \"new\" --priority low ...]");
        return TODOC_ERR_INVALID;
    }

    /* Load existing task */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch task: %s", db_last_error());
        return err;
    }

    /* Overlay only the fields the user specified */
    if (args->title) {
        free(task.title);
        task.title = todoc_strdup(args->title);
    }
    if (args->description) {
        free(task.description);
        task.description = todoc_strdup(args->description);
    }
    if (args->type) {
        task.type = *args->type;
    }
    if (args->priority) {
        task.priority = *args->priority;
    }
    if (args->status) {
        task.status = *args->status;
    }
    if (args->scope) {
        free(task.scope);
        task.scope = todoc_strdup(args->scope);
    }
    if (args->due_date) {
        free(task.due_date);
        task.due_date = todoc_strdup(args->due_date);
    }

    err = db_task_update(&task);
    if (err != TODOC_OK) {
        display_error("Failed to update task: %s", db_last_error());
        task_free(&task);
        return err;
    }

    display_success("Task #%lld updated.", (long long)args->task_id);
    task_free(&task);
    return TODOC_OK;
}

/* ── done ────────────────────────────────────────────────────── */

todoc_err_t cmd_done(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc done <id>");
        return TODOC_ERR_INVALID;
    }

    todoc_err_t err = db_task_set_status(args->task_id, STATUS_DONE);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to mark task as done: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld marked as done.", (long long)args->task_id);
    return TODOC_OK;
}

/* ── rm ──────────────────────────────────────────────────────── */

todoc_err_t cmd_rm(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc rm <id>");
        return TODOC_ERR_INVALID;
    }

    todoc_err_t err = db_task_delete(args->task_id);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to delete task: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld deleted.", (long long)args->task_id);
    return TODOC_OK;
}

/* ── stats ───────────────────────────────────────────────────── */

todoc_err_t cmd_stats(const cli_args_t *args)
{
    (void)args;

    task_stats_t stats = {0};
    todoc_err_t err = db_task_stats(&stats);
    if (err != TODOC_OK) {
        display_error("Failed to get statistics: %s", db_last_error());
        return err;
    }

    if (stats.total == 0) {
        display_info("No tasks yet. Run 'todoc add' to create one.");
        return TODOC_OK;
    }

    display_stats(&stats);
    return TODOC_OK;
}

/* ── export ──────────────────────────────────────────────────── */

todoc_err_t cmd_export(const cli_args_t *args)
{
    task_t *tasks = NULL;
    int count = 0;

    todoc_err_t err = db_task_list(&args->filter, &tasks, &count);
    if (err != TODOC_OK) {
        display_error("Failed to list tasks: %s", db_last_error());
        return err;
    }

    switch (args->export_format) {
    case EXPORT_JSON:
        export_tasks_json(tasks, count);
        break;
    case EXPORT_CSV:
    default:
        export_tasks_csv(tasks, count);
        break;
    }

    db_task_list_free(tasks, count);
    return TODOC_OK;
}
