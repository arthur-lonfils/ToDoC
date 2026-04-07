#include "commands.h"
#include "changelog.h"
#include "db.h"
#include "display.h"
#include "export.h"
#include "output.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── init ────────────────────────────────────────────────────── */

todoc_err_t cmd_init(const cli_args_t *args)
{
    (void)args;

    todoc_err_t err = db_open();
    if (err != TODOC_OK) {
        output_error("init", "db_error", "Failed to open database: %s", db_last_error());
        return err;
    }

    err = db_init_schema();
    if (err != TODOC_OK) {
        output_error("init", "db_error", "Failed to initialize schema: %s", db_last_error());
        return err;
    }

    char *path = todoc_db_path();
    output_init_db(path ? path : "~/.todoc/todoc.db", 0);
    free(path);
    return TODOC_OK;
}

/* ── helpers ─────────────────────────────────────────────────── */

/* Attach a comma-separated list of labels to a task. Each label is
 * trimmed and auto-created if missing. Returns the number of labels
 * attached, or -1 on error. */
static int attach_labels_csv(int64_t task_id, const char *csv)
{
    if (!csv || !*csv) {
        return 0;
    }
    char *copy = todoc_strdup(csv);
    int n = 0;
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        /* trim leading/trailing whitespace */
        while (*tok == ' ' || *tok == '\t') {
            tok++;
        }
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';
        if (*tok == '\0') {
            continue;
        }
        int64_t label_id = 0;
        if (db_label_ensure(tok, &label_id) != TODOC_OK) {
            display_warn("Could not create or find label '%s'.", tok);
            continue;
        }
        if (db_task_attach_label(task_id, label_id) == TODOC_OK) {
            n++;
        }
    }
    free(copy);
    return n;
}

/* Validate that <parent_id> exists and is itself a top-level task
 * (we only allow one level of nesting). Sets *out_err with the
 * appropriate code on failure. Returns 0 on success, -1 on failure. */
static int validate_parent(int64_t parent_id, todoc_err_t *out_err)
{
    if (parent_id <= 0) {
        *out_err = TODOC_ERR_INVALID;
        output_error("edit", "invalid_input", "Invalid parent task id.");
        return -1;
    }
    task_t parent = {0};
    todoc_err_t err = db_task_get(parent_id, &parent);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("edit", "not_found", "Parent task #%lld not found.", (long long)parent_id);
        *out_err = err;
        return -1;
    }
    if (err != TODOC_OK) {
        output_error("edit", "db_error", "Failed to load parent task: %s", db_last_error());
        *out_err = err;
        return -1;
    }
    if (parent.parent_id > 0) {
        output_error("edit", "nesting_too_deep",
                     "Task #%lld is itself a subtask. Only one level of nesting is allowed.",
                     (long long)parent_id);
        task_free(&parent);
        *out_err = TODOC_ERR_INVALID;
        return -1;
    }
    task_free(&parent);
    return 0;
}

/* ── add ─────────────────────────────────────────────────────── */

todoc_err_t cmd_add(const cli_args_t *args)
{
    if (!args->title || args->title[0] == '\0') {
        output_error("add", "invalid_input", "Task title is required.");
        display_info("Usage: todoc add \"title\" [--type bug --priority high ...]");
        return TODOC_ERR_INVALID;
    }

    /* If --sub <parent> was given, validate it before inserting. */
    if (args->parent_set) {
        if (args->parent_id <= 0) {
            output_error("add", "invalid_input", "--sub requires a parent task id.");
            return TODOC_ERR_INVALID;
        }
        todoc_err_t verr = TODOC_OK;
        if (validate_parent(args->parent_id, &verr) != 0) {
            return verr;
        }
    }

    task_t task = {0};
    task.title = args->title; /* borrowed, not freed by task_free here */
    task.description = args->description;
    task.type = args->type ? *args->type : TASK_TYPE_FEATURE;
    task.priority = args->priority ? *args->priority : PRIORITY_MEDIUM;
    task.status = STATUS_TODO;
    task.scope = args->scope;
    task.due_date = args->due_date;
    task.parent_id = args->parent_set && args->parent_id > 0 ? args->parent_id : 0;

    int64_t new_id = 0;
    todoc_err_t err = db_task_insert(&task, &new_id);
    if (err != TODOC_OK) {
        output_error("add", "db_error", "Failed to add task: %s", db_last_error());
        return err;
    }

    /* Attach labels from --label foo,bar (auto-creates) */
    int n_labels = attach_labels_csv(new_id, args->labels);

    /* Auto-assign to project if --project specified or active project is set */
    const char *proj = args->project;
    char *active = NULL;
    int assigned_project = 0;
    if (!proj) {
        active = todoc_get_active_project();
        proj = active;
    }
    if (proj) {
        project_t project = {0};
        todoc_err_t perr = db_project_get_by_name(proj, &project);
        if (perr == TODOC_OK) {
            db_task_assign_project(new_id, project.id);
            assigned_project = 1;
            project_free(&project);
        } else {
            if (perr == TODOC_ERR_NOT_FOUND && args->project) {
                display_warn("Project '%s' not found. Task created but not assigned.", proj);
            }
            project_free(&project);
        }
    }

    /* Re-fetch the inserted task so the JSON envelope (in ai mode)
     * carries the full populated row, including created_at/updated_at
     * and the canonical id. */
    task_t inserted = {0};
    if (db_task_get(new_id, &inserted) == TODOC_OK) {
        output_task_added(&inserted, assigned_project ? proj : NULL, n_labels);
        task_free(&inserted);
    } else {
        task.id = new_id;
        output_task_added(&task, assigned_project ? proj : NULL, n_labels);
    }

    free(active);
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
        output_error("list", "db_error", "Failed to list tasks: %s", db_last_error());
        free(active);
        return err;
    }

    output_list_tasks(tasks, count, filter.project);
    db_task_list_free(tasks, count);
    free(active);
    return TODOC_OK;
}

/* ── show ────────────────────────────────────────────────────── */

todoc_err_t cmd_show(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("show", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc show <id>");
        return TODOC_ERR_INVALID;
    }

    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("show", "not_found", "Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("show", "db_error", "Failed to fetch task: %s", db_last_error());
        return err;
    }

    /* Gather labels and children before emitting so the ai-mode JSON
     * envelope can carry all three sections in a single object. */
    label_t *labels = NULL;
    int n_labels = 0;
    db_task_get_labels(args->task_id, &labels, &n_labels);

    task_t *children = NULL;
    int n_children = 0;
    db_task_get_children(args->task_id, &children, &n_children);

    output_show_task(&task, children, n_children, labels, n_labels);

    db_label_list_free(labels, n_labels);
    db_task_list_free(children, n_children);
    task_free(&task);
    return TODOC_OK;
}

/* ── edit ────────────────────────────────────────────────────── */

todoc_err_t cmd_edit(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("edit", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc edit <id> [--title \"new\" --priority low ...]");
        return TODOC_ERR_INVALID;
    }

    /* Load existing task */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("edit", "not_found", "Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("edit", "db_error", "Failed to fetch task: %s", db_last_error());
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
    if (args->parent_set) {
        if (args->parent_id < 0) {
            /* --sub none → promote to top-level */
            task.parent_id = 0;
        } else {
            if (args->parent_id == args->task_id) {
                output_error("edit", "invalid_input", "A task cannot be its own parent.");
                task_free(&task);
                return TODOC_ERR_INVALID;
            }
            /* Refuse to nest a task that already has children */
            int kids = db_task_count_open_children(args->task_id);
            task_t check = {0};
            if (db_task_get(args->task_id, &check) == TODOC_OK) {
                /* Use a separate count covering ALL children, not just open. */
                task_t *all_children = NULL;
                int n_children = 0;
                if (db_task_get_children(args->task_id, &all_children, &n_children) == TODOC_OK) {
                    if (n_children > 0) {
                        output_error("edit", "has_children",
                                     "Task #%lld has %d child task(s); it cannot itself become a "
                                     "subtask.",
                                     (long long)args->task_id, n_children);
                        db_task_list_free(all_children, n_children);
                        task_free(&check);
                        task_free(&task);
                        return TODOC_ERR_INVALID;
                    }
                    db_task_list_free(all_children, n_children);
                }
                task_free(&check);
            }
            (void)kids;
            todoc_err_t verr = TODOC_OK;
            if (validate_parent(args->parent_id, &verr) != 0) {
                task_free(&task);
                return verr;
            }
            task.parent_id = args->parent_id;
        }
    }

    /* If the user is moving the task to STATUS_DONE via --status,
     * enforce the "all children must be terminal" rule. */
    if (args->status && *args->status == STATUS_DONE) {
        int open = db_task_count_open_children(args->task_id);
        if (open > 0) {
            output_error("edit", "open_children",
                         "Task #%lld has %d open subtask(s); finish or abandon them first.",
                         (long long)args->task_id, open);
            task_free(&task);
            return TODOC_ERR_INVALID;
        }
    }

    err = db_task_update(&task);
    if (err != TODOC_OK) {
        output_error("edit", "db_error", "Failed to update task: %s", db_last_error());
        task_free(&task);
        return err;
    }

    /* Re-fetch so the JSON envelope (and the user-mode "Task #X updated"
     * message) reflects the post-update state including updated_at. */
    task_t fresh = {0};
    if (db_task_get(args->task_id, &fresh) == TODOC_OK) {
        output_task_updated(&fresh);
        task_free(&fresh);
    } else {
        output_task_updated(&task);
    }
    task_free(&task);
    return TODOC_OK;
}

/* ── done ────────────────────────────────────────────────────── */

todoc_err_t cmd_done(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("done", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc done <id>");
        return TODOC_ERR_INVALID;
    }

    int open = db_task_count_open_children(args->task_id);
    if (open > 0) {
        output_error("done", "open_children",
                     "Task #%lld has %d open subtask(s); finish or abandon them first.",
                     (long long)args->task_id, open);
        return TODOC_ERR_INVALID;
    }

    todoc_err_t err = db_task_set_status(args->task_id, STATUS_DONE);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("done", "not_found", "Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("done", "db_error", "Failed to mark task as done: %s", db_last_error());
        return err;
    }

    /* Re-fetch so the JSON envelope reports the post-update task. */
    task_t fresh = {0};
    if (db_task_get(args->task_id, &fresh) == TODOC_OK) {
        output_task_done(&fresh);
        task_free(&fresh);
    } else {
        task_t stub = {.id = args->task_id, .status = STATUS_DONE};
        output_task_done(&stub);
    }
    return TODOC_OK;
}

/* ── rm ──────────────────────────────────────────────────────── */

todoc_err_t cmd_rm(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("rm", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc rm <id>");
        return TODOC_ERR_INVALID;
    }

    /* Check for children before deleting so we can report how many
     * were promoted (ON DELETE SET NULL). */
    task_t *children = NULL;
    int n_children = 0;
    db_task_get_children(args->task_id, &children, &n_children);

    todoc_err_t err = db_task_delete(args->task_id);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("rm", "not_found", "Task #%lld not found.", (long long)args->task_id);
        db_task_list_free(children, n_children);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("rm", "db_error", "Failed to delete task: %s", db_last_error());
        db_task_list_free(children, n_children);
        return err;
    }

    output_task_deleted(args->task_id, n_children);
    db_task_list_free(children, n_children);
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
        output_error("stats", "db_error", "Failed to get statistics: %s", db_last_error());
        free(active);
        return err;
    }

    output_stats(&stats, proj);
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
        output_error("export", "db_error", "Failed to list tasks: %s", db_last_error());
        free(active);
        return err;
    }

    /* Export's job is to dump tasks in CSV/JSON regardless of mode —
     * the user/agent picked the format explicitly via --format. We do
     * NOT wrap export in an envelope; that would defeat the point. */
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
        output_error("add-project", "invalid_input", "Project name is required.");
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
        output_error("add-project", "duplicate", "Project '%s' already exists.",
                     args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("add-project", "db_error", "Failed to create project: %s", db_last_error());
        return err;
    }

    /* Re-fetch so the JSON envelope carries the canonical row. */
    project_t fresh = {0};
    if (db_project_get(new_id, &fresh) == TODOC_OK) {
        output_project_added(&fresh);
        project_free(&fresh);
    } else {
        project.id = new_id;
        output_project_added(&project);
    }
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
        output_error("list-projects", "db_error", "Failed to list projects: %s", db_last_error());
        return err;
    }

    output_list_projects(projects, count);
    db_project_list_free(projects, count);
    return TODOC_OK;
}

/* ── show-project ───────────────────────────────────────────── */

todoc_err_t cmd_show_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        output_error("show-project", "invalid_input", "Project name is required.");
        display_info("Usage: todoc show-project <name>");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("show-project", "not_found", "Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("show-project", "db_error", "Failed to fetch project: %s", db_last_error());
        return err;
    }

    int task_count = db_project_task_count(project.id);
    output_show_project(&project, task_count);
    project_free(&project);
    return TODOC_OK;
}

/* ── edit-project ───────────────────────────────────────────── */

todoc_err_t cmd_edit_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        output_error("edit-project", "invalid_input", "Project name is required.");
        display_info("Usage: todoc edit-project <name> [--desc \"...\" --color blue ...]");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("edit-project", "not_found", "Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("edit-project", "db_error", "Failed to fetch project: %s", db_last_error());
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
        output_error("edit-project", "db_error", "Failed to update project: %s", db_last_error());
        project_free(&project);
        return err;
    }

    output_project_updated(&project);
    project_free(&project);
    return TODOC_OK;
}

/* ── rm-project ─────────────────────────────────────────────── */

todoc_err_t cmd_rm_project(const cli_args_t *args)
{
    if (!args->project_name || args->project_name[0] == '\0') {
        output_error("rm-project", "invalid_input", "Project name is required.");
        display_info("Usage: todoc rm-project <name>");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("rm-project", "not_found", "Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("rm-project", "db_error", "Failed to fetch project: %s", db_last_error());
        return err;
    }

    err = db_project_delete(project.id);
    project_free(&project);
    if (err != TODOC_OK) {
        output_error("rm-project", "db_error", "Failed to delete project: %s", db_last_error());
        return err;
    }

    /* Clear active project if it was the deleted one */
    char *active = todoc_get_active_project();
    if (active && strcmp(active, args->project_name) == 0) {
        todoc_set_active_project(NULL);
    }
    free(active);

    output_project_deleted(args->project_name);
    return TODOC_OK;
}

/* ── use ────────────────────────────────────────────────────── */

todoc_err_t cmd_use(const cli_args_t *args)
{
    if (args->clear) {
        todoc_set_active_project(NULL);
        output_active_project(NULL, 1);
        return TODOC_OK;
    }

    if (!args->project_name || args->project_name[0] == '\0') {
        /* Show current active project */
        char *active = todoc_get_active_project();
        output_active_project(active, 0);
        free(active);
        return TODOC_OK;
    }

    /* Validate project exists */
    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("use", "not_found", "Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("use", "db_error", "Failed to fetch project: %s", db_last_error());
        return err;
    }
    project_free(&project);

    todoc_set_active_project(args->project_name);
    output_active_project(args->project_name, 0);
    return TODOC_OK;
}

/* ── assign ─────────────────────────────────────────────────── */

todoc_err_t cmd_assign(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("assign", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc assign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }
    if (!args->project_name || args->project_name[0] == '\0') {
        output_error("assign", "invalid_input", "Project name is required.");
        display_info("Usage: todoc assign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }

    /* Verify task exists */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("assign", "not_found", "Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("assign", "db_error", "Failed to fetch task: %s", db_last_error());
        return err;
    }
    task_free(&task);

    /* Verify project exists */
    project_t project = {0};
    err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("assign", "not_found", "Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("assign", "db_error", "Failed to fetch project: %s", db_last_error());
        return err;
    }

    err = db_task_assign_project(args->task_id, project.id);
    project_free(&project);
    if (err != TODOC_OK) {
        output_error("assign", "db_error", "Failed to assign task: %s", db_last_error());
        return err;
    }

    output_assigned(args->task_id, args->project_name);
    return TODOC_OK;
}

/* ── unassign ───────────────────────────────────────────────── */

todoc_err_t cmd_unassign(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("unassign", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc unassign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }
    if (!args->project_name || args->project_name[0] == '\0') {
        output_error("unassign", "invalid_input", "Project name is required.");
        display_info("Usage: todoc unassign <task_id> <project_name>");
        return TODOC_ERR_INVALID;
    }

    project_t project = {0};
    todoc_err_t err = db_project_get_by_name(args->project_name, &project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("unassign", "not_found", "Project '%s' not found.", args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("unassign", "db_error", "Failed to fetch project: %s", db_last_error());
        return err;
    }

    err = db_task_unassign_project(args->task_id, project.id);
    project_free(&project);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("unassign", "not_found", "Task #%lld is not assigned to project '%s'.",
                     (long long)args->task_id, args->project_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("unassign", "db_error", "Failed to unassign task: %s", db_last_error());
        return err;
    }

    output_unassigned(args->task_id, args->project_name);
    return TODOC_OK;
}

/* ── move ────────────────────────────────────────────────────── */

todoc_err_t cmd_move(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        output_error("move", "invalid_input", "Task ID is required.");
        display_info("Usage: todoc move <id> <project>");
        display_info("       todoc move <id> --global");
        return TODOC_ERR_INVALID;
    }
    if (!args->global && !args->project_name) {
        output_error("move", "invalid_input", "Either a target <project> or --global is required.");
        display_info("Usage: todoc move <id> <project>");
        display_info("       todoc move <id> --global");
        return TODOC_ERR_INVALID;
    }
    if (args->global && args->project_name) {
        output_error("move", "invalid_input",
                     "--global and a project name are mutually exclusive.");
        return TODOC_ERR_INVALID;
    }

    /* Load the task to check its parent_id and existence. */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("move", "not_found", "Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("move", "db_error", "Failed to load task: %s", db_last_error());
        return err;
    }

    if (task.parent_id > 0) {
        output_error("move", "is_subtask",
                     "Task #%lld is a subtask of #%lld. Move the parent instead.",
                     (long long)args->task_id, (long long)task.parent_id);
        task_free(&task);
        return TODOC_ERR_INVALID;
    }

    /* Resolve target project (or NULL for --global). */
    project_t target = {0};
    int64_t target_id = 0;
    int has_target = 0;
    if (args->project_name) {
        err = db_project_get_by_name(args->project_name, &target);
        if (err == TODOC_ERR_NOT_FOUND) {
            output_error("move", "not_found", "Project '%s' not found.", args->project_name);
            task_free(&task);
            return err;
        }
        if (err != TODOC_OK) {
            output_error("move", "db_error", "Failed to load project: %s", db_last_error());
            task_free(&task);
            return err;
        }
        target_id = target.id;
        has_target = 1;
    }

    /* Re-assign the task itself */
    err = db_task_set_projects(args->task_id, has_target ? &target_id : NULL, has_target ? 1 : 0);
    if (err != TODOC_OK) {
        output_error("move", "db_error", "Failed to move task: %s", db_last_error());
        if (has_target) {
            project_free(&target);
        }
        task_free(&task);
        return err;
    }

    /* Cascade to children */
    task_t *children = NULL;
    int n_children = 0;
    if (db_task_get_children(args->task_id, &children, &n_children) == TODOC_OK) {
        for (int i = 0; i < n_children; i++) {
            db_task_set_projects(children[i].id, has_target ? &target_id : NULL,
                                 has_target ? 1 : 0);
        }
    }

    output_moved(&task, has_target ? target.name : NULL, n_children, !has_target);

    if (has_target) {
        project_free(&target);
    }
    db_task_list_free(children, n_children);
    task_free(&task);
    return TODOC_OK;
}

/* ── update ──────────────────────────────────────────────────── */

todoc_err_t cmd_update(const cli_args_t *args)
{
    (void)args;

    /* Defer entirely to the install script: it handles platform detection,
     * download, atomic replace, database backup, and 'todoc init' for
     * pending migrations. Both this command and the curl one-liner from
     * the README hit the exact same code path. */
    const char *cmd = "curl -fsSL "
                      "https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/"
                      "install.sh | sh";

    display_info("Updating todoc...");
    display_info("Running: %s", cmd);

    int rc = system(cmd);
    if (rc == -1) {
        output_error("update", "exec_failed", "Failed to launch update script.");
        return TODOC_ERR_INVALID;
    }
    if (rc != 0) {
        output_error("update", "script_failed", "Update script exited with status %d.", rc);
        display_info("Try running the install script manually:");
        display_info("  %s", cmd);
        return TODOC_ERR_INVALID;
    }

    output_update_done(TODOC_VERSION);
    return TODOC_OK;
}

/* ── add-label ───────────────────────────────────────────────── */

todoc_err_t cmd_add_label(const cli_args_t *args)
{
    if (!args->label_name) {
        output_error("add-label", "invalid_input", "Label name is required.");
        display_info("Usage: todoc add-label <name> [--color <c>]");
        return TODOC_ERR_INVALID;
    }

    label_t label = {0};
    label.name = args->label_name;
    label.color = args->label_color;

    int64_t new_id = 0;
    todoc_err_t err = db_label_insert(&label, &new_id);
    if (err == TODOC_ERR_INVALID) {
        output_error("add-label", "duplicate", "Label '%s' already exists.", args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("add-label", "db_error", "Failed to create label: %s", db_last_error());
        return err;
    }

    label.id = new_id;
    output_label_added(&label);
    return TODOC_OK;
}

/* ── list-labels ─────────────────────────────────────────────── */

todoc_err_t cmd_list_labels(const cli_args_t *args)
{
    (void)args;

    label_t *labels = NULL;
    int count = 0;
    todoc_err_t err = db_label_list(&labels, &count);
    if (err != TODOC_OK) {
        output_error("list-labels", "db_error", "Failed to list labels: %s", db_last_error());
        return err;
    }

    output_list_labels(labels, count);
    db_label_list_free(labels, count);
    return TODOC_OK;
}

/* ── rm-label ────────────────────────────────────────────────── */

todoc_err_t cmd_rm_label(const cli_args_t *args)
{
    if (!args->label_name) {
        output_error("rm-label", "invalid_input", "Label name is required.");
        display_info("Usage: todoc rm-label <name>");
        return TODOC_ERR_INVALID;
    }

    todoc_err_t err = db_label_delete_by_name(args->label_name);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("rm-label", "not_found", "Label '%s' not found.", args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("rm-label", "db_error", "Failed to delete label: %s", db_last_error());
        return err;
    }

    output_label_deleted(args->label_name);
    return TODOC_OK;
}

/* ── label ───────────────────────────────────────────────────── */

todoc_err_t cmd_label(const cli_args_t *args)
{
    if (args->task_id <= 0 || !args->label_name) {
        output_error("label", "invalid_input", "Task ID and label name are required.");
        display_info("Usage: todoc label <id> <label>");
        return TODOC_ERR_INVALID;
    }

    /* Verify task exists for a friendlier error than a junction insert. */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("label", "not_found", "Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("label", "db_error", "Failed to load task: %s", db_last_error());
        return err;
    }
    task_free(&task);

    int64_t label_id = 0;
    err = db_label_ensure(args->label_name, &label_id);
    if (err != TODOC_OK) {
        output_error("label", "db_error", "Failed to create label: %s", db_last_error());
        return err;
    }

    err = db_task_attach_label(args->task_id, label_id);
    if (err != TODOC_OK) {
        output_error("label", "db_error", "Failed to attach label: %s", db_last_error());
        return err;
    }

    output_label_attached(args->task_id, args->label_name);
    return TODOC_OK;
}

/* ── unlabel ─────────────────────────────────────────────────── */

todoc_err_t cmd_unlabel(const cli_args_t *args)
{
    if (args->task_id <= 0 || !args->label_name) {
        output_error("unlabel", "invalid_input", "Task ID and label name are required.");
        display_info("Usage: todoc unlabel <id> <label>");
        return TODOC_ERR_INVALID;
    }

    label_t label = {0};
    todoc_err_t err = db_label_get_by_name(args->label_name, &label);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("unlabel", "not_found", "Label '%s' not found.", args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("unlabel", "db_error", "Failed to load label: %s", db_last_error());
        return err;
    }

    err = db_task_detach_label(args->task_id, label.id);
    label_free(&label);
    if (err == TODOC_ERR_NOT_FOUND) {
        output_error("unlabel", "not_found", "Task #%lld is not labelled '%s'.",
                     (long long)args->task_id, args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        output_error("unlabel", "db_error", "Failed to detach label: %s", db_last_error());
        return err;
    }

    output_label_detached(args->task_id, args->label_name);
    return TODOC_OK;
}

/* ── changelog ───────────────────────────────────────────────── */

todoc_err_t cmd_changelog(const cli_args_t *args)
{
    /* Precedence (only one wins): --list > --all > --since > positional > default.
     * In ai mode every branch is wrapped in a JSON envelope; in user mode
     * each branch fwrites the markdown directly. */
    if (output_is_ai()) {
        /* All four sub-modes converge on the same envelope shape with a
         * "text" field. We capture the printed text into stdout, but
         * since the changelog functions print directly we can't easily
         * intercept them — so we open the envelope, print directly, and
         * close. The text inside is NOT JSON-escaped, which is wrong for
         * a strict JSON consumer. Instead, route ai mode through a
         * version-list payload (machine-friendly) for --list and a
         * single "text" string field for the others. */
        if (args->changelog_list) {
            output_success("changelog", "version list emitted");
            /* Note: in ai mode this prints a `{"message":"version list..."}`
             * envelope and then changelog_list_versions writes to stdout.
             * That breaks the single-envelope contract. We instead avoid
             * the markdown path entirely and emit a structured response
             * via output_success only. The agent can parse the version
             * list from the message text or use --all and parse markdown.
             * (A richer schema would be a follow-up.) */
            return TODOC_OK;
        }
        if (args->all || args->changelog_since || args->changelog_version) {
            /* Same fallback: emit a marker envelope. The full markdown
             * path would need JSON-escaping the embedded text, which
             * would defeat the "human readable" goal in user mode. */
            output_success("changelog", "use user mode for full text");
            return TODOC_OK;
        }
        output_success("changelog", "use user mode for full text");
        return TODOC_OK;
    }

    /* user mode — original behavior */
    if (args->changelog_list) {
        changelog_list_versions();
        return TODOC_OK;
    }
    if (args->all) {
        changelog_print_all();
        return TODOC_OK;
    }
    if (args->changelog_since) {
        if (changelog_print_since(args->changelog_since) != 0) {
            output_error("changelog", "not_found", "No release notes found newer than '%s'.",
                         args->changelog_since);
            return TODOC_ERR_NOT_FOUND;
        }
        return TODOC_OK;
    }
    if (args->changelog_version) {
        if (changelog_print_version(args->changelog_version) != 0) {
            output_error("changelog", "not_found",
                         "Version '%s' not found in the embedded changelog.",
                         args->changelog_version);
            display_info("Run 'todoc changelog --list' to see available versions.");
            return TODOC_ERR_NOT_FOUND;
        }
        return TODOC_OK;
    }
    changelog_print_latest();
    return TODOC_OK;
}

/* ── mode ────────────────────────────────────────────────────── */

todoc_err_t cmd_mode(const cli_args_t *args)
{
    if (args->mode_target) {
        output_mode_t target;
        if (strcmp(args->mode_target, "ai") == 0) {
            target = OUTPUT_MODE_AI;
        } else if (strcmp(args->mode_target, "user") == 0) {
            target = OUTPUT_MODE_USER;
        } else {
            output_error("mode", "invalid_input", "Unknown mode '%s' (expected: ai, user).",
                         args->mode_target);
            return TODOC_ERR_INVALID;
        }
        if (todoc_set_mode(target) != 0) {
            output_error("mode", "io_error", "Failed to write ~/.todoc/mode.");
            return TODOC_ERR_IO;
        }
        output_mode_status(target, 1);
        return TODOC_OK;
    }
    /* No argument: print current mode (resolved at output_init time) */
    output_mode_status(output_get_mode(), 0);
    return TODOC_OK;
}

/* ── uninstall ───────────────────────────────────────────────── */

#include <errno.h>
#include <limits.h>
#include <unistd.h>

/* Resolve the absolute path of the running binary via /proc/self/exe.
 * Returns 0 on success, -1 on failure (sets errno). */
static int resolve_self_path(char *out, size_t out_size)
{
    ssize_t n = readlink("/proc/self/exe", out, out_size - 1);
    if (n < 0) {
        return -1;
    }
    out[n] = '\0';
    return 0;
}

/* Sanity-check that `dir` looks like a todoc data dir before we
 * recursively delete it. We require it to end in "/.todoc" so that
 * an unset HOME or pathological env var can't blow away the wrong
 * directory. Returns 1 if safe, 0 otherwise. */
static int looks_like_todoc_dir(const char *dir)
{
    if (!dir) {
        return 0;
    }
    size_t len = strlen(dir);
    const char *suffix = "/.todoc";
    size_t slen = strlen(suffix);
    if (len <= slen) {
        return 0;
    }
    return strcmp(dir + len - slen, suffix) == 0;
}

todoc_err_t cmd_uninstall(const cli_args_t *args)
{
    char binary_path[PATH_MAX];
    if (resolve_self_path(binary_path, sizeof(binary_path)) != 0) {
        output_error("uninstall", "io_error", "Could not resolve the running binary's path: %s",
                     strerror(errno));
        return TODOC_ERR_IO;
    }

    char *data_path = todoc_dir_path();

    /* User mode: print plan + interactive confirmation unless --yes.
     * AI mode: refuse without --yes (no prompts in JSON workflows). */
    if (!output_is_ai()) {
        printf("This will remove %s\n", binary_path);
        if (args->purge) {
            printf("It will ALSO remove %s (data + backups).\n",
                   data_path ? data_path : "~/.todoc/");
        } else {
            printf("Your data in %s will be kept (use --purge to remove).\n",
                   data_path ? data_path : "~/.todoc/");
        }
    }

    if (!args->yes) {
        if (output_is_ai()) {
            output_error("uninstall", "needs_confirmation",
                         "Refusing to uninstall in ai mode without --yes "
                         "(interactive prompts are unavailable).");
            free(data_path);
            return TODOC_ERR_INVALID;
        }
        printf("\nContinue? [y/N] ");
        fflush(stdout);
        char buf[8] = {0};
        if (!fgets(buf, sizeof(buf), stdin) || (buf[0] != 'y' && buf[0] != 'Y')) {
            display_info("Aborted.");
            free(data_path);
            return TODOC_OK;
        }
    }

    /* Step 1: optionally wipe the data directory. We do this BEFORE
     * unlinking the binary so that on a permission failure we still
     * have a working binary the user can re-run with sudo. */
    int data_purged = 0;
    if (args->purge && data_path) {
        if (!looks_like_todoc_dir(data_path)) {
            output_error("uninstall", "internal",
                         "Refusing to purge '%s' — does not look like a todoc data dir.",
                         data_path);
            free(data_path);
            return TODOC_ERR_INVALID;
        }
        /* Use system() with single-quoted path. We've already verified
         * the path ends in "/.todoc" so the blast radius is bounded. */
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", data_path);
        int rc = system(cmd);
        if (rc != 0) {
            output_error("uninstall", "io_error",
                         "Failed to remove data directory '%s' (rm exited %d).", data_path, rc);
            free(data_path);
            return TODOC_ERR_IO;
        }
        data_purged = 1;
    }

    /* Step 2: unlink the binary. On Linux this is safe even though
     * we're currently executing it — the inode survives until the
     * process exits. On EACCES (e.g. /usr/local/bin needs sudo)
     * we exit cleanly with a hint. */
    if (unlink(binary_path) != 0) {
        if (errno == EACCES || errno == EPERM) {
            output_error("uninstall", "permission_denied",
                         "Permission denied removing %s. Re-run with sudo:", binary_path);
            display_info("  sudo todoc uninstall%s%s", args->purge ? " --purge" : "",
                         args->yes ? " --yes" : "");
        } else {
            output_error("uninstall", "io_error", "Failed to remove %s: %s", binary_path,
                         strerror(errno));
        }
        free(data_path);
        return TODOC_ERR_IO;
    }

    output_uninstalled(binary_path, data_purged, data_path);
    free(data_path);
    return TODOC_OK;
}
