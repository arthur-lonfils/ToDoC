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

/* Safe allocation wrappers — exit(1) on OOM */
void *todoc_malloc(size_t size);
void *todoc_calloc(size_t count, size_t size);
void *todoc_realloc(void *ptr, size_t size);

#endif
