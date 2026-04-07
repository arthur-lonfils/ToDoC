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

    /* Auto-assign to project if --project specified or active project is set */
    const char *proj = args->project;
    char *active = NULL;
    if (!proj) {
        active = todoc_get_active_project();
        proj = active;
    }
    if (proj) {
        project_t project = {0};
        todoc_err_t perr = db_project_get_by_name(proj, &project);
        if (perr == TODOC_OK) {
            db_task_assign_project(new_id, project.id);
            display_success("Task #%lld created and assigned to project '%s'.", (long long)new_id,
                            proj);
            project_free(&project);
            free(active);
            return TODOC_OK;
        }
        if (perr == TODOC_ERR_NOT_FOUND && args->project) {
            display_warn("Project '%s' not found. Task created but not assigned.", proj);
        }
        project_free(&project);
    }
    free(active);

    display_success("Task #%lld created.", (long long)new_id);
    return TODOC_OK;
}

/* ── list ────────────────────────────────────────────────────── */

todoc_err_t cmd_list(const cli_args_t *args)
{
    task_t *tasks = NULL;
    int count = 0;

    /* Inject active project if no explicit --project or --all */
    char *active = NULL;
    task_filter_t filter = args->filter;
    if (!filter.project && !args->all) {
        active = todoc_get_active_project();
        if (active) {
            filter.project = active;
        }
    }

    todoc_err_t err = db_task_list(&filter, &tasks, &count);
    if (err != TODOC_OK) {
        display_error("Failed to list tasks: %s", db_last_error());
        free(active);
        return err;
    }

    if (filter.project) {
        display_info("Showing tasks for project '%s' (use --all to show all)", filter.project);
    }

    display_task_list(tasks, count);
    db_task_list_free(tasks, count);
    free(active);
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
    /* Inject active project if no explicit --project or --all */
    const char *proj = args->project;
    char *active = NULL;
    if (!proj && !args->all) {
        active = todoc_get_active_project();
        proj = active;
    }

    task_stats_t stats = {0};
    todoc_err_t err = db_task_stats(&stats, proj);
    if (err != TODOC_OK) {
        display_error("Failed to get statistics: %s", db_last_error());
        free(active);
        return err;
    }

    if (stats.total == 0) {
        display_info("No tasks yet. Run 'todoc add' to create one.");
        free(active);
        return TODOC_OK;
    }

    if (proj) {
        display_info("Statistics for project '%s'", proj);
    }

    display_stats(&stats);
    free(active);
    return TODOC_OK;
}

/* ── export ──────────────────────────────────────────────────── */

todoc_err_t cmd_export(const cli_args_t *args)
{
    task_t *tasks = NULL;
    int count = 0;

    /* Inject active project if no explicit --project or --all */
    char *active = NULL;
    task_filter_t filter = args->filter;
    if (!filter.project && !args->all) {
        active = todoc_get_active_project();
        if (active) {
            filter.project = active;
        }
    }

    todoc_err_t err = db_task_list(&filter, &tasks, &count);
    if (err != TODOC_OK) {
        display_error("Failed to list tasks: %s", db_last_error());
        free(active);
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
    free(active);
    return TODOC_OK;
}

/* ══════════════════════════════════════════════════════════════ */
/* ── Project commands ─────────────────────────────────────────── */
/* ════════════════════════════════════════════════════════════���═ */

/* ── add-project ────────────────────────────────────────────── */

todoc_err_t cmd_add_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        display_error("Project name is required.");
        display_info(
            "Usage: todoc add-project <name> [--desc \"...\" --color blue --due YYYY-MM-DD]");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    project.name = args->project_name;
    project.description = args->description;
    project.color = args->project_color;
    project.status = args->project_status ? *args->project_status : PROJECT_ACTIVE;
    project.due_date = args->due_date;

    int64_t new_id = 0;
    todoc_err_t err = db_project_insert(&project, &new_id);
    if (err == TODOC_ERR_INVALID) {
        display_error("Project '%s' already exists.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to create project: %s", db_last_error());
        return err;
    }

    display_success("Project '%s' created (#%lld).", args->project_name, (long long)new_id);
    return TODOC_OK;
}

/* ── list-projects ──────────────────────────────────────────── */

todoc_err_t cmd_list_projects(const cli_args_t *args)
{
    project_filter_t filter = {0};
    if (args->project_status) {
        filter.status = args->project_status;
    }

    project_t *projects = NULL;
    int count = 0;

    todoc_err_t err = db_project_list(&filter, &projects, &count);
    if (err != TODOC_OK) {
        display_error("Failed to list projects: %s", db_last_error());
        return err;
    }

    display_project_list(projects, count);
    db_project_list_free(projects, count);
    return TODOC_OK;
}

/* ── show-project ───────────────────────────────────────────── */

todoc_err_t cmd_show_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        display_error("Project name is required.");
        display_info("Usage: todoc show-project <name>");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch project: %s", db_last_error());
        return err;
    }

    int task_count = db_project_task_count(project.id);
    display_project_detail(&project, task_count);
    project_free(&project);
    return TODOC_OK;
}

/* ── edit-project ───────────────────────────────────────────── */

todoc_err_t cmd_edit_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        display_error("Project name is required.");
        display_info("Usage: todoc edit-project <name> [--desc \"...\" --color blue ...]");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch project: %s", db_last_error());
        return err;
    }

    /* Overlay specified fields */
    if (args->description) {
        free(project.description);
        project.description = todoc_strdup(args->description);
    }
    if (args->project_color) {
        free(project.color);
        project.color = todoc_strdup(args->project_color);
    }
    if (args->project_status) {
        project.status = *args->project_status;
    }
    if (args->due_date) {
        free(project.due_date);
        project.due_date = todoc_strdup(args->due_date);
    }

    err = db_project_update(&project);
    if (err != TODOC_OK) {
        display_error("Failed to update project: %s", db_last_error());
        project_free(&project);
        return err;
    }

    display_success("Project '%s' updated.", args->project_name);
    project_free(&project);
    return TODOC_OK;
}

/* ── rm-project ─────────────────────────────────────────────── */

todoc_err_t cmd_rm_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        display_error("Project name is required.");
        display_info("Usage: todoc rm-project <name>");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch project: %s", db_last_error());
        return err;
    }

    err = db_project_delete(project.id);
    project_free(&project);
    if (err != TODOC_OK) {
        display_error("Failed to delete project: %s", db_last_error());
        return err;
    }

    /* Clear active project if it was the deleted one */
    char *active = todoc_get_active_project();
    if (active && strcmp(active, args->project_name) == 0) {
        todoc_set_active_project(NULL);
    }
    free(active);

    display_success("Project '%s' deleted.", args->project_name);
    return TODOC_OK;
}

/* ── use ────────────────────────────────────────────────────── */

todoc_err_t cmd_use(const cli_args_t *args)
{
    if (args->clear) {
        todoc_set_active_project(NULL);
        display_success("Active project cleared.");
        return TODOC_OK;
    }

    if (!args->project_name || args->project_name[0] == '\0') {
        /* Show current active project */
        char *active = todoc_get_active_project();
        if (active) {
            display_info("Active project: %s", active);
            free(active);
        } else {
            display_info("No active project. Usage: todoc use <name>");
        }
        return TODOC_OK;
    }

    /* Validate project exists */
    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch project: %s", db_last_error());
        return err;
    }
    project_free(&project);

    todoc_set_active_project(args->project_name);
    display_success("Now using project '%s'.", args->project_name);
    return TODOC_OK;
}

/* ── assign ─────────────────────────────────────────────────── */

todoc_err_t cmd_assign(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc assign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }
    if (!args->project_name || args->project_name[0] == '\0') {
        display_error("Project name is required.");
        display_info("Usage: todoc assign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }

    /* Verify task exists */
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
    task_free(&task);

    /* Verify project exists */
    project_t project = {0};
    err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch project: %s", db_last_error());
        return err;
    }

    err = db_task_assign_project(args->task_id, project.id);
    project_free(&project);
    if (err != TODOC_OK) {
        display_error("Failed to assign task: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld assigned to project '%s'.", (long long)args->task_id,
                    args->project_name);
    return TODOC_OK;
}

/* ── unassign ───────────────────────────────────────────────── */

todoc_err_t cmd_unassign(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc unassign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }
    if (!args->project_name || args->project_name[0] == '\0') {
        display_error("Project name is required.");
        display_info("Usage: todoc unassign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to fetch project: %s", db_last_error());
        return err;
    }

    err = db_task_unassign_project(args->task_id, project.id);
    project_free(&project);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld is not assigned to project '%s'.", (long long)args->task_id,
                      args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to unassign task: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld removed from project '%s'.", (long long)args->task_id,
                    args->project_name);
    return TODOC_OK;
}
