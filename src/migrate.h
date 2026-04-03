#ifndef TODOC_MIGRATE_H
#define TODOC_MIGRATE_H

#include "model.h"

/* A single migration entry (embedded at compile time) */
typedef struct {
    int         version;
    const char *name;
    const char *sql;
} migration_t;

/* Populated by the generated migrations.c */
extern const migration_t migrations[];
extern const int          migrations_count;

/* Run all pending migrations. Creates the schema_migrations table if needed.
 * Returns TODOC_OK if all migrations applied (or already up-to-date) */
todoc_err_t migrate_run_all(void);

/* Get the current schema version (highest applied migration).
 * Returns 0 if no migrations have been applied yet */
int migrate_current_version(void);

#endif
