#include "output.h"
#include "display.h"
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Mode state ──────────────────────────────────────────────── */

static output_mode_t g_mode = OUTPUT_MODE_USER;
static const char *g_mode_source = "default";

static void apply_mode(void)
{
    if (g_mode == OUTPUT_MODE_AI) {
        /* No colors in JSON output. The display.c functions are still
         * called by the user-mode branches of output_*; we use
         * display_disable_color so any stray colored printf is harmless. */
        display_disable_color();
    }
}

/* Markers that strongly indicate todoc is being driven by an AI agent
 * inside a tool-using session, not a human in a terminal. Each entry
 * is checked with getenv(); the first non-empty hit wins and is
 * recorded as the source string. The list is intentionally tight —
 * we only want signals that are unambiguous "I'm an agent", not
 * generic "I'm in VS Code". The TODOC_AGENT=1 entry is the documented
 * opt-in for any agent we don't auto-detect. */
static const char *const k_agent_env_vars[] = {
    "TODOC_AGENT",            /* explicit opt-in for unknown agents */
    "CLAUDECODE",             /* Claude Code CLI */
    "CLAUDE_CODE_ENTRYPOINT", /* Claude Code CLI */
    "CLAUDE_PROJECT_DIR",     /* Claude Code CLI */
    "CURSOR_TRACE_ID",        /* Cursor */
    NULL,
};

/* Returns a pointer to the matching env-var name, or NULL. */
static const char *detect_agent_env(void)
{
    for (const char *const *p = k_agent_env_vars; *p; p++) {
        const char *v = getenv(*p);
        if (v && v[0] != '\0') {
            return *p;
        }
    }
    return NULL;
}

/* If a stale ~/.todoc/mode file from a pre-auto-detect version still
 * exists, remove it silently. The mode is no longer persisted, but
 * leaving the file around would be confusing for anyone inspecting
 * ~/.todoc/. Errors are ignored — this is best-effort housekeeping. */
static void remove_legacy_mode_file(void)
{
    char *dir = todoc_dir_path();
    if (!dir) {
        return;
    }
    size_t len = strlen(dir) + strlen("/mode") + 1;
    char *path = malloc(len);
    if (path) {
        snprintf(path, len, "%s/mode", dir);
        (void)remove(path);
        free(path);
    }
    free(dir);
}

void output_init(int json_flag)
{
    /* Resolution order: --json > TODOC_MODE > agent-env auto-detect > default. */
    if (json_flag) {
        g_mode = OUTPUT_MODE_AI;
        g_mode_source = "json-flag";
        apply_mode();
        remove_legacy_mode_file();
        return;
    }
    const char *env = getenv("TODOC_MODE");
    if (env && env[0] != '\0') {
        if (strcmp(env, "ai") == 0) {
            g_mode = OUTPUT_MODE_AI;
            g_mode_source = "env-var";
            apply_mode();
            remove_legacy_mode_file();
            return;
        }
        if (strcmp(env, "user") == 0) {
            g_mode = OUTPUT_MODE_USER;
            g_mode_source = "env-var";
            apply_mode();
            remove_legacy_mode_file();
            return;
        }
        /* Unknown TODOC_MODE value: fall through to auto-detect. */
    }
    const char *agent = detect_agent_env();
    if (agent) {
        g_mode = OUTPUT_MODE_AI;
        /* Build a per-call source string. We only need to expose the
         * first matched var; reuse a small static buffer so the
         * pointer remains valid for the lifetime of the process. */
        static char buf[64];
        snprintf(buf, sizeof(buf), "auto-detect:%s", agent);
        g_mode_source = buf;
        apply_mode();
        remove_legacy_mode_file();
        return;
    }
    g_mode = OUTPUT_MODE_USER;
    g_mode_source = "default";
    apply_mode();
    remove_legacy_mode_file();
}

output_mode_t output_get_mode(void)
{
    return g_mode;
}

int output_is_ai(void)
{
    return g_mode == OUTPUT_MODE_AI;
}

const char *output_mode_source(void)
{
    return g_mode_source;
}

/* ── Envelope helpers (ai mode only) ────────────────────────── */

#define SCHEMA "todoc/v1"

static void env_open(FILE *fp, const char *command, int ok)
{
    fputc('{', fp);
    json_key(fp, "schema");
    json_str(fp, SCHEMA);
    fputc(',', fp);
    json_key(fp, "command");
    json_str(fp, command);
    fputc(',', fp);
    json_key(fp, "ok");
    json_bool(fp, ok);
    fputc(',', fp);
    json_key(fp, "data");
}

static void env_close(FILE *fp)
{
    fputc('}', fp);
    fputc('\n', fp);
}

/* ── Reusable JSON serializers for model types ──────────────── */

static void json_task(FILE *fp, const task_t *t)
{
    fputc('{', fp);
    int first = 1;
    json_comma(fp, &first);
    json_key(fp, "id");
    json_int64(fp, (long long)t->id);
    json_comma(fp, &first);
    json_key(fp, "title");
    json_str(fp, t->title);
    json_comma(fp, &first);
    json_key(fp, "description");
    json_str(fp, t->description);
    json_comma(fp, &first);
    json_key(fp, "type");
    json_str(fp, task_type_to_str(t->type));
    json_comma(fp, &first);
    json_key(fp, "priority");
    json_str(fp, priority_to_str(t->priority));
    json_comma(fp, &first);
    json_key(fp, "status");
    json_str(fp, status_to_str(t->status));
    json_comma(fp, &first);
    json_key(fp, "scope");
    json_str(fp, t->scope);
    json_comma(fp, &first);
    json_key(fp, "due_date");
    json_str(fp, t->due_date);
    json_comma(fp, &first);
    json_key(fp, "parent_id");
    if (t->parent_id > 0) {
        json_int64(fp, (long long)t->parent_id);
    } else {
        fputs("null", fp);
    }
    json_comma(fp, &first);
    json_key(fp, "created_at");
    json_str(fp, t->created_at);
    json_comma(fp, &first);
    json_key(fp, "updated_at");
    json_str(fp, t->updated_at);
    fputc('}', fp);
}

static void json_task_array(FILE *fp, const task_t *tasks, int count)
{
    fputc('[', fp);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', fp);
        }
        json_task(fp, &tasks[i]);
    }
    fputc(']', fp);
}

static void json_project(FILE *fp, const project_t *p)
{
    fputc('{', fp);
    int first = 1;
    json_comma(fp, &first);
    json_key(fp, "id");
    json_int64(fp, (long long)p->id);
    json_comma(fp, &first);
    json_key(fp, "name");
    json_str(fp, p->name);
    json_comma(fp, &first);
    json_key(fp, "description");
    json_str(fp, p->description);
    json_comma(fp, &first);
    json_key(fp, "color");
    json_str(fp, p->color);
    json_comma(fp, &first);
    json_key(fp, "status");
    json_str(fp, project_status_to_str(p->status));
    json_comma(fp, &first);
    json_key(fp, "due_date");
    json_str(fp, p->due_date);
    json_comma(fp, &first);
    json_key(fp, "created_at");
    json_str(fp, p->created_at);
    json_comma(fp, &first);
    json_key(fp, "updated_at");
    json_str(fp, p->updated_at);
    fputc('}', fp);
}

static void json_project_array(FILE *fp, const project_t *projects, int count)
{
    fputc('[', fp);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', fp);
        }
        json_project(fp, &projects[i]);
    }
    fputc(']', fp);
}

static void json_label(FILE *fp, const label_t *l)
{
    fputc('{', fp);
    int first = 1;
    json_comma(fp, &first);
    json_key(fp, "id");
    json_int64(fp, (long long)l->id);
    json_comma(fp, &first);
    json_key(fp, "name");
    json_str(fp, l->name);
    json_comma(fp, &first);
    json_key(fp, "color");
    json_str(fp, l->color);
    fputc('}', fp);
}

static void json_label_array(FILE *fp, const label_t *labels, int count)
{
    fputc('[', fp);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', fp);
        }
        json_label(fp, &labels[i]);
    }
    fputc(']', fp);
}

static void json_label_name_array(FILE *fp, const label_t *labels, int count)
{
    fputc('[', fp);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            fputc(',', fp);
        }
        json_str(fp, labels[i].name);
    }
    fputc(']', fp);
}

/* ── Tasks ──────────────────────────────────────────────────── */

void output_show_task(const task_t *t, const task_t *kids, int n_kids, const label_t *labels,
                      int n_labels)
{
    if (output_is_ai()) {
        env_open(stdout, "show", 1);
        fputc('{', stdout);
        json_key(stdout, "task");
        json_task(stdout, t);
        fputc(',', stdout);
        json_key(stdout, "subtasks");
        json_task_array(stdout, kids, n_kids);
        fputc(',', stdout);
        json_key(stdout, "labels");
        json_label_name_array(stdout, labels, n_labels);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_task_detail(t);
    if (n_labels > 0) {
        display_label_inline(labels, n_labels);
    }
    if (n_kids > 0) {
        display_subtask_list(kids, n_kids);
    }
}

void output_list_tasks(const task_t *tasks, int count, const char *project_scope)
{
    if (output_is_ai()) {
        env_open(stdout, "list", 1);
        fputc('{', stdout);
        json_key(stdout, "tasks");
        json_task_array(stdout, tasks, count);
        fputc(',', stdout);
        json_key(stdout, "count");
        json_int(stdout, count);
        fputc(',', stdout);
        json_key(stdout, "project");
        json_str(stdout, project_scope);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    if (project_scope) {
        display_info("Showing tasks for project '%s' (use --all to show all)", project_scope);
    }
    display_task_list(tasks, count);
}

void output_task_added(const task_t *t, const char *project_assigned, int n_labels)
{
    if (output_is_ai()) {
        env_open(stdout, "add", 1);
        fputc('{', stdout);
        json_key(stdout, "task");
        json_task(stdout, t);
        fputc(',', stdout);
        json_key(stdout, "project_assigned");
        json_str(stdout, project_assigned);
        fputc(',', stdout);
        json_key(stdout, "labels_attached");
        json_int(stdout, n_labels);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    if (project_assigned && n_labels > 0) {
        display_success("Task #%lld created in project '%s' with %d label(s).", (long long)t->id,
                        project_assigned, n_labels);
    } else if (project_assigned) {
        display_success("Task #%lld created and assigned to project '%s'.", (long long)t->id,
                        project_assigned);
    } else if (n_labels > 0) {
        display_success("Task #%lld created with %d label(s).", (long long)t->id, n_labels);
    } else {
        display_success("Task #%lld created.", (long long)t->id);
    }
}

void output_task_updated(const task_t *t)
{
    if (output_is_ai()) {
        env_open(stdout, "edit", 1);
        fputc('{', stdout);
        json_key(stdout, "task");
        json_task(stdout, t);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld updated.", (long long)t->id);
}

void output_task_done(const task_t *t)
{
    if (output_is_ai()) {
        env_open(stdout, "done", 1);
        fputc('{', stdout);
        json_key(stdout, "task");
        json_task(stdout, t);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld marked as done.", (long long)t->id);
}

void output_task_deleted(int64_t id, int promoted_kids)
{
    if (output_is_ai()) {
        env_open(stdout, "rm", 1);
        fputc('{', stdout);
        json_key(stdout, "task_id");
        json_int64(stdout, (long long)id);
        fputc(',', stdout);
        json_key(stdout, "promoted_subtasks");
        json_int(stdout, promoted_kids);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld deleted.", (long long)id);
    if (promoted_kids > 0) {
        display_info("%d subtask(s) promoted to top-level.", promoted_kids);
    }
}

/* ── Projects ───────────────────────────────────────────────── */

void output_show_project(const project_t *p, int task_count)
{
    if (output_is_ai()) {
        env_open(stdout, "show-project", 1);
        fputc('{', stdout);
        json_key(stdout, "project");
        json_project(stdout, p);
        fputc(',', stdout);
        json_key(stdout, "task_count");
        json_int(stdout, task_count);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_project_detail(p, task_count);
}

void output_list_projects(const project_t *projects, int count)
{
    if (output_is_ai()) {
        env_open(stdout, "list-projects", 1);
        fputc('{', stdout);
        json_key(stdout, "projects");
        json_project_array(stdout, projects, count);
        fputc(',', stdout);
        json_key(stdout, "count");
        json_int(stdout, count);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_project_list(projects, count);
}

void output_project_added(const project_t *p)
{
    if (output_is_ai()) {
        env_open(stdout, "add-project", 1);
        fputc('{', stdout);
        json_key(stdout, "project");
        json_project(stdout, p);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Project '%s' created.", p->name);
}

void output_project_updated(const project_t *p)
{
    if (output_is_ai()) {
        env_open(stdout, "edit-project", 1);
        fputc('{', stdout);
        json_key(stdout, "project");
        json_project(stdout, p);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Project '%s' updated.", p->name);
}

void output_project_deleted(const char *name)
{
    if (output_is_ai()) {
        env_open(stdout, "rm-project", 1);
        fputc('{', stdout);
        json_key(stdout, "name");
        json_str(stdout, name);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Project '%s' deleted.", name);
}

/* ── Labels ─────────────────────────────────────────────────── */

void output_list_labels(const label_t *labels, int count)
{
    if (output_is_ai()) {
        env_open(stdout, "list-labels", 1);
        fputc('{', stdout);
        json_key(stdout, "labels");
        json_label_array(stdout, labels, count);
        fputc(',', stdout);
        json_key(stdout, "count");
        json_int(stdout, count);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_label_list(labels, count);
}

void output_label_added(const label_t *l)
{
    if (output_is_ai()) {
        env_open(stdout, "add-label", 1);
        fputc('{', stdout);
        json_key(stdout, "label");
        json_label(stdout, l);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Label '%s' created.", l->name);
}

void output_label_attached(int64_t task_id, const char *label)
{
    if (output_is_ai()) {
        env_open(stdout, "label", 1);
        fputc('{', stdout);
        json_key(stdout, "task_id");
        json_int64(stdout, (long long)task_id);
        fputc(',', stdout);
        json_key(stdout, "label");
        json_str(stdout, label);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld labelled '%s'.", (long long)task_id, label);
}

void output_label_detached(int64_t task_id, const char *label)
{
    if (output_is_ai()) {
        env_open(stdout, "unlabel", 1);
        fputc('{', stdout);
        json_key(stdout, "task_id");
        json_int64(stdout, (long long)task_id);
        fputc(',', stdout);
        json_key(stdout, "label");
        json_str(stdout, label);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld unlabelled '%s'.", (long long)task_id, label);
}

void output_label_deleted(const char *name)
{
    if (output_is_ai()) {
        env_open(stdout, "rm-label", 1);
        fputc('{', stdout);
        json_key(stdout, "name");
        json_str(stdout, name);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Label '%s' deleted.", name);
}

/* ── Associations ───────────────────────────────────────────── */

void output_assigned(int64_t task_id, const char *project)
{
    if (output_is_ai()) {
        env_open(stdout, "assign", 1);
        fputc('{', stdout);
        json_key(stdout, "task_id");
        json_int64(stdout, (long long)task_id);
        fputc(',', stdout);
        json_key(stdout, "project");
        json_str(stdout, project);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld assigned to project '%s'.", (long long)task_id, project);
}

void output_unassigned(int64_t task_id, const char *project)
{
    if (output_is_ai()) {
        env_open(stdout, "unassign", 1);
        fputc('{', stdout);
        json_key(stdout, "task_id");
        json_int64(stdout, (long long)task_id);
        fputc(',', stdout);
        json_key(stdout, "project");
        json_str(stdout, project);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Task #%lld removed from project '%s'.", (long long)task_id, project);
}

void output_moved(const task_t *t, const char *target, int n_kids, int to_global)
{
    if (output_is_ai()) {
        env_open(stdout, "move", 1);
        fputc('{', stdout);
        json_key(stdout, "task_id");
        json_int64(stdout, (long long)t->id);
        fputc(',', stdout);
        json_key(stdout, "target");
        if (to_global) {
            fputs("null", stdout);
        } else {
            json_str(stdout, target);
        }
        fputc(',', stdout);
        json_key(stdout, "moved_subtasks");
        json_int(stdout, n_kids);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    if (to_global) {
        if (n_kids > 0) {
            display_success("Task #%lld and %d subtask(s) removed from all projects.",
                            (long long)t->id, n_kids);
        } else {
            display_success("Task #%lld removed from all projects.", (long long)t->id);
        }
    } else {
        if (n_kids > 0) {
            display_success("Task #%lld and %d subtask(s) moved to project '%s'.", (long long)t->id,
                            n_kids, target);
        } else {
            display_success("Task #%lld moved to project '%s'.", (long long)t->id, target);
        }
    }
}

/* ── Misc ───────────────────────────────────────────────────── */

void output_active_project(const char *name, int cleared)
{
    if (output_is_ai()) {
        env_open(stdout, "use", 1);
        fputc('{', stdout);
        json_key(stdout, "active_project");
        if (cleared || !name) {
            fputs("null", stdout);
        } else {
            json_str(stdout, name);
        }
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    if (cleared) {
        display_success("Active project cleared.");
    } else if (name) {
        display_success("Active project set to '%s'.", name);
    } else {
        display_info("No active project.");
    }
}

void output_stats(const task_stats_t *s, const char *project_scope)
{
    if (output_is_ai()) {
        env_open(stdout, "stats", 1);
        fputc('{', stdout);
        json_key(stdout, "project");
        json_str(stdout, project_scope);
        fputc(',', stdout);
        json_key(stdout, "total");
        json_int(stdout, s->total);
        fputc(',', stdout);
        json_key(stdout, "overdue");
        json_int(stdout, s->overdue);
        fputc(',', stdout);
        json_key(stdout, "by_status");
        fputc('{', stdout);
        for (int i = 0; i < STATUS_COUNT; i++) {
            if (i > 0) {
                fputc(',', stdout);
            }
            json_key(stdout, status_to_str((status_t)i));
            json_int(stdout, s->by_status[i]);
        }
        fputc('}', stdout);
        fputc(',', stdout);
        json_key(stdout, "by_priority");
        fputc('{', stdout);
        for (int i = 0; i < PRIORITY_COUNT; i++) {
            if (i > 0) {
                fputc(',', stdout);
            }
            json_key(stdout, priority_to_str((priority_t)i));
            json_int(stdout, s->by_priority[i]);
        }
        fputc('}', stdout);
        fputc(',', stdout);
        json_key(stdout, "by_type");
        fputc('{', stdout);
        for (int i = 0; i < TASK_TYPE_COUNT; i++) {
            if (i > 0) {
                fputc(',', stdout);
            }
            json_key(stdout, task_type_to_str((task_type_t)i));
            json_int(stdout, s->by_type[i]);
        }
        fputc('}', stdout);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    if (project_scope) {
        display_info("Statistics for project '%s'", project_scope);
    }
    display_stats(s);
}

void output_init_db(const char *db_path, int already_existed)
{
    if (output_is_ai()) {
        env_open(stdout, "init", 1);
        fputc('{', stdout);
        json_key(stdout, "db_path");
        json_str(stdout, db_path);
        fputc(',', stdout);
        json_key(stdout, "already_existed");
        json_bool(stdout, already_existed);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("Database initialized at %s", db_path ? db_path : "~/.todoc/todoc.db");
}

void output_mode_status(output_mode_t mode, const char *source)
{
    /* 'mode' is a read-only diagnostic. It reports the resolved mode
     * for this process and the source that won the resolution race
     * (json-flag, env-var, auto-detect:<var>, or default). */
    if (output_is_ai()) {
        env_open(stdout, "mode", 1);
        fputc('{', stdout);
        json_key(stdout, "mode");
        json_str(stdout, mode == OUTPUT_MODE_AI ? "ai" : "user");
        fputc(',', stdout);
        json_key(stdout, "source");
        json_str(stdout, source ? source : "default");
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    printf("%s (source: %s)\n", mode == OUTPUT_MODE_AI ? "ai" : "user",
           source ? source : "default");
}

void output_update_done(const char *current_version)
{
    if (output_is_ai()) {
        env_open(stdout, "update", 1);
        fputc('{', stdout);
        json_key(stdout, "version");
        json_str(stdout, current_version);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("todoc updated.");
    display_info("Run 'todoc changelog' to see what's new.");
}

void output_uninstalled(const char *binary_path, int data_purged, const char *data_path)
{
    if (output_is_ai()) {
        env_open(stdout, "uninstall", 1);
        fputc('{', stdout);
        json_key(stdout, "binary_path");
        json_str(stdout, binary_path);
        fputc(',', stdout);
        json_key(stdout, "data_purged");
        json_bool(stdout, data_purged);
        fputc(',', stdout);
        json_key(stdout, "data_path");
        json_str(stdout, data_path);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    display_success("todoc removed from %s", binary_path);
    if (data_purged) {
        display_info("Also removed %s (data + backups)", data_path);
    } else {
        display_info("Your data in %s was kept.", data_path ? data_path : "~/.todoc/");
        display_info("Pass --purge next time to remove it too.");
    }
}

/* ── Generic success / error ────────────────────────────────── */

void output_success(const char *command, const char *fmt, ...)
{
    if (output_is_ai()) {
        env_open(stdout, command, 1);
        fputc('{', stdout);
        json_key(stdout, "message");
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        json_str(stdout, buf);
        fputc('}', stdout);
        env_close(stdout);
        return;
    }
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    display_success("%s", buf);
}

void output_error(const char *command, const char *code, const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (output_is_ai()) {
        fputc('{', stderr);
        json_key(stderr, "schema");
        json_str(stderr, SCHEMA);
        fputc(',', stderr);
        json_key(stderr, "command");
        json_str(stderr, command ? command : "");
        fputc(',', stderr);
        json_key(stderr, "ok");
        json_bool(stderr, 0);
        fputc(',', stderr);
        json_key(stderr, "error");
        fputc('{', stderr);
        json_key(stderr, "code");
        json_str(stderr, code ? code : "error");
        fputc(',', stderr);
        json_key(stderr, "message");
        json_str(stderr, buf);
        fputc('}', stderr);
        fputc('}', stderr);
        fputc('\n', stderr);
        return;
    }
    display_error("%s", buf);
}
