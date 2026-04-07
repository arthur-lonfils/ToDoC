#ifndef TODOC_DISPLAY_H
#define TODOC_DISPLAY_H

#include "model.h"

/* Detect if stdout is a TTY; set up color support */
void display_init(void);

/* ── Task output ─────────────────────────────────────────────── */

/* Compact one-line row for list view */
void display_task_row(const task_t *task);

/* Full detail view for show command */
void display_task_detail(const task_t *task);

/* Table header + rows for list command */
void display_task_list(const task_t *tasks, int count);

/* Statistics summary */
void display_stats(const task_stats_t *stats);

/* ── Project output ─────────────────────────────────────────── */

void display_project_row(const project_t *project);
void display_project_detail(const project_t *project, int task_count);
void display_project_list(const project_t *projects, int count);

/* ── Feedback messages ───────────────────────────────────────── */

void display_success(const char *fmt, ...);
void display_error(const char *fmt, ...);
void display_warn(const char *fmt, ...);
void display_info(const char *fmt, ...);

#endif
