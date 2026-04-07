#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

char *todoc_strdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    char *dup = strdup(s);
    if (!dup) {
        fprintf(stderr, "todoc: out of memory\n");
        exit(1);
    }
    return dup;
}

char *todoc_dir_path(void)
{
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "todoc: HOME environment variable not set\n");
        return NULL;
    }

    size_t len = strlen(home) + strlen("/.todoc") + 1;
    char *path = todoc_malloc(len);
    snprintf(path, len, "%s/.todoc", home);
    return path;
}

char *todoc_db_path(void)
{
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "todoc: HOME environment variable not set\n");
        return NULL;
    }

    size_t len = strlen(home) + strlen("/.todoc/todoc.db") + 1;
    char *path = todoc_malloc(len);
    snprintf(path, len, "%s/.todoc/todoc.db", home);
    return path;
}

int todoc_ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "todoc: '%s' exists but is not a directory\n", path);
        return -1;
    }

    if (mkdir(path, 0755) != 0) {
        fprintf(stderr, "todoc: cannot create directory '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int todoc_validate_date(const char *s)
{
    if (!s) {
        return 0;
    }

    /* Check format: YYYY-MM-DD (exactly 10 chars) */
    if (strlen(s) != 10) {
        return 0;
    }
    if (s[4] != '-' || s[7] != '-') {
        return 0;
    }

    /* Check all other chars are digits */
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
    }

    /* Parse and validate ranges */
    struct tm tm_val = {0};
    int year = atoi(s);
    int month = atoi(s + 5);
    int day = atoi(s + 8);

    if (year < 1970 || year > 2100) {
        return 0;
    }
    if (month < 1 || month > 12) {
        return 0;
    }
    if (day < 1 || day > 31) {
        return 0;
    }

    /* Use mktime to validate the actual date (e.g., Feb 30 is invalid) */
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = month - 1;
    tm_val.tm_mday = day;
    tm_val.tm_isdst = -1;

    struct tm check = tm_val;
    if (mktime(&check) == (time_t)-1) {
        return 0;
    }

    /* mktime normalizes invalid dates (e.g., Jan 32 -> Feb 1) */
    if (check.tm_year != tm_val.tm_year || check.tm_mon != tm_val.tm_mon ||
        check.tm_mday != tm_val.tm_mday) {
        return 0;
    }

    return 1;
}

/* ── Active project context ─────────────────────────────────── */

char *todoc_active_project_path(void)
{
    const char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "todoc: HOME environment variable not set\n");
        return NULL;
    }

    size_t len = strlen(home) + strlen("/.todoc/active_project") + 1;
    char *path = todoc_malloc(len);
    snprintf(path, len, "%s/.todoc/active_project", home);
    return path;
}

char *todoc_get_active_project(void)
{
    char *path = todoc_active_project_path();
    if (!path) {
        return NULL;
    }

    FILE *fp = fopen(path, "r");
    free(path);
    if (!fp) {
        return NULL;
    }

    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    if (buf[0] == '\0') {
        return NULL;
    }

    return todoc_strdup(buf);
}

int todoc_set_active_project(const char *name)
{
    char *path = todoc_active_project_path();
    if (!path) {
        return -1;
    }

    if (!name) {
        /* Clear: remove the file */
        remove(path);
        free(path);
        return 0;
    }

    char *dir = todoc_dir_path();
    if (!dir) {
        free(path);
        return -1;
    }
    if (todoc_ensure_dir(dir) != 0) {
        free(dir);
        free(path);
        return -1;
    }
    free(dir);

    FILE *fp = fopen(path, "w");
    free(path);
    if (!fp) {
        return -1;
    }

    fprintf(fp, "%s\n", name);
    fclose(fp);
    return 0;
}

/* ── Shell detection ────────────────────────────────────────── */

const char *todoc_detect_shell(void)
{
    const char *shell = getenv("SHELL");
    if (!shell || !*shell) {
        return NULL;
    }
    /* Take the basename: /usr/bin/zsh → zsh */
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;

    if (strcmp(base, "bash") == 0) {
        return "bash";
    }
    if (strcmp(base, "zsh") == 0) {
        return "zsh";
    }
    if (strcmp(base, "fish") == 0) {
        return "fish";
    }
    return NULL;
}

/* ── Safe allocation wrappers ──────────────────────────────── */

void *todoc_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "todoc: out of memory\n");
        exit(1);
    }
    return ptr;
}

void *todoc_calloc(size_t count, size_t size)
{
    void *ptr = calloc(count, size);
    if (!ptr && count > 0 && size > 0) {
        fprintf(stderr, "todoc: out of memory\n");
        exit(1);
    }
    return ptr;
}

void *todoc_realloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "todoc: out of memory\n");
        exit(1);
    }
    return new_ptr;
}
