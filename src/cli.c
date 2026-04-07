#include "cli.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Subcommand lookup ───────────────────────────────────────── */

typedef struct {
    const char *name;
    command_t cmd;
} cmd_entry_t;

static const cmd_entry_t cmd_table[] = {
    {"init", CMD_INIT},
    {"add", CMD_ADD},
    {"list", CMD_LIST},
    {"ls", CMD_LIST},
    {"show", CMD_SHOW},
    {"edit", CMD_EDIT},
    {"done", CMD_DONE},
    {"rm", CMD_RM},
    {"remove", CMD_RM},
    {"delete", CMD_RM},
    {"stats", CMD_STATS},
    {"export", CMD_EXPORT},
    {"add-project", CMD_ADD_PROJECT},
    {"list-projects", CMD_LIST_PROJECTS},
    {"show-project", CMD_SHOW_PROJECT},
    {"edit-project", CMD_EDIT_PROJECT},
    {"rm-project", CMD_RM_PROJECT},
    {"use", CMD_USE},
    {"assign", CMD_ASSIGN},
    {"unassign", CMD_UNASSIGN},
    {"help", CMD_HELP},
    {"--help", CMD_HELP},
    {"-h", CMD_HELP},
    {"version", CMD_VERSION},
    {"--version", CMD_VERSION},
    {"-v", CMD_VERSION},
    {"update", CMD_UPDATE},
    {"move", CMD_MOVE},
    {"add-label", CMD_ADD_LABEL},
    {"list-labels", CMD_LIST_LABELS},
    {"rm-label", CMD_RM_LABEL},
    {"label", CMD_LABEL},
    {"unlabel", CMD_UNLABEL},
    {"changelog", CMD_CHANGELOG},
    {"mode", CMD_MODE},
    {"uninstall", CMD_UNINSTALL},
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

static project_status_t *alloc_project_status(project_status_t val)
{
    project_status_t *p = todoc_malloc(sizeof(*p));
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
            /* For project commands, parse as project status */
            if (out->command == CMD_ADD_PROJECT || out->command == CMD_EDIT_PROJECT ||
                out->command == CMD_LIST_PROJECTS) {
                project_status_t ps = str_to_project_status(val);
                if ((int)ps == -1) {
                    fprintf(stderr,
                            "todoc: invalid project status '%s' (expected: active, completed, "
                            "archived)\n",
                            val);
                    return TODOC_ERR_INVALID;
                }
                free(out->project_status);
                out->project_status = alloc_project_status(ps);
            } else {
                status_t s = str_to_status(val);
                if ((int)s == -1) {
                    fprintf(stderr,
                            "todoc: invalid status '%s' (expected: todo, in-progress, done, "
                            "blocked, cancelled)\n",
                            val);
                    return TODOC_ERR_INVALID;
                }
                free(out->status);
                out->status = alloc_status(s);
                free(out->filter.status);
                out->filter.status = alloc_status(s);
            }
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
        } else if (strcmp(arg, "--project") == 0 || strcmp(arg, "-P") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            free(out->project);
            out->project = todoc_strdup(val);
            free(out->filter.project);
            out->filter.project = todoc_strdup(val);
        } else if (strcmp(arg, "--all") == 0) {
            out->all = 1;
            out->filter.all = 1;
        } else if (strcmp(arg, "--color") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            if (out->command == CMD_ADD_LABEL) {
                free(out->label_color);
                out->label_color = todoc_strdup(val);
            } else {
                free(out->project_color);
                out->project_color = todoc_strdup(val);
            }
        } else if (strcmp(arg, "--clear") == 0) {
            out->clear = 1;
        } else if (strcmp(arg, "--sub") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            /* 'none' means "remove parent" (only meaningful for edit) */
            if (strcmp(val, "none") == 0 || strcmp(val, "0") == 0) {
                out->parent_id = -1;
                out->parent_set = 1;
            } else {
                int64_t pid = 0;
                if (parse_task_id(val, &pid) != 0) {
                    return TODOC_ERR_INVALID;
                }
                out->parent_id = pid;
                out->parent_set = 1;
            }
        } else if (strcmp(arg, "--global") == 0) {
            out->global = 1;
        } else if (strcmp(arg, "--label") == 0 || strcmp(arg, "-l") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            free(out->labels);
            out->labels = todoc_strdup(val);
            /* Also feed the filter — for `list --label foo` the value
             * is treated as a single label name. cmd_add splits the
             * comma-separated form itself. */
            free(out->filter.label);
            out->filter.label = todoc_strdup(val);
        } else if (strcmp(arg, "--since") == 0) {
            const char *val = consume_value(argc, argv, &i, arg);
            if (!val) {
                return TODOC_ERR_INVALID;
            }
            free(out->changelog_since);
            out->changelog_since = todoc_strdup(val);
        } else if (strcmp(arg, "--list") == 0) {
            out->changelog_list = 1;
        } else if (strcmp(arg, "--json") == 0 || strcmp(arg, "-j") == 0) {
            out->output_json = 1;
        } else if (strcmp(arg, "--yes") == 0 || strcmp(arg, "-y") == 0) {
            out->yes = 1;
        } else if (strcmp(arg, "--purge") == 0) {
            out->purge = 1;
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
            } else if ((out->command == CMD_ADD_PROJECT || out->command == CMD_SHOW_PROJECT ||
                        out->command == CMD_EDIT_PROJECT || out->command == CMD_RM_PROJECT ||
                        out->command == CMD_USE) &&
                       !out->project_name) {
                out->project_name = todoc_strdup(arg);
            } else if ((out->command == CMD_ASSIGN || out->command == CMD_UNASSIGN ||
                        out->command == CMD_MOVE) &&
                       out->task_id == 0) {
                if (parse_task_id(arg, &out->task_id) != 0) {
                    return TODOC_ERR_INVALID;
                }
            } else if ((out->command == CMD_ASSIGN || out->command == CMD_UNASSIGN ||
                        out->command == CMD_MOVE) &&
                       !out->project_name) {
                out->project_name = todoc_strdup(arg);
            } else if ((out->command == CMD_ADD_LABEL || out->command == CMD_RM_LABEL) &&
                       !out->label_name) {
                out->label_name = todoc_strdup(arg);
            } else if ((out->command == CMD_LABEL || out->command == CMD_UNLABEL) &&
                       out->task_id == 0) {
                if (parse_task_id(arg, &out->task_id) != 0) {
                    return TODOC_ERR_INVALID;
                }
            } else if ((out->command == CMD_LABEL || out->command == CMD_UNLABEL) &&
                       !out->label_name) {
                out->label_name = todoc_strdup(arg);
            } else if (out->command == CMD_CHANGELOG && !out->changelog_version) {
                out->changelog_version = todoc_strdup(arg);
            } else if (out->command == CMD_MODE && !out->mode_target) {
                out->mode_target = todoc_strdup(arg);
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

    if (out->command == CMD_VERSION || out->command == CMD_INIT || out->command == CMD_UPDATE) {
        return TODOC_OK;
    }

    /* 'help' takes an optional positional topic */
    if (out->command == CMD_HELP) {
        if (argc >= 3 && !is_flag(argv[2])) {
            out->help_topic = todoc_strdup(argv[2]);
        }
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
    free(args->project_name);
    free(args->project_color);
    free(args->project_status);
    free(args->project);
    free(args->help_topic);
    free(args->label_name);
    free(args->label_color);
    free(args->labels);
    free(args->changelog_version);
    free(args->changelog_since);
    free(args->mode_target);
    memset(args, 0, sizeof(*args));
}

/* ── Usage ───────────────────────────────────────────────────── */

static void print_overview(void)
{
    printf("todoc %s — A command-line task manager\n"
           "\n"
           "Usage:\n"
           "  todoc <command> [options]\n"
           "  todoc help <topic>           Show focused help for a topic\n"
           "\n"
           "Task Commands:\n"
           "  init                       Initialize the task database\n"
           "  add <title> [options]      Add a new task\n"
           "  list [filters]             List tasks (alias: ls)\n"
           "  show <id>                  Show full task details\n"
           "  edit <id> [options]        Edit an existing task\n"
           "  done <id>                  Mark a task as done\n"
           "  rm <id>                    Delete a task (aliases: remove, delete)\n"
           "  stats                      Show task statistics\n"
           "  export [filters]           Export tasks (default: csv)\n"
           "\n"
           "Project Commands:\n"
           "  add-project <name>         Create a new project\n"
           "  list-projects              List all projects\n"
           "  show-project <name>        Show project details\n"
           "  edit-project <name>        Edit a project\n"
           "  rm-project <name>          Delete a project\n"
           "  use <name>                 Set active project context\n"
           "  use --clear                Clear active project\n"
           "  assign <id> <project>      Assign task to project\n"
           "  unassign <id> <project>    Remove task from project\n"
           "  move <id> <project>        Replace task's project assignments\n"
           "  move <id> --global         Remove all project assignments\n"
           "\n"
           "Label Commands:\n"
           "  add-label <name>           Create a new label\n"
           "  list-labels                List all labels\n"
           "  rm-label <name>            Delete a label\n"
           "  label <id> <label>         Attach a label to a task\n"
           "  unlabel <id> <label>       Detach a label from a task\n"
           "\n"
           "Other:\n"
           "  help [topic]               Show help (run 'todoc help help' for topics)\n"
           "  version                    Show version\n"
           "  update                     Update todoc to the latest release\n"
           "  changelog [version]        Show release notes (--all, --since, --list)\n"
           "  mode [ai|user]             Switch output mode (JSON for LLM agents)\n"
           "  uninstall                  Remove the todoc binary (--purge wipes data)\n"
           "\n"
           "Global flags:\n"
           "  --json, -j                 One-shot ai mode for the next command\n"
           "\n"
           "Help Topics:\n"
           "  todoc help task            Task commands and options\n"
           "  todoc help project         Project commands and options\n"
           "  todoc help label           Labels (cross-cutting tags)\n"
           "  todoc help export          Export formats and filters\n"
           "  todoc help <command>       Help for a specific command (e.g. 'add', 'use')\n"
           "\n"
           "Examples:\n"
           "  todoc init\n"
           "  todoc add \"Fix login bug\" --type bug --priority high --project auth\n"
           "  todoc list --status todo --priority critical\n"
           "  todoc add-project auth --desc \"Auth system\" --color blue\n"
           "  todoc use auth\n",
           TODOC_VERSION);
}

static void print_task_topic(void)
{
    printf("todoc — Task commands\n"
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
           "\n"
           "Options:\n"
           "  --title <text>             Task title (positional for add)\n"
           "  --desc <text>              Task description\n"
           "  --type, -t <type>          bug, feature, chore, idea\n"
           "  --priority, -p <priority>  critical, high, medium, low\n"
           "  --status, -s <status>      todo, in-progress, done, blocked, cancelled, abandoned\n"
           "  --scope <tag>              Scope tag\n"
           "  --due <YYYY-MM-DD>         Due date\n"
           "  --sub <parent-id>          Make this task a subtask of <parent-id>\n"
           "  --sub none                 (edit only) Promote a subtask to top-level\n"
           "  --label, -l <name[,...]>   Attach labels (add) or filter (list)\n"
           "  --limit <n>                Limit list results\n"
           "  --project, -P <name>       Filter by or assign to project\n"
           "  --all                      Show all tasks (bypass active project)\n"
           "\n"
           "Subtasks:\n"
           "  Tasks support a single level of subtasks. A parent can only be\n"
           "  marked done once every child is in a terminal status (done,\n"
           "  cancelled, or abandoned). Deleting a parent promotes its\n"
           "  children to top-level tasks.\n"
           "\n"
           "Examples:\n"
           "  todoc add \"Fix login bug\" --type bug --priority high\n"
           "  todoc add \"Reproduce on staging\" --sub 3\n"
           "  todoc list --status todo --priority critical\n"
           "  todoc edit 3 --priority low --due 2026-04-15\n"
           "  todoc done 3\n"
           "  todoc rm 3\n");
}

static void print_project_topic(void)
{
    printf("todoc — Project commands\n"
           "\n"
           "Projects group tasks. Set an active project with 'todoc use <name>'\n"
           "and 'list', 'stats', and 'export' will scope to it automatically.\n"
           "Use --all on any of those to bypass the active project for one\n"
           "command. Tasks and projects have a many-to-many relationship.\n"
           "\n"
           "Commands:\n"
           "  add-project <name>         Create a new project\n"
           "  list-projects              List projects (filter with --status)\n"
           "  show-project <name>        Show project details + task count\n"
           "  edit-project <name>        Edit a project\n"
           "  rm-project <name>          Delete a project (tasks survive)\n"
           "  use <name>                 Set active project context\n"
           "  use --clear                Clear active project\n"
           "  assign <id> <project>      Link task to project\n"
           "  unassign <id> <project>    Remove task from project\n"
           "  move <id> <project>        Replace a task's project assignments\n"
           "  move <id> --global         Remove all project assignments\n"
           "\n"
           "Move vs assign:\n"
           "  'assign' is additive — a task may belong to several projects.\n"
           "  'move' replaces the existing assignments with exactly the\n"
           "  target. Moving a parent task moves its children too. You\n"
           "  cannot move a subtask directly — move its parent instead.\n"
           "\n"
           "Options:\n"
           "  --desc <text>              Project description\n"
           "  --color <tag>              Color/label tag\n"
           "  --status, -s <status>      active, completed, archived\n"
           "  --due <YYYY-MM-DD>         Project due date\n"
           "  --global                   (move only) Remove all project links\n"
           "\n"
           "Examples:\n"
           "  todoc add-project auth --desc \"Auth system\" --color blue --due 2026-06-01\n"
           "  todoc add \"Fix login\" --type bug --project auth\n"
           "  todoc use auth\n"
           "  todoc list                  (shows only auth tasks)\n"
           "  todoc list --all            (shows all tasks)\n"
           "  todoc assign 3 auth\n"
           "  todoc unassign 3 auth\n"
           "  todoc edit-project auth --status completed\n"
           "  todoc use --clear\n");
}

static void print_export_topic(void)
{
    printf("todoc — Export\n"
           "\n"
           "Export tasks to CSV or JSON on stdout. All task filters apply,\n"
           "and the active project (if set) scopes the output unless --all\n"
           "is given.\n"
           "\n"
           "Usage:\n"
           "  todoc export [filters] [--format csv|json]\n"
           "\n"
           "Options:\n"
           "  --format, -f <format>      csv (default) or json\n"
           "  --status, -s <status>      Filter by status\n"
           "  --type, -t <type>          Filter by type\n"
           "  --priority, -p <priority>  Filter by priority\n"
           "  --scope <tag>              Filter by scope\n"
           "  --project, -P <name>       Filter by project\n"
           "  --all                      Bypass active project\n"
           "\n"
           "Examples:\n"
           "  todoc export                                  (csv to stdout)\n"
           "  todoc export --format json > tasks.json\n"
           "  todoc export --status done --format csv > done.csv\n"
           "  todoc export --project auth --format json\n");
}

static void print_label_topic(void)
{
    printf("todoc — Labels\n"
           "\n"
           "Labels are free-form, many-to-many tags. A task can carry any\n"
           "number of labels, and a label can be attached to any number\n"
           "of tasks. Labels are independent of 'scope' (one primary string\n"
           "per task) — use scope for the area of the system, labels for\n"
           "cross-cutting attributes like 'urgent', 'blocked-on-x', 'v2'.\n"
           "\n"
           "Labels are auto-created on first use, so 'todoc label 5 urgent'\n"
           "and 'todoc add ... --label urgent' both work without ceremony.\n"
           "\n"
           "Commands:\n"
           "  add-label <name> [--color]  Create a label (optional metadata)\n"
           "  list-labels                 List every label\n"
           "  rm-label <name>             Delete a label (cascades to associations)\n"
           "  label <id> <label>          Attach a label to a task\n"
           "  unlabel <id> <label>        Detach a label from a task\n"
           "\n"
           "Filtering and creation:\n"
           "  todoc add \"...\" --label urgent,blocked     Attach on creation\n"
           "  todoc list --label urgent                  Filter by label\n"
           "\n"
           "Examples:\n"
           "  todoc add-label urgent --color red\n"
           "  todoc add \"Fix login\" --type bug --label urgent,security\n"
           "  todoc label 12 blocked-on-design\n"
           "  todoc list --label urgent\n"
           "  todoc unlabel 12 blocked-on-design\n"
           "  todoc rm-label v1\n");
}

/* Per-command focused help. Returns 1 if printed, 0 if no match. */
static int print_command_topic(const char *cmd)
{
    if (strcmp(cmd, "init") == 0) {
        printf("todoc init — Initialize the task database\n"
               "\n"
               "Creates ~/.todoc/todoc.db if missing and applies any pending\n"
               "schema migrations. Idempotent — safe to re-run after upgrades.\n");
    } else if (strcmp(cmd, "add") == 0) {
        printf("todoc add <title> [options] — Add a new task\n"
               "\n"
               "Options:\n"
               "  --desc <text>              Description\n"
               "  --type, -t <type>          bug, feature, chore, idea\n"
               "  --priority, -p <priority>  critical, high, medium, low\n"
               "  --scope <tag>              Scope tag\n"
               "  --due <YYYY-MM-DD>         Due date\n"
               "  --project, -P <name>       Assign to project on creation\n"
               "\n"
               "If an active project is set, new tasks are auto-assigned to it.\n"
               "\n"
               "Examples:\n"
               "  todoc add \"Fix login\" --type bug --priority high\n"
               "  todoc add \"Add OAuth\" --type feature --project auth\n");
    } else if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) {
        printf("todoc list [filters] — List tasks (alias: ls)\n"
               "\n"
               "When an active project is set, only its tasks are shown.\n"
               "Use --all to bypass the active project.\n"
               "\n"
               "Filters:\n"
               "  --status, -s <status>      todo, in-progress, done, blocked, cancelled\n"
               "  --type, -t <type>          bug, feature, chore, idea\n"
               "  --priority, -p <priority>  critical, high, medium, low\n"
               "  --scope <tag>              Filter by scope\n"
               "  --project, -P <name>       Filter by project\n"
               "  --all                      Bypass active project\n"
               "  --limit <n>                Limit number of results\n");
    } else if (strcmp(cmd, "show") == 0) {
        printf("todoc show <id> — Show full task details\n");
    } else if (strcmp(cmd, "edit") == 0) {
        printf("todoc edit <id> [options] — Edit an existing task\n"
               "\n"
               "Any task option (--title, --desc, --type, --priority, --status,\n"
               "--scope, --due) can be passed; only the supplied fields change.\n");
    } else if (strcmp(cmd, "done") == 0) {
        printf("todoc done <id> — Mark a task as done\n");
    } else if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "remove") == 0 || strcmp(cmd, "delete") == 0) {
        printf("todoc rm <id> — Delete a task (aliases: remove, delete)\n");
    } else if (strcmp(cmd, "stats") == 0) {
        printf("todoc stats — Show task statistics\n"
               "\n"
               "Counts tasks by status, priority, and type. Scoped to the active\n"
               "project when one is set.\n");
    } else if (strcmp(cmd, "export") == 0) {
        print_export_topic();
    } else if (strcmp(cmd, "add-project") == 0) {
        printf("todoc add-project <name> [options] — Create a new project\n"
               "\n"
               "Options:\n"
               "  --desc <text>              Description\n"
               "  --color <tag>              Color/label tag\n"
               "  --due <YYYY-MM-DD>         Due date\n"
               "  --status, -s <status>      active (default), completed, archived\n");
    } else if (strcmp(cmd, "list-projects") == 0) {
        printf("todoc list-projects [--status <status>] — List projects\n");
    } else if (strcmp(cmd, "show-project") == 0) {
        printf("todoc show-project <name> — Show project details + task count\n");
    } else if (strcmp(cmd, "edit-project") == 0) {
        printf("todoc edit-project <name> [options] — Edit a project\n"
               "\n"
               "Options: --desc, --color, --due, --status\n");
    } else if (strcmp(cmd, "rm-project") == 0) {
        printf("todoc rm-project <name> — Delete a project\n"
               "\n"
               "Tasks belonging to the project are NOT deleted; only the\n"
               "project and its task associations are removed.\n");
    } else if (strcmp(cmd, "use") == 0) {
        printf("todoc use <name> — Set the active project context\n"
               "todoc use --clear — Clear the active project\n"
               "\n"
               "While an active project is set, 'list', 'stats', and 'export'\n"
               "are scoped to it. Use --all on any of those to bypass it.\n"
               "New tasks created with 'todoc add' are auto-assigned to the\n"
               "active project.\n");
    } else if (strcmp(cmd, "assign") == 0) {
        printf("todoc assign <id> <project> — Link a task to a project\n"
               "\n"
               "A task may belong to multiple projects. Re-assigning is a no-op.\n");
    } else if (strcmp(cmd, "unassign") == 0) {
        printf("todoc unassign <id> <project> — Remove a task from a project\n");
    } else if (strcmp(cmd, "move") == 0) {
        printf("todoc move <id> <project> — Replace a task's project assignments\n"
               "todoc move <id> --global — Remove all project assignments\n"
               "\n"
               "Unlike 'assign' (additive), 'move' replaces existing project\n"
               "links with exactly the target. Moving a parent task atomically\n"
               "moves all its children to the same project. Subtasks cannot\n"
               "be moved directly — move the parent instead.\n");
    } else if (strcmp(cmd, "add-label") == 0) {
        printf("todoc add-label <name> [--color <c>] — Create a new label\n"
               "\n"
               "Labels are usually auto-created when first used by 'label' or\n"
               "'add --label'. Use this command only when you want to set\n"
               "optional metadata (color) up front.\n");
    } else if (strcmp(cmd, "list-labels") == 0) {
        printf("todoc list-labels — List every label\n");
    } else if (strcmp(cmd, "rm-label") == 0) {
        printf("todoc rm-label <name> — Delete a label\n"
               "\n"
               "Tasks that carried the label are not affected — only the\n"
               "label row and its associations are removed.\n");
    } else if (strcmp(cmd, "label") == 0) {
        printf("todoc label <id> <label> — Attach a label to a task\n"
               "\n"
               "The label is auto-created if it does not already exist.\n");
    } else if (strcmp(cmd, "unlabel") == 0) {
        printf("todoc unlabel <id> <label> — Detach a label from a task\n");
    } else if (strcmp(cmd, "uninstall") == 0) {
        printf("todoc uninstall — Remove the todoc binary\n"
               "\n"
               "Usage:\n"
               "  todoc uninstall              Remove the binary, keep ~/.todoc/ data\n"
               "  todoc uninstall --yes        Same, but skip the interactive confirmation\n"
               "  todoc uninstall --purge      Also remove ~/.todoc/ (data and backups)\n"
               "  todoc uninstall --purge -y   Full nuke, no prompt\n"
               "\n"
               "If todoc is installed in a system directory like /usr/local/bin\n"
               "the unlink will fail with permission denied — re-run with sudo:\n"
               "\n"
               "  sudo todoc uninstall --purge -y\n"
               "\n"
               "Self-removal is safe on Linux: unlink() removes the directory\n"
               "entry but the running process keeps the inode until it exits,\n"
               "so the success message still prints. In ai mode --yes is\n"
               "required (interactive prompts can't be answered).\n");
    } else if (strcmp(cmd, "mode") == 0) {
        printf("todoc mode [ai|user] — Switch output mode\n"
               "\n"
               "todoc has two output modes:\n"
               "  user (default)  colored, human-formatted output\n"
               "  ai              structured JSON envelopes for LLM-driven agents\n"
               "\n"
               "Usage:\n"
               "  todoc mode               Show the current mode\n"
               "  todoc mode ai            Switch to ai mode persistently\n"
               "  todoc mode user          Switch back to user mode\n"
               "\n"
               "Resolution order (highest precedence first):\n"
               "  1. The --json flag (one-shot ai mode for one command)\n"
               "  2. The TODOC_MODE env var (ai or user)\n"
               "  3. The persistent ~/.todoc/mode file\n"
               "  4. Default: user\n"
               "\n"
               "In ai mode:\n"
               "  - stdout contains exactly one JSON envelope per command\n"
               "  - stderr is empty on success, one JSON error envelope on failure\n"
               "  - colors, info messages, warnings, and the update-check\n"
               "    notification are all suppressed\n");
    } else if (strcmp(cmd, "changelog") == 0) {
        printf("todoc changelog [version] — Show release notes\n"
               "\n"
               "The full CHANGELOG.md is embedded in the binary at build time,\n"
               "so this command works fully offline. By default it prints the\n"
               "section for the latest release.\n"
               "\n"
               "Usage:\n"
               "  todoc changelog                  Latest release only (default)\n"
               "  todoc changelog 0.4.0            One specific release\n"
               "  todoc changelog v0.4.0           Same — leading 'v' is accepted\n"
               "  todoc changelog --since 0.3.0    Everything strictly after 0.3.0\n"
               "  todoc changelog --all            Full history\n"
               "  todoc changelog --list           Just version names + dates\n"
               "\n"
               "After a 'todoc update', this is the fastest way to see what\n"
               "changed in the version you just installed.\n");
    } else if (strcmp(cmd, "help") == 0) {
        printf("todoc help [topic] — Show help\n"
               "\n"
               "Topics:\n"
               "  task        Task commands and options\n"
               "  project     Project commands and options\n"
               "  export      Export formats and filters\n"
               "  <command>   Any command name (e.g. 'add', 'use', 'edit-project')\n");
    } else if (strcmp(cmd, "version") == 0) {
        printf("todoc version — Show the installed version\n");
    } else if (strcmp(cmd, "update") == 0) {
        printf(
            "todoc update — Update todoc to the latest release\n"
            "\n"
            "Downloads and installs the latest release binary from GitHub,\n"
            "backs up your task database first, and applies any new schema\n"
            "migrations automatically. Equivalent to:\n"
            "\n"
            "  curl -sSL "
            "https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/install.sh | sh\n"
            "\n"
            "Steps performed:\n"
            "  1. Backup ~/.todoc/todoc.db (if it exists)\n"
            "  2. Download and install the latest release\n"
            "  3. Run 'todoc init' to apply pending migrations\n"
            "\n"
            "The backup path is printed at the end so you can restore it\n"
            "if anything goes wrong:\n"
            "  cp ~/.todoc/todoc.db.backup-<timestamp> ~/.todoc/todoc.db\n");
    } else {
        return 0;
    }
    return 1;
}

int cli_print_help(const char *topic)
{
    if (!topic) {
        print_overview();
        return 0;
    }

    if (strcmp(topic, "task") == 0 || strcmp(topic, "tasks") == 0) {
        print_task_topic();
        return 0;
    }
    if (strcmp(topic, "project") == 0 || strcmp(topic, "projects") == 0) {
        print_project_topic();
        return 0;
    }
    if (strcmp(topic, "label") == 0 || strcmp(topic, "labels") == 0) {
        print_label_topic();
        return 0;
    }
    if (strcmp(topic, "export") == 0) {
        print_export_topic();
        return 0;
    }

    if (print_command_topic(topic)) {
        return 0;
    }

    fprintf(stderr, "todoc: unknown help topic '%s'\n", topic);
    fprintf(stderr, "Run 'todoc help' for available topics.\n");
    return 1;
}

void cli_print_usage(void)
{
    print_overview();
}
