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

/* ── Feedback messages ───────────────────────────────────────── */

void display_success(const char *fmt, ...);
void display_error(const char *fmt, ...);
void display_warn(const char *fmt, ...);
void display_info(const char *fmt, ...);

#endif
