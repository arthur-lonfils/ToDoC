#include "model.h"

#include <stdlib.h>
#include <string.h>

/* ── String lookup tables ────────────────────────────────────── */

static const char *type_strings[TASK_TYPE_COUNT] = {
    [TASK_TYPE_BUG] = "bug",
    [TASK_TYPE_FEATURE] = "feature",
    [TASK_TYPE_CHORE] = "chore",
    [TASK_TYPE_IDEA] = "idea",
};

static const char *priority_strings[PRIORITY_COUNT] = {
    [PRIORITY_CRITICAL] = "critical",
    [PRIORITY_HIGH] = "high",
    [PRIORITY_MEDIUM] = "medium",
    [PRIORITY_LOW] = "low",
};

static const char *status_strings[STATUS_COUNT] = {
    [STATUS_TODO] = "todo",       [STATUS_IN_PROGRESS] = "in-progress", [STATUS_DONE] = "done",
    [STATUS_BLOCKED] = "blocked", [STATUS_CANCELLED] = "cancelled",
};

/* ── Lifecycle ───────────────────────────────────────────────── */

void task_free(task_t *task)
{
    if (!task) {
        return;
    }
    free(task->title);
    free(task->description);
    free(task->scope);
    free(task->due_date);
    free(task->created_at);
    free(task->updated_at);

    task->title = NULL;
    task->description = NULL;
    task->scope = NULL;
    task->due_date = NULL;
    task->created_at = NULL;
    task->updated_at = NULL;
}

void task_filter_free(task_filter_t *filter)
{
    if (!filter) {
        return;
    }
    free(filter->status);
    free(filter->priority);
    free(filter->type);
    free(filter->scope);

    filter->status = NULL;
    filter->priority = NULL;
    filter->type = NULL;
    filter->scope = NULL;
}

/* ── Enum -> string ──────────────────────────────────────────── */

const char *task_type_to_str(task_type_t t)
{
    if (t >= 0 && t < TASK_TYPE_COUNT) {
        return type_strings[t];
    }
    return "unknown";
}

const char *priority_to_str(priority_t p)
{
    if (p >= 0 && p < PRIORITY_COUNT) {
        return priority_strings[p];
    }
    return "unknown";
}

const char *status_to_str(status_t s)
{
    if (s >= 0 && s < STATUS_COUNT) {
        return status_strings[s];
    }
    return "unknown";
}

/* ── String -> enum ──────────────────────────────────────────── */

task_type_t str_to_task_type(const char *s)
{
    if (!s) {
        return (task_type_t)-1;
    }
    for (int i = 0; i < TASK_TYPE_COUNT; i++) {
        if (strcmp(s, type_strings[i]) == 0) {
            return (task_type_t)i;
        }
    }
    return (task_type_t)-1;
}

priority_t str_to_priority(const char *s)
{
    if (!s) {
        return (priority_t)-1;
    }
    for (int i = 0; i < PRIORITY_COUNT; i++) {
        if (strcmp(s, priority_strings[i]) == 0) {
            return (priority_t)i;
        }
    }
    return (priority_t)-1;
}

status_t str_to_status(const char *s)
{
    if (!s) {
        return (status_t)-1;
    }
    for (int i = 0; i < STATUS_COUNT; i++) {
        if (strcmp(s, status_strings[i]) == 0) {
            return (status_t)i;
        }
    }
    return (status_t)-1;
}
