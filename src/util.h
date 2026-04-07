#ifndef TODOC_UTIL_H
#define TODOC_UTIL_H

#include <stddef.h>

/* Safe string duplication (NULL-safe: returns NULL for NULL input) */
char *todoc_strdup(const char *s);

/* Resolve ~/.todoc/ directory path; returns heap-allocated string */
char *todoc_dir_path(void);

/* Resolve ~/.todoc/todoc.db path; returns heap-allocated string */
char *todoc_db_path(void);

/* Ensure directory exists (creates if missing). Returns 0 on success, -1 on error */
int todoc_ensure_dir(const char *path);

/* Validate date string format YYYY-MM-DD with range checks. Returns 1 if valid, 0 if not */
int todoc_validate_date(const char *s);

/* Active project context — stored in ~/.todoc/active_project */
char *todoc_active_project_path(void);
char *todoc_get_active_project(void);           /* returns heap-allocated name or NULL */
int todoc_set_active_project(const char *name); /* NULL to clear; returns 0 on success */

/* Output mode — controls whether todoc emits human-formatted output
 * (USER) or structured JSON for LLM agents (AI). Persisted in
 * ~/.todoc/mode. The TODOC_MODE env var and the --json flag both
 * override the persisted value at process scope. */
typedef enum {
    OUTPUT_MODE_USER = 0,
    OUTPUT_MODE_AI,
} output_mode_t;

char *todoc_mode_path(void);            /* "~/.todoc/mode" */
output_mode_t todoc_get_mode(void);     /* file → enum, defaults to USER */
int todoc_set_mode(output_mode_t mode); /* writes "ai" or "user" */

/* Detect the user's shell from $SHELL. Returns one of "bash", "zsh",
 * "fish", or NULL if $SHELL is unset or unrecognised. The returned
 * pointer is a literal — do not free it. */
const char *todoc_detect_shell(void);

/* Safe allocation wrappers — exit(1) on OOM */
void *todoc_malloc(size_t size);
void *todoc_calloc(size_t count, size_t size);
void *todoc_realloc(void *ptr, size_t size);

#endif
