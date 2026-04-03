#include "export.h"

#include <stdio.h>
#include <string.h>

/* ── Format enum ─────────────────────────────────────────────── */

static const char *format_strings[EXPORT_FORMAT_COUNT] = {
    [EXPORT_CSV] = "csv",
    [EXPORT_JSON] = "json",
};

const char *export_format_to_str(export_format_t f)
{
    if (f >= 0 && f < EXPORT_FORMAT_COUNT) {
        return format_strings[f];
    }
    return "unknown";
}

export_format_t str_to_export_format(const char *s)
{
    if (!s) {
        return (export_format_t)-1;
    }
    for (int i = 0; i < EXPORT_FORMAT_COUNT; i++) {
        if (strcmp(s, format_strings[i]) == 0) {
            return (export_format_t)i;
        }
    }
    return (export_format_t)-1;
}

/* ── CSV helpers ─────────────────────────────────────────────── */

/* Print a CSV field, quoting if it contains comma, quote, or newline */
static void csv_field(const char *s)
{
    if (!s) {
        return;
    }

    int needs_quote = 0;
    for (const char *p = s; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quote = 1;
            break;
        }
    }

    if (!needs_quote) {
        fputs(s, stdout);
        return;
    }

    fputc('"', stdout);
    for (const char *p = s; *p; p++) {
        if (*p == '"') {
            fputs("\"\"", stdout);
        } else {
            fputc(*p, stdout);
        }
    }
    fputc('"', stdout);
}

/* ── CSV export ──────────────────────────────────────────────── */

void export_tasks_csv(const task_t *tasks, int count)
{
    printf("id,title,description,type,priority,status,scope,due_date,created_at,updated_at\n");

    for (int i = 0; i < count; i++) {
        const task_t *t = &tasks[i];
        printf("%lld,", (long long)t->id);
        csv_field(t->title);
        putchar(',');
        csv_field(t->description);
        putchar(',');
        printf("%s,%s,%s,", task_type_to_str(t->type), priority_to_str(t->priority),
               status_to_str(t->status));
        csv_field(t->scope);
        putchar(',');
        csv_field(t->due_date);
        putchar(',');
        csv_field(t->created_at);
        putchar(',');
        csv_field(t->updated_at);
        putchar('\n');
    }
}

/* ── JSON helpers ────────────────────────────────────────────── */

/* Print a JSON string value, escaping special characters */
static void json_string(const char *s)
{
    if (!s) {
        printf("null");
        return;
    }

    fputc('"', stdout);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", stdout);
            break;
        case '\\':
            fputs("\\\\", stdout);
            break;
        case '\n':
            fputs("\\n", stdout);
            break;
        case '\r':
            fputs("\\r", stdout);
            break;
        case '\t':
            fputs("\\t", stdout);
            break;
        default:
            fputc(*p, stdout);
            break;
        }
    }
    fputc('"', stdout);
}

/* ── JSON export ─────────────────────────────────────────────── */

void export_tasks_json(const task_t *tasks, int count)
{
    printf("[\n");
    for (int i = 0; i < count; i++) {
        const task_t *t = &tasks[i];
        printf("  {\n");
        printf("    \"id\": %lld,\n", (long long)t->id);
        printf("    \"title\": ");
        json_string(t->title);
        printf(",\n");
        printf("    \"description\": ");
        json_string(t->description);
        printf(",\n");
        printf("    \"type\": \"%s\",\n", task_type_to_str(t->type));
        printf("    \"priority\": \"%s\",\n", priority_to_str(t->priority));
        printf("    \"status\": \"%s\",\n", status_to_str(t->status));
        printf("    \"scope\": ");
        json_string(t->scope);
        printf(",\n");
        printf("    \"due_date\": ");
        json_string(t->due_date);
        printf(",\n");
        printf("    \"created_at\": ");
        json_string(t->created_at);
        printf(",\n");
        printf("    \"updated_at\": ");
        json_string(t->updated_at);
        printf("\n");
        printf("  }%s\n", (i < count - 1) ? "," : "");
    }
    printf("]\n");
}
