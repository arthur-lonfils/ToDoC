#include "cli.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Version ─────────────────────────────────────────────────── */

#define TODOC_VERSION "0.2.0"

/* ── Subcommand lookup ───────────────────────────────────────── */

typedef struct {
    const char *name;
    command_t cmd;
} cmd_entry_t;

static const cmd_entry_t cmd_table[] = {
    {"init", CMD_INIT},         {"add", CMD_ADD},
    {"list", CMD_LIST},         {"ls", CMD_LIST},
    {"show", CMD_SHOW},         {"edit", CMD_EDIT},
    {"done", CMD_DONE},         {"rm", CMD_RM},
    {"remove", CMD_RM},         {"delete", CMD_RM},
    {"stats", CMD_STATS},       {"export", CMD_EXPORT},
    {"help", CMD_HELP},         {"--help", CMD_HELP},
    {"-h", CMD_HELP},           {"version", CMD_VERSION},
    {"--version", CMD_VERSION}, {"-v", CMD_VERSION},
    {NULL, CMD_NONE},
};

static command_t lookup_command(const char *name)
{
    for (const cmd_entry_t *e = cmd_table; e->name; e++) {
        if (strcmp(name, e->name) == 0) {
            return e->cmd;
        }
    }
    return CMD_NONE;
}

/* ── Flag helpers ────────────────────────────────────────────── */

/* Check if arg starts with "--" or "-" */
static int is_flag(const char *arg)
{
    return arg[0] == '-';
}

/* Consume the next argument as a value for a flag.
 * Returns NULL and prints error if missing */
static const char *consume_value(int argc, char **argv, int *i, const char *flag)
{
    if (*i + 1 >= argc || is_flag(argv[*i + 1])) {
        fprintf(stderr, "todoc: option '%s' requires a value\n", flag);
        return NULL;
    }
    (*i)++;
    return argv[*i];
}

/* Allocate and set an enum pointer (for optional filter fields) */
static task_type_t *alloc_type(task_type_t val)
{
    task_type_t *p = todoc_malloc(sizeof(*p));
    *p = val;
    return p;
}

static priority_t *alloc_priority(priority_t val)
{
    priority_t *p = todoc_malloc(sizeof(*p));
    *p = val;
    return p;
}

static status_t *alloc_status(status_t val)
{
    status_t *p = todoc_malloc(sizeof(*p));
    *p = val;
    return p;
}

/* ── Parse ID argument ───────────────────────────────────────── */

static int parse_task_id(const char *arg, int64_t *out)
{
    /* Strip leading '#' if present */
    if (arg[0] == '#') {
        arg++;
    }

    char *end = NULL;
    long long val = strtoll(arg, &end, 10);
    if (*end != '\0' || val <= 0) {
        fprintf(stderr, "todoc: invalid task ID '%s'\n", arg);
        return -1;
    }
    *out = (int64_t)val;
    return 0;
}

/* ── Parse flags common to add/edit/list ─────────────────────── */

static todoc_err_t parse_flags(int argc, char **argv, int start, cli_args_t *out)
{
    for (int i = start; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--type") == 0 || strcmp(arg, "-t") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            task_type_t t = str_to_task_type(val);
            if ((int)t == -1) {
                fprintf(stderr, "todoc: invalid type '%s' (expected: bug, feature, chore, idea)\n",
                        val);
                return TODOC_ERR_INVALID;
            }
            free(out->type);
            out->type = alloc_type(t);
            /* Also set filter for list */
            free(out->filter.type);
            out->filter.type = alloc_type(t);
        } else if (strcmp(arg, "--priority") == 0 || strcmp(arg, "-p") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            priority_t p = str_to_priority(val);
            if ((int)p == -1) {
                fprintf(stderr,
                        "todoc: invalid priority '%s' (expected: critical, high, medium, low)\n",
                        val);
                return TODOC_ERR_INVALID;
            }
            free(out->priority);
            out->priority = alloc_priority(p);
            free(out->filter.priority);
            out->filter.priority = alloc_priority(p);
        } else if (strcmp(arg, "--status") == 0 || strcmp(arg, "-s") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            status_t s = str_to_status(val);
            if ((int)s == -1) {
                fprintf(stderr,
                        "todoc: invalid status '%s' (expected: todo, in-progress, done, blocked, "
                        "cancelled)\n",
                        val);
                return TODOC_ERR_INVALID;
            }
            free(out->status);
            out->status = alloc_status(s);
            free(out->filter.status);
            out->filter.status = alloc_status(s);
        } else if (strcmp(arg, "--scope") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            free(out->scope);
            out->scope = todoc_strdup(val);
            free(out->filter.scope);
            out->filter.scope = todoc_strdup(val);
        } else if (strcmp(arg, "--due") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            if (!todoc_validate_date(val)) {
                fprintf(stderr, "todoc: invalid date '%s' (expected: YYYY-MM-DD)\n", val);
                return TODOC_ERR_INVALID;
            }
            free(out->due_date);
            out->due_date = todoc_strdup(val);
        } else if (strcmp(arg, "--desc") == 0 || strcmp(arg, "--description") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            free(out->description);
            out->description = todoc_strdup(val);
        } else if (strcmp(arg, "--title") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            free(out->title);
            out->title = todoc_strdup(val);
        } else if (strcmp(arg, "--format") == 0 || strcmp(arg, "-f") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            export_format_t fmt = str_to_export_format(val);
            if ((int)fmt == -1) {
                fprintf(stderr, "todoc: invalid format '%s' (expected: csv, json)\n", val);
                return TODOC_ERR_INVALID;
            }
            out->export_format = fmt;
        } else if (strcmp(arg, "--limit") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            out->filter.limit = atoi(val);
            if (out->filter.limit <= 0) {
                fprintf(stderr, "todoc: invalid limit '%s'\n", val);
                return TODOC_ERR_INVALID;
            }
        } else if (!is_flag(arg)) {
            /* Positional argument handling depends on command */
            if (out->command == CMD_ADD && !out->title) {
                out->title = todoc_strdup(arg);
            } else if ((out->command == CMD_SHOW || out->command == CMD_EDIT ||
                        out->command == CMD_DONE || out->command == CMD_RM) &&
                       out->task_id == 0) {
                if (parse_task_id(arg, &out->task_id) != 0) {
                    return TODOC_ERR_INVALID;
                }
            } else {
                fprintf(stderr, "todoc: unexpected argument '%s'\n", arg);
                return TODOC_ERR_INVALID;
            }
        } else {
            fprintf(stderr, "todoc: unknown option '%s'\n", arg);
            return TODOC_ERR_INVALID;
        }
    }
    return TODOC_OK;
}

/* ── Main parse entry ────────────────────────────────────────── */

todoc_err_t cli_parse(int argc, char **argv, cli_args_t *out)
{
    memset(out, 0, sizeof(*out));

    if (argc < 2) {
        out->command = CMD_HELP;
        return TODOC_OK;
    }

    out->command = lookup_command(argv[1]);
    if (out->command == CMD_NONE) {
        fprintf(stderr, "todoc: unknown command '%s'\n", argv[1]);
        fprintf(stderr, "Run 'todoc help' for usage.\n");
        return TODOC_ERR_INVALID;
    }

    if (out->command == CMD_HELP || out->command == CMD_VERSION || out->command == CMD_INIT ||
        out->command == CMD_STATS) {
        return TODOC_OK;
    }

    return parse_flags(argc, argv, 2, out);
}

/* ── Free ────────────────────────────────────────────────────── */

void cli_args_free(cli_args_t *args)
{
    if (!args) {
        return;
    }
    free(args->title);
    free(args->description);
    free(args->type);
    free(args->priority);
    free(args->status);
    free(args->scope);
    free(args->due_date);
    task_filter_free(&args->filter);
    memset(args, 0, sizeof(*args));
}

/* ── Usage ───────────────────────────────────────────────────── */

void cli_print_usage(void)
{
    printf("todoc %s — A command-line task manager\n"
           "\n"
           "Usage:\n"
           "  todoc <command> [options]\n"
           "\n"
           "Commands:\n"
           "  init                       Initialize the task database\n"
           "  add <title> [options]      Add a new task\n"
           "  list [filters]             List tasks (alias: ls)\n"
           "  show <id>                  Show full task details\n"
           "  edit <id> [options]        Edit an existing task\n"
           "  done <id>                  Mark a task as done\n"
           "  rm <id>                    Delete a task (aliases: remove, delete)\n"
           "  stats                      Show task statistics\n"
           "  export [filters]           Export tasks (default: csv)\n"
           "  help                       Show this help message\n"
           "  version                    Show version\n"
           "\n"
           "Options:\n"
           "  --title <text>             Task title (positional for add)\n"
           "  --desc <text>              Task description\n"
           "  --type, -t <type>          bug, feature, chore, idea\n"
           "  --priority, -p <priority>  critical, high, medium, low\n"
           "  --status, -s <status>      todo, in-progress, done, blocked, cancelled\n"
           "  --scope <tag>              Project/scope tag\n"
           "  --due <YYYY-MM-DD>         Due date\n"
           "  --format, -f <format>      Export format: csv (default), json\n"
           "  --limit <n>                Limit list results\n"
           "\n"
           "Examples:\n"
           "  todoc init\n"
           "  todoc add \"Fix login bug\" --type bug --priority high --scope auth\n"
           "  todoc list --status todo --priority critical\n"
           "  todoc edit 3 --priority low --due 2026-04-15\n"
           "  todoc done 3\n"
           "  todoc rm 3\n"
           "  todoc export --format json > tasks.json\n"
           "  todoc export --status done --format csv > done.csv\n",
           TODOC_VERSION);
}
