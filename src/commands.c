#include "commands.h"
#include "changelog.h"
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
        display_error("Invalid parent task id.");
        return -1;
    }
    task_t parent = {0};
    todoc_err_t err = db_task_get(parent_id, &parent);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Parent task #%lld not found.", (long long)parent_id);
        *out_err = err;
        return -1;
    }
    if (err != TODOC_OK) {
        display_error("Failed to load parent task: %s", db_last_error());
        *out_err = err;
        return -1;
    }
    if (parent.parent_id > 0) {
        display_error("Task #%lld is itself a subtask. Only one level of nesting is allowed.",
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
        display_error("Task title is required.");
        display_info("Usage: todoc add \"title\" [--type bug --priority high ...]");
        return TODOC_ERR_INVALID;
    }

    /* If --sub <parent> was given, validate it before inserting. */
    if (args->parent_set) {
        if (args->parent_id <= 0) {
            display_error("--sub requires a parent task id.");
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
        display_error("Failed to add task: %s", db_last_error());
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

    if (assigned_project && n_labels > 0) {
        display_success("Task #%lld created in project '%s' with %d label(s).", (long long)new_id,
                        proj, n_labels);
    } else if (assigned_project) {
        display_success("Task #%lld created and assigned to project '%s'.", (long long)new_id,
                        proj);
    } else if (n_labels > 0) {
        display_success("Task #%lld created with %d label(s).", (long long)new_id, n_labels);
    } else {
        display_success("Task #%lld created.", (long long)new_id);
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

    /* Show labels inline */
    label_t *labels = NULL;
    int n_labels = 0;
    if (db_task_get_labels(args->task_id, &labels, &n_labels) == TODOC_OK && n_labels > 0) {
        display_label_inline(labels, n_labels);
    }
    db_label_list_free(labels, n_labels);

    /* Show children if any */
    task_t *children = NULL;
    int n_children = 0;
    if (db_task_get_children(args->task_id, &children, &n_children) == TODOC_OK && n_children > 0) {
        display_subtask_list(children, n_children);
    }
    db_task_list_free(children, n_children);

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
    if (args->parent_set) {
        if (args->parent_id < 0) {
            /* --sub none → promote to top-level */
            task.parent_id = 0;
        } else {
            if (args->parent_id == args->task_id) {
                display_error("A task cannot be its own parent.");
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
                        display_error("Task #%lld has %d child task(s); it cannot itself become a "
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
            display_error("Task #%lld has %d open subtask(s); finish or abandon them first.",
                          (long long)args->task_id, open);
            task_free(&task);
            return TODOC_ERR_INVALID;
        }
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

    int open = db_task_count_open_children(args->task_id);
    if (open > 0) {
        display_error("Task #%lld has %d open subtask(s); finish or abandon them first.",
                      (long long)args->task_id, open);
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

    /* Check for children before deleting so we can warn the user that
     * they will be promoted to top-level tasks (ON DELETE SET NULL). */
    task_t *children = NULL;
    int n_children = 0;
    db_task_get_children(args->task_id, &children, &n_children);

    todoc_err_t err = db_task_delete(args->task_id);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        db_task_list_free(children, n_children);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to delete task: %s", db_last_error());
        db_task_list_free(children, n_children);
        return err;
    }

    display_success("Task #%lld deleted.", (long long)args->task_id);
    if (n_children > 0) {
        display_info("%d subtask(s) promoted to top-level.", n_children);
    }
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

/* ── move ────────────────────────────────────────────────────── */

todoc_err_t cmd_move(const cli_args_t *args)
{
    if (args->task_id <= 0) {
        display_error("Task ID is required.");
        display_info("Usage: todoc move <id> <project>");
        display_info("       todoc move <id> --global");
        return TODOC_ERR_INVALID;
    }
    if (!args->global && !args->project_name) {
        display_error("Either a target <project> or --global is required.");
        display_info("Usage: todoc move <id> <project>");
        display_info("       todoc move <id> --global");
        return TODOC_ERR_INVALID;
    }
    if (args->global && args->project_name) {
        display_error("--global and a project name are mutually exclusive.");
        return TODOC_ERR_INVALID;
    }

    /* Load the task to check its parent_id and existence. */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to load task: %s", db_last_error());
        return err;
    }

    if (task.parent_id > 0) {
        display_error("Task #%lld is a subtask of #%lld. Move the parent instead.",
                      (long long)args->task_id, (long long)task.parent_id);
        task_free(&task);
        return TODOC_ERR_INVALID;
    }
    task_free(&task);

    /* Resolve target project (or NULL for --global). */
    project_t target = {0};
    int64_t target_id = 0;
    int has_target = 0;
    if (args->project_name) {
        err = db_project_get_by_name(args->project_name, &target);
        if (err == TODOC_ERR_NOT_FOUND) {
            display_error("Project '%s' not found.", args->project_name);
            return err;
        }
        if (err != TODOC_OK) {
            display_error("Failed to load project: %s", db_last_error());
            return err;
        }
        target_id = target.id;
        has_target = 1;
    }

    /* Re-assign the task itself */
    err = db_task_set_projects(args->task_id, has_target ? &target_id : NULL, has_target ? 1 : 0);
    if (err != TODOC_OK) {
        display_error("Failed to move task: %s", db_last_error());
        if (has_target) {
            project_free(&target);
        }
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

    if (has_target) {
        if (n_children > 0) {
            display_success("Task #%lld and %d subtask(s) moved to project '%s'.",
                            (long long)args->task_id, n_children, target.name);
        } else {
            display_success("Task #%lld moved to project '%s'.", (long long)args->task_id,
                            target.name);
        }
        project_free(&target);
    } else {
        if (n_children > 0) {
            display_success("Task #%lld and %d subtask(s) removed from all projects.",
                            (long long)args->task_id, n_children);
        } else {
            display_success("Task #%lld removed from all projects.", (long long)args->task_id);
        }
    }

    db_task_list_free(children, n_children);
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
        display_error("Failed to launch update script.");
        return TODOC_ERR_INVALID;
    }
    if (rc != 0) {
        display_error("Update script exited with status %d.", rc);
        display_info("Try running the install script manually:");
        display_info("  %s", cmd);
        return TODOC_ERR_INVALID;
    }

    display_success("todoc updated.");
    display_info("Run 'todoc changelog' to see what's new.");
    return TODOC_OK;
}

/* ── add-label ───────────────────────────────────────────────── */

todoc_err_t cmd_add_label(const cli_args_t *args)
{
    if (!args->label_name) {
        display_error("Label name is required.");
        display_info("Usage: todoc add-label <name> [--color <c>]");
        return TODOC_ERR_INVALID;
    }

    label_t label = {0};
    label.name = args->label_name;
    label.color = args->label_color;

    int64_t new_id = 0;
    todoc_err_t err = db_label_insert(&label, &new_id);
    if (err == TODOC_ERR_INVALID) {
        display_error("Label '%s' already exists.", args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to create label: %s", db_last_error());
        return err;
    }

    display_success("Label '%s' created.", args->label_name);
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
        display_error("Failed to list labels: %s", db_last_error());
        return err;
    }

    display_label_list(labels, count);
    db_label_list_free(labels, count);
    return TODOC_OK;
}

/* ── rm-label ────────────────────────────────────────────────── */

todoc_err_t cmd_rm_label(const cli_args_t *args)
{
    if (!args->label_name) {
        display_error("Label name is required.");
        display_info("Usage: todoc rm-label <name>");
        return TODOC_ERR_INVALID;
    }

    todoc_err_t err = db_label_delete_by_name(args->label_name);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Label '%s' not found.", args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to delete label: %s", db_last_error());
        return err;
    }

    display_success("Label '%s' deleted.", args->label_name);
    return TODOC_OK;
}

/* ── label ───────────────────────────────────────────────────── */

todoc_err_t cmd_label(const cli_args_t *args)
{
    if (args->task_id <= 0 || !args->label_name) {
        display_error("Task ID and label name are required.");
        display_info("Usage: todoc label <id> <label>");
        return TODOC_ERR_INVALID;
    }

    /* Verify task exists for a friendlier error than a junction insert. */
    task_t task = {0};
    todoc_err_t err = db_task_get(args->task_id, &task);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld not found.", (long long)args->task_id);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to load task: %s", db_last_error());
        return err;
    }
    task_free(&task);

    int64_t label_id = 0;
    err = db_label_ensure(args->label_name, &label_id);
    if (err != TODOC_OK) {
        display_error("Failed to create label: %s", db_last_error());
        return err;
    }

    err = db_task_attach_label(args->task_id, label_id);
    if (err != TODOC_OK) {
        display_error("Failed to attach label: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld labelled '%s'.", (long long)args->task_id, args->label_name);
    return TODOC_OK;
}

/* ── unlabel ─────────────────────────────────────────────────── */

todoc_err_t cmd_unlabel(const cli_args_t *args)
{
    if (args->task_id <= 0 || !args->label_name) {
        display_error("Task ID and label name are required.");
        display_info("Usage: todoc unlabel <id> <label>");
        return TODOC_ERR_INVALID;
    }

    label_t label = {0};
    todoc_err_t err = db_label_get_by_name(args->label_name, &label);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Label '%s' not found.", args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to load label: %s", db_last_error());
        return err;
    }

    err = db_task_detach_label(args->task_id, label.id);
    label_free(&label);
    if (err == TODOC_ERR_NOT_FOUND) {
        display_error("Task #%lld is not labelled '%s'.", (long long)args->task_id,
                      args->label_name);
        return err;
    }
    if (err != TODOC_OK) {
        display_error("Failed to detach label: %s", db_last_error());
        return err;
    }

    display_success("Task #%lld unlabelled '%s'.", (long long)args->task_id, args->label_name);
    return TODOC_OK;
}

/* ── changelog ───────────────────────────────────────────────── */

todoc_err_t cmd_changelog(const cli_args_t *args)
{
    /* Precedence (only one wins): --list > --all > --since > positional > default. */
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
            display_error("No release notes found newer than '%s'.", args->changelog_since);
            return TODOC_ERR_NOT_FOUND;
        }
        return TODOC_OK;
    }
    if (args->changelog_version) {
        if (changelog_print_version(args->changelog_version) != 0) {
            display_error("Version '%s' not found in the embedded changelog.",
                          args->changelog_version);
            display_info("Run 'todoc changelog --list' to see available versions.");
            return TODOC_ERR_NOT_FOUND;
        }
        return TODOC_OK;
    }
    changelog_print_latest();
    return TODOC_OK;
}
