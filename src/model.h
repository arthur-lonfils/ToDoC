#ifndef TODOC_MODEL_H
#define TODOC_MODEL_H

#include <stdint.h>

/* ── Error codes ─────────────────────────────────────────────── */

typedef enum {
    TODOC_OK = 0,
    TODOC_ERR_DB,
    TODOC_ERR_NOT_FOUND,
    TODOC_ERR_IO,
    TODOC_ERR_INVALID,
} todoc_err_t;

/* ── Task type ───────────────────────────────────────────────── */

typedef enum {
    TASK_TYPE_BUG = 0,
    TASK_TYPE_FEATURE,
    TASK_TYPE_CHORE,
    TASK_TYPE_IDEA,
    TASK_TYPE_COUNT
} task_type_t;

/* ── Priority ────────────────────────────────────────────────── */

typedef enum {
    PRIORITY_CRITICAL = 0,
    PRIORITY_HIGH,
    PRIORITY_MEDIUM,
    PRIORITY_LOW,
    PRIORITY_COUNT
} priority_t;

/* ── Status ──────────────────────────────────────────────────── */

typedef enum {
    STATUS_TODO = 0,
    STATUS_IN_PROGRESS,
    STATUS_DONE,
    STATUS_BLOCKED,
    STATUS_CANCELLED,
    STATUS_ABANDONED,
    STATUS_COUNT
} status_t;

/* A status is "terminal" if no more work is expected on it. Used to
 * decide whether a parent task can be marked done. */
int status_is_terminal(status_t s);

/* ── Project status ─────────────────────────────────────────── */

typedef enum {
    PROJECT_ACTIVE = 0,
    PROJECT_COMPLETED,
    PROJECT_ARCHIVED,
    PROJECT_STATUS_COUNT
} project_status_t;

/* ── Task struct ─────────────────────────────────────────────── */

typedef struct {
    int64_t id;
    char *title;       /* heap-allocated, required */
    char *description; /* heap-allocated, nullable */
    task_type_t type;
    priority_t priority;
    status_t status;
    char *scope;       /* heap-allocated, nullable */
    char *due_date;    /* heap-allocated "YYYY-MM-DD", nullable */
    char *created_at;  /* heap-allocated ISO-8601, set by DB */
    char *updated_at;  /* heap-allocated ISO-8601, set by DB */
    int64_t parent_id; /* 0 = no parent (top-level task) */
} task_t;

/* ── Project struct ─────────────────────────────────────────── */

typedef struct {
    int64_t id;
    char *name;        /* heap-allocated, required, unique */
    char *description; /* heap-allocated, nullable */
    char *color;       /* heap-allocated, nullable */
    project_status_t status;
    char *due_date;   /* heap-allocated "YYYY-MM-DD", nullable */
    char *created_at; /* heap-allocated ISO-8601, set by DB */
    char *updated_at; /* heap-allocated ISO-8601, set by DB */
} project_t;

/* ── Filter for task list queries ──────────────────────────────── */

typedef struct {
    status_t *status; /* NULL = no filter */
    priority_t *priority;
    task_type_t *type;
    char *scope;
    char *project; /* project name filter, NULL = no filter */
    int all;       /* 1 = show all, bypass active project */
    int limit;     /* 0 = no limit */
} task_filter_t;

/* ── Filter for project list queries ───────────────────────────── */

typedef struct {
    project_status_t *status; /* NULL = no filter */
    int limit;                /* 0 = no limit */
} project_filter_t;

/* ── Statistics ──────────────────────────────────────────────── */

typedef struct {
    int total;
    int by_status[STATUS_COUNT];
    int by_priority[PRIORITY_COUNT];
    int by_type[TASK_TYPE_COUNT];
    int overdue;
} task_stats_t;

/* ── Lifecycle ───────────────────────────────────────────────── */

void task_free(task_t *task);
void task_filter_free(task_filter_t *filter);
void project_free(project_t *project);
void project_filter_free(project_filter_t *filter);

/* ── Enum <-> string conversion ──────────────────────────────── */

const char *task_type_to_str(task_type_t t);
task_type_t str_to_task_type(const char *s); /* returns -1 on failure */

const char *priority_to_str(priority_t p);
priority_t str_to_priority(const char *s); /* returns -1 on failure */

const char *status_to_str(status_t s);
status_t str_to_status(const char *s); /* returns -1 on failure */

const char *project_status_to_str(project_status_t s);
project_status_t str_to_project_status(const char *s); /* returns -1 on failure */

#endif
