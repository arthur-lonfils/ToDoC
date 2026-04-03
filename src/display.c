#include "display.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── ANSI color codes ────────────────────────────────────────── */

#define CLR_RESET    "\033[0m"
#define CLR_BOLD     "\033[1m"
#define CLR_DIM      "\033[2m"
#define CLR_RED      "\033[31m"
#define CLR_GREEN    "\033[32m"
#define CLR_YELLOW   "\033[33m"
#define CLR_BLUE     "\033[34m"
#define CLR_MAGENTA  "\033[35m"
#define CLR_CYAN     "\033[36m"
#define CLR_WHITE    "\033[37m"
#define CLR_BOLD_RED "\033[1;31m"

static int g_use_color = 0;

/* ── Helpers ─────────────────────────────────────────────────── */

void display_init(void)
{
    /* Respect NO_COLOR convention (https://no-color.org/) */
    if (getenv("NO_COLOR")) {
        g_use_color = 0;
        return;
    }
    g_use_color = isatty(STDOUT_FILENO);
}

static const char *clr(const char *code)
{
    return g_use_color ? code : "";
}

static const char *priority_color(priority_t p)
{
    switch (p) {
    case PRIORITY_CRITICAL:
        return CLR_BOLD_RED;
    case PRIORITY_HIGH:
        return CLR_RED;
    case PRIORITY_MEDIUM:
        return CLR_YELLOW;
    case PRIORITY_LOW:
        return CLR_DIM;
    default:
        return "";
    }
}

static const char *status_color(status_t s)
{
    switch (s) {
    case STATUS_TODO:
        return CLR_WHITE;
    case STATUS_IN_PROGRESS:
        return CLR_CYAN;
    case STATUS_DONE:
        return CLR_GREEN;
    case STATUS_BLOCKED:
        return CLR_RED;
    case STATUS_CANCELLED:
        return CLR_DIM;
    default:
        return "";
    }
}

static const char *type_color(task_type_t t)
{
    switch (t) {
    case TASK_TYPE_BUG:
        return CLR_RED;
    case TASK_TYPE_FEATURE:
        return CLR_BLUE;
    case TASK_TYPE_CHORE:
        return CLR_WHITE;
    case TASK_TYPE_IDEA:
        return CLR_MAGENTA;
    default:
        return "";
    }
}

/* ── Task row (compact) ─────────────────────────────────────── */

void display_task_row(const task_t *task)
{
    const char *rst = clr(CLR_RESET);

    /* ID */
    printf("  %s#%-4lld%s ", clr(CLR_DIM), (long long)task->id, rst);

    /* Status */
    printf("%s%-12s%s ", clr(status_color(task->status)), status_to_str(task->status), rst);

    /* Priority */
    printf("%s%-9s%s ", clr(priority_color(task->priority)), priority_to_str(task->priority), rst);

    /* Type */
    printf("%s%-8s%s ", clr(type_color(task->type)), task_type_to_str(task->type), rst);

    /* Scope */
    if (task->scope) {
        printf("%s[%s]%s ", clr(CLR_CYAN), task->scope, rst);
    }

    /* Due date */
    if (task->due_date) {
        printf("%s(%s)%s ", clr(CLR_YELLOW), task->due_date, rst);
    }

    /* Title */
    if (task->status == STATUS_DONE) {
        printf("%s%s%s", clr(CLR_DIM), task->title, rst);
    } else {
        printf("%s", task->title);
    }

    printf("\n");
}

/* ── Task detail ─────────────────────────────────────────────── */

void display_task_detail(const task_t *task)
{
    const char *rst = clr(CLR_RESET);

    printf("\n");
    printf("  %s%sTask #%lld%s\n", clr(CLR_BOLD), clr(CLR_WHITE), (long long)task->id, rst);
    printf("  %s%s%s\n\n", clr(CLR_BOLD), task->title, rst);

    if (task->description) {
        printf("  %s%s%s\n\n", clr(CLR_DIM), task->description, rst);
    }

    printf("  %-12s %s%s%s\n", "Status:", clr(status_color(task->status)),
           status_to_str(task->status), rst);
    printf("  %-12s %s%s%s\n", "Priority:", clr(priority_color(task->priority)),
           priority_to_str(task->priority), rst);
    printf("  %-12s %s%s%s\n", "Type:", clr(type_color(task->type)), task_type_to_str(task->type),
           rst);

    if (task->scope) {
        printf("  %-12s %s%s%s\n", "Scope:", clr(CLR_CYAN), task->scope, rst);
    }
    if (task->due_date) {
        printf("  %-12s %s%s%s\n", "Due:", clr(CLR_YELLOW), task->due_date, rst);
    }

    printf("\n");
    printf("  %sCreated:%s  %s\n", clr(CLR_DIM), rst, task->created_at ? task->created_at : "-");
    printf("  %sUpdated:%s  %s\n", clr(CLR_DIM), rst, task->updated_at ? task->updated_at : "-");
    printf("\n");
}

/* ── Task list ───────────────────────────────────────────────── */

void display_task_list(const task_t *tasks, int count)
{
    if (count == 0) {
        display_info("No tasks found.");
        return;
    }

    const char *rst = clr(CLR_RESET);

    printf("\n  %s%s%-6s %-12s %-9s %-8s %s%s\n", clr(CLR_BOLD), clr(CLR_DIM), "ID", "STATUS",
           "PRIORITY", "TYPE", "TITLE", rst);
    printf("  %s%s%s\n", clr(CLR_DIM),
           "─────────────────────────────────────────────────────────────", rst);

    for (int i = 0; i < count; i++) {
        display_task_row(&tasks[i]);
    }

    printf("\n  %s%d task(s)%s\n\n", clr(CLR_DIM), count, rst);
}

/* ── Statistics ──────────────────────────────────────────────── */

void display_stats(const task_stats_t *stats)
{
    const char *rst = clr(CLR_RESET);

    printf("\n  %s%sTask Statistics%s\n", clr(CLR_BOLD), clr(CLR_WHITE), rst);
    printf("  %s══════════════════════════════%s\n\n", clr(CLR_DIM), rst);

    printf("  Total tasks: %s%s%d%s\n", clr(CLR_BOLD), clr(CLR_WHITE), stats->total, rst);

    if (stats->overdue > 0) {
        printf("  Overdue:     %s%d%s\n", clr(CLR_BOLD_RED), stats->overdue, rst);
    }

    printf("\n  %sBy Status:%s\n", clr(CLR_BOLD), rst);
    for (int i = 0; i < STATUS_COUNT; i++) {
        if (stats->by_status[i] > 0) {
            printf("    %s%-12s%s %d\n", clr(status_color((status_t)i)), status_to_str((status_t)i),
                   rst, stats->by_status[i]);
        }
    }

    printf("\n  %sBy Priority:%s\n", clr(CLR_BOLD), rst);
    for (int i = 0; i < PRIORITY_COUNT; i++) {
        if (stats->by_priority[i] > 0) {
            printf("    %s%-12s%s %d\n", clr(priority_color((priority_t)i)),
                   priority_to_str((priority_t)i), rst, stats->by_priority[i]);
        }
    }

    printf("\n  %sBy Type:%s\n", clr(CLR_BOLD), rst);
    for (int i = 0; i < TASK_TYPE_COUNT; i++) {
        if (stats->by_type[i] > 0) {
            printf("    %s%-12s%s %d\n", clr(type_color((task_type_t)i)),
                   task_type_to_str((task_type_t)i), rst, stats->by_type[i]);
        }
    }

    printf("\n");
}

/* ── Feedback messages ───────────────────────────────────────── */

void display_success(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "%s✓%s ", clr(CLR_GREEN), clr(CLR_RESET));
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

void display_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%s✗%s ", clr(CLR_RED), clr(CLR_RESET));
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void display_warn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "%s⚠%s ", clr(CLR_YELLOW), clr(CLR_RESET));
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

void display_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "%s·%s ", clr(CLR_BLUE), clr(CLR_RESET));
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}
