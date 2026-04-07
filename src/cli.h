#ifndef TODOC_CLI_H
#define TODOC_CLI_H

#define TODOC_VERSION "0.7.0"

#include "export.h"
#include "model.h"

#include <stdint.h>

/* ── Subcommands ─────────────────────────────────────────────── */

typedef enum {
    CMD_NONE = 0,
    CMD_INIT,
    CMD_ADD,
    CMD_LIST,
    CMD_SHOW,
    CMD_EDIT,
    CMD_DONE,
    CMD_RM,
    CMD_STATS,
    CMD_EXPORT,
    CMD_ADD_PROJECT,
    CMD_LIST_PROJECTS,
    CMD_SHOW_PROJECT,
    CMD_EDIT_PROJECT,
    CMD_RM_PROJECT,
    CMD_USE,
    CMD_ASSIGN,
    CMD_UNASSIGN,
    CMD_HELP,
    CMD_VERSION,
    CMD_UPDATE,
    CMD_MOVE,
    CMD_ADD_LABEL,
    CMD_LIST_LABELS,
    CMD_RM_LABEL,
    CMD_LABEL,
    CMD_UNLABEL,
    CMD_CHANGELOG,
    CMD_MODE,
} command_t;

/* ── Parsed CLI arguments ────────────────────────────────────── */

typedef struct {
    command_t command;

    /* For add/edit */
    char *title;
    char *description;
    task_type_t *type; /* NULL = not specified */
    priority_t *priority;
    status_t *status;
    char *scope;
    char *due_date;

    /* For show/edit/done/rm */
    int64_t task_id; /* 0 = not set */

    /* For list/export (filter) */
    task_filter_t filter;

    /* For export */
    export_format_t export_format;

    /* For project commands */
    char *project_name;               /* positional arg for project commands */
    char *project_color;              /* --color flag */
    project_status_t *project_status; /* --status for project commands */

    /* For --project flag (task-project association) */
    char *project; /* --project flag value */
    int all;       /* --all flag for list/stats/export */

    /* For use --clear */
    int clear;

    /* For 'help <topic>' */
    char *help_topic;

    /* For subtasks: --sub <parent-id>. parent_id < 0 means "make orphan"
     * (used by 'todoc edit <id> --sub none'), 0 means unspecified. */
    int64_t parent_id;
    int parent_set; /* 1 if --sub was passed (so we can distinguish 0/unset) */

    /* For 'move': --global removes all project assignments */
    int global;

    /* For label commands and --label flag.
     * label_name: positional name on add-label/rm-label/label/unlabel
     * label_color: --color on add-label
     * labels: comma-separated list passed to 'add --label foo,bar' or
     *         the single value passed to 'list --label foo' (which
     *         also lives in filter.label). Owned by cli_args_t. */
    char *label_name;
    char *label_color;
    char *labels;

    /* For 'changelog' */
    char *changelog_version; /* positional X.Y.Z (or vX.Y.Z) */
    char *changelog_since;   /* --since X.Y.Z */
    int changelog_list;      /* --list flag */

    /* For 'mode' command */
    char *mode_target; /* positional: "ai" or "user", NULL = show */

    /* For --json one-shot flag (forces ai mode for this invocation) */
    int output_json;
} cli_args_t;

/* Parse argc/argv into a cli_args_t. Returns TODOC_OK or TODOC_ERR_INVALID.
 * On error, a message is already printed to stderr */
todoc_err_t cli_parse(int argc, char **argv, cli_args_t *out);

/* Free heap-allocated members of cli_args_t */
void cli_args_free(cli_args_t *args);

/* Print usage/help text to stdout. If topic is NULL, prints the top-level
 * overview. Otherwise prints help for the named topic (e.g. "task",
 * "project", "export") or a specific command name. Returns 0 on success,
 * non-zero if the topic is unknown. */
int cli_print_help(const char *topic);

/* Convenience: prints the top-level overview (equivalent to
 * cli_print_help(NULL)). */
void cli_print_usage(void);

#endif
