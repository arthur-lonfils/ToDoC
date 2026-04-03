#include "migrate.h"
#include "db.h"
#include "display.h"

#include <sqlite3.h>
#include <string.h>

/* ── Schema migrations table ─────────────────────────────────── */

static todoc_err_t ensure_migrations_table(void)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version    INTEGER PRIMARY KEY,"
        "  name       TEXT NOT NULL,"
        "  applied_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now','localtime'))"
        ");";

    sqlite3 *db = db_get_handle();
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        display_error("Failed to create migrations table: %s", err_msg);
        sqlite3_free(err_msg);
        return TODOC_ERR_DB;
    }
    return TODOC_OK;
}

/* ── Current version ─────────────────────────────────────────── */

int migrate_current_version(void)
{
    sqlite3 *db = db_get_handle();
    if (!db) {
        return 0;
    }

    const char *sql = "SELECT MAX(version) FROM schema_migrations;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            version = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return version;
}

/* ── Check if a specific version has been applied ────────────── */

static int is_version_applied(int version)
{
    sqlite3 *db = db_get_handle();
    const char *sql = "SELECT 1 FROM schema_migrations WHERE version = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_int(stmt, 1, version);
    int applied = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return applied;
}

/* ── Record a migration as applied ───────────────────────────── */

static todoc_err_t record_migration(int version, const char *name)
{
    sqlite3 *db = db_get_handle();
    const char *sql = "INSERT INTO schema_migrations (version, name) VALUES (?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return TODOC_ERR_DB;
    }

    sqlite3_bind_int(stmt, 1, version);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? TODOC_OK : TODOC_ERR_DB;
}

/* ── Run all pending migrations ──────────────────────────────── */

todoc_err_t migrate_run_all(void)
{
    todoc_err_t err = ensure_migrations_table();
    if (err != TODOC_OK) {
        return err;
    }

    sqlite3 *db = db_get_handle();
    int applied = 0;

    for (int i = 0; i < migrations_count; i++) {
        const migration_t *m = &migrations[i];

        if (is_version_applied(m->version)) {
            continue;
        }

        /* Run the migration inside a transaction */
        char *err_msg = NULL;
        int rc = sqlite3_exec(db, "BEGIN;", NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            display_error("Failed to begin transaction: %s", err_msg);
            sqlite3_free(err_msg);
            return TODOC_ERR_DB;
        }

        rc = sqlite3_exec(db, m->sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            display_error("Migration %03d (%s) failed: %s", m->version, m->name, err_msg);
            sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return TODOC_ERR_DB;
        }

        err = record_migration(m->version, m->name);
        if (err != TODOC_OK) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            display_error("Failed to record migration %03d.", m->version);
            return err;
        }

        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        display_info("Applied migration %03d: %s", m->version, m->name);
        applied++;
    }

    if (applied == 0) {
        display_info("Database schema is up to date (v%d).", migrate_current_version());
    } else {
        display_success("Applied %d migration(s). Schema now at v%d.", applied,
                        migrate_current_version());
    }

    return TODOC_OK;
}
