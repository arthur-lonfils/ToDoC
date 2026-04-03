#include "cli.h"
#include "commands.h"
#include "db.h"
#include "display.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    display_init();

    cli_args_t args = {0};
    todoc_err_t err = cli_parse(argc, argv, &args);
    if (err != TODOC_OK) {
        cli_args_free(&args);
        return 1;
    }

    /* Handle commands that don't need the database */
    if (args.command == CMD_HELP) {
        cli_print_usage();
        cli_args_free(&args);
        return 0;
    }
    if (args.command == CMD_VERSION) {
        printf("todoc 0.1.0\n");
        cli_args_free(&args);
        return 0;
    }

    /* init opens the DB itself */
    if (args.command == CMD_INIT) {
        err = cmd_init(&args);
        db_close();
        cli_args_free(&args);
        return (err == TODOC_OK) ? 0 : 1;
    }

    /* All other commands need the database open */
    if (db_open() != TODOC_OK) {
        display_error("Failed to open database: %s", db_last_error());
        display_info("Run 'todoc init' to create the database.");
        cli_args_free(&args);
        return 1;
    }

    /* Dispatch table */
    typedef todoc_err_t (*cmd_handler_t)(const cli_args_t *);
    static const cmd_handler_t handlers[] = {
        [CMD_ADD]   = cmd_add,
        [CMD_LIST]  = cmd_list,
        [CMD_SHOW]  = cmd_show,
        [CMD_EDIT]  = cmd_edit,
        [CMD_DONE]  = cmd_done,
        [CMD_RM]    = cmd_rm,
        [CMD_STATS] = cmd_stats,
    };

    cmd_handler_t handler = handlers[args.command];
    if (handler) {
        err = handler(&args);
    } else {
        display_error("Internal error: unhandled command.");
        err = TODOC_ERR_INVALID;
    }

    db_close();
    cli_args_free(&args);
    return (err == TODOC_OK) ? 0 : 1;
}
