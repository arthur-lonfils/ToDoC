#ifndef TODOC_OUTPUT_H
#define TODOC_OUTPUT_H

#include "model.h"
#include "util.h"

#include <stdint.h>

/* The output module is the single gateway for everything todoc shows
 * to the user (or to the agent). Each handler in commands.c calls
 * exactly one output_* function per "exit point" — and the output
 * module dispatches based on the current mode:
 *
 *   USER mode → calls into display.c, prints colored human output
 *   AI   mode → emits a single JSON envelope on stdout (or stderr
 *               for errors)
 *
 * The mode is resolved once per process at output_init() time. The
 * resolution order is, highest precedence first:
 *
 *   1. --json flag (passed to output_init)
 *   2. TODOC_MODE env var (ai|user)
 *   3. ~/.todoc/mode file (ai|user)
 *   4. default OUTPUT_MODE_USER
 */

void output_init(int json_flag);
output_mode_t output_get_mode(void);
int output_is_ai(void);

/* Tasks */
void output_show_task(const task_t *t, const task_t *kids, int n_kids, const label_t *labels,
                      int n_labels);
void output_list_tasks(const task_t *tasks, int count, const char *project_scope);
void output_task_added(const task_t *t, const char *project_assigned, int n_labels);
void output_task_updated(const task_t *t);
void output_task_done(const task_t *t);
void output_task_deleted(int64_t id, int promoted_kids);

/* Projects */
void output_show_project(const project_t *p, int task_count);
void output_list_projects(const project_t *projects, int count);
void output_project_added(const project_t *p);
void output_project_updated(const project_t *p);
void output_project_deleted(const char *name);

/* Labels */
void output_list_labels(const label_t *labels, int count);
void output_label_added(const label_t *l);
void output_label_attached(int64_t task_id, const char *label);
void output_label_detached(int64_t task_id, const char *label);
void output_label_deleted(const char *name);

/* Project / label associations */
void output_assigned(int64_t task_id, const char *project);
void output_unassigned(int64_t task_id, const char *project);
void output_moved(const task_t *t, const char *target, int n_kids, int to_global);

/* Misc */
void output_active_project(const char *name, int cleared);
void output_stats(const task_stats_t *s, const char *project_scope);
void output_init_db(const char *db_path, int already_existed);
void output_mode_status(output_mode_t mode, int just_set);
void output_update_done(const char *current_version);

/* Generic info/success message — used by handlers that need to print
 * a one-line confirmation that doesn't carry structured data. In ai
 * mode this becomes `{"ok":true,"data":{"message":"..."}}`. */
void output_success(const char *command, const char *fmt, ...);

/* Generic error path. In user mode prints to stderr via display_error.
 * In ai mode prints a JSON error envelope to stderr. */
void output_error(const char *command, const char *code, const char *fmt, ...);

#endif
