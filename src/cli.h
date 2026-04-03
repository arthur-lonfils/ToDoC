#ifndef TODOC_CLI_H
#define TODOC_CLI_H

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
    CMD_HELP,
    CMD_VERSION,
} command_t;

/* ── Parsed CLI arguments ────────────────────────────────────── */

typedef struct {
    command_t    command;

    /* For add/edit */
    char        *title;
    char        *description;
    task_type_t *type;          /* NULL = not specified */
    priority_t  *priority;
    status_t    *status;
    char        *scope;
    char        *due_date;

    /* For show/edit/done/rm */
    int64_t      task_id;       /* 0 = not set */

    /* For list (filter) */
    task_filter_t filter;
} cli_args_t;

/* Parse argc/argv into a cli_args_t. Returns TODOC_OK or TODOC_ERR_INVALID.
 * On error, a message is already printed to stderr */
todoc_err_t cli_parse(int argc, char **argv, cli_args_t *out);

/* Free heap-allocated members of cli_args_t */
void cli_args_free(cli_args_t *args);

/* Print usage/help text to stdout */
void cli_print_usage(void);

#endif
