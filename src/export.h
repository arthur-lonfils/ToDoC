#ifndef TODOC_EXPORT_H
#define TODOC_EXPORT_H

#include "model.h"

typedef enum { EXPORT_CSV = 0, EXPORT_JSON, EXPORT_FORMAT_COUNT } export_format_t;

const char *export_format_to_str(export_format_t f);
export_format_t str_to_export_format(const char *s); /* returns -1 on failure */

/* Write tasks to stdout in the given format. No ANSI colors. */
void export_tasks_csv(const task_t *tasks, int count);
void export_tasks_json(const task_t *tasks, int count);

#endif
