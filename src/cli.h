#ifndef TODOC_CLI_H
#define TODOC_CLI_H

#define TODOC_VERSION "0.3.0"

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
} cli_args_t;

/* Parse argc/argv into a cli_args_t. Returns TODOC_OK or TODOC_ERR_INVALID.
 * On error, a message is already printed to stderr */
todoc_err_t cli_parse(int argc, char **argv, cli_args_t *out);

/* Free heap-allocated members of cli_args_t */
void cli_args_free(cli_args_t *args);

/* Print usage/help text to stdout */
void cli_print_usage(void);

#endif
