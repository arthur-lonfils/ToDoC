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
    STATUS_COUNT
} status_t;

/* ── Task struct ─────────────────────────────────────────────── */

typedef struct {
    int64_t     id;
    char       *title;          /* heap-allocated, required */
    char       *description;    /* heap-allocated, nullable */
    task_type_t type;
    priority_t  priority;
    status_t    status;
    char       *scope;          /* heap-allocated, nullable */
    char       *due_date;       /* heap-allocated "YYYY-MM-DD", nullable */
    char       *created_at;     /* heap-allocated ISO-8601, set by DB */
    char       *updated_at;     /* heap-allocated ISO-8601, set by DB */
} task_t;

/* ── Filter for list queries ─────────────────────────────────── */

typedef struct {
    status_t    *status;        /* NULL = no filter */
    priority_t  *priority;
    task_type_t *type;
    char        *scope;
    int          limit;         /* 0 = no limit */
} task_filter_t;

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

/* ── Enum <-> string conversion ──────────────────────────────── */

const char  *task_type_to_str(task_type_t t);
task_type_t  str_to_task_type(const char *s);   /* returns -1 on failure */

const char  *priority_to_str(priority_t p);
priority_t   str_to_priority(const char *s);    /* returns -1 on failure */

const char  *status_to_str(status_t s);
status_t     str_to_status(const char *s);      /* returns -1 on failure */

#endif
