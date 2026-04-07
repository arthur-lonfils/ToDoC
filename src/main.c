#include "cli.h"
#include "commands.h"
#include "db.h"
#include "display.h"
#include "output.h"
#include "update_check.h"

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

    /* Resolve output mode (--json > TODOC_MODE > ~/.todoc/mode > user)
     * before any handler runs so display_* and output_* both know which
     * branch to take. */
    output_init(args.output_json);

    /* Kick off the background update-check refresh as early as possible
     * so the child has the most time to finish before the user runs
     * todoc again. No-op for noisy commands, in ai mode, or when
     * disabled by env var. */
    update_check_refresh_async(args.command);

    /* Handle commands that don't need the database */
    if (args.command == CMD_HELP) {
        int rc = cli_print_help(args.help_topic);
        cli_args_free(&args);
        return rc;
    }
    if (args.command == CMD_VERSION) {
        printf("todoc %s\n", TODOC_VERSION);
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

    /* update shells out to the install script; no DB needed */
    if (args.command == CMD_UPDATE) {
        err = cmd_update(&args);
        cli_args_free(&args);
        return (err == TODOC_OK) ? 0 : 1;
    }

    /* changelog reads embedded data; no DB needed */
    if (args.command == CMD_CHANGELOG) {
        err = cmd_changelog(&args);
        cli_args_free(&args);
        return (err == TODOC_OK) ? 0 : 1;
    }

    /* mode is a meta-command — no DB, no need for output_init magic
     * (cmd_mode handles its own output) */
    if (args.command == CMD_MODE) {
        err = cmd_mode(&args);
        cli_args_free(&args);
        return (err == TODOC_OK) ? 0 : 1;
    }

    /* uninstall removes the binary itself; no DB needed */
    if (args.command == CMD_UNINSTALL) {
        err = cmd_uninstall(&args);
        cli_args_free(&args);
        return (err == TODOC_OK) ? 0 : 1;
    }

    /* completions just prints embedded scripts or writes a file; no DB needed */
    if (args.command == CMD_COMPLETIONS) {
        err = cmd_completions(&args);
        cli_args_free(&args);
        return (err == TODOC_OK) ? 0 : 1;
    }

    /* All other commands need the database open */
    if (db_open() != TODOC_OK) {
        output_error("dispatch", "db_error", "Failed to open database: %s", db_last_error());
        display_info("Run 'todoc init' to create the database.");
        cli_args_free(&args);
        return 1;
    }

    /* Dispatch table */
    typedef todoc_err_t (*cmd_handler_t)(const cli_args_t *);
    static const cmd_handler_t handlers[] = {
        [CMD_ADD] = cmd_add,
        [CMD_LIST] = cmd_list,
        [CMD_SHOW] = cmd_show,
        [CMD_EDIT] = cmd_edit,
        [CMD_DONE] = cmd_done,
        [CMD_EXPORT] = cmd_export,
        [CMD_RM] = cmd_rm,
        [CMD_STATS] = cmd_stats,
        [CMD_ADD_PROJECT] = cmd_add_project,
        [CMD_LIST_PROJECTS] = cmd_list_projects,
        [CMD_SHOW_PROJECT] = cmd_show_project,
        [CMD_EDIT_PROJECT] = cmd_edit_project,
        [CMD_RM_PROJECT] = cmd_rm_project,
        [CMD_USE] = cmd_use,
        [CMD_ASSIGN] = cmd_assign,
        [CMD_UNASSIGN] = cmd_unassign,
        [CMD_MOVE] = cmd_move,
        [CMD_ADD_LABEL] = cmd_add_label,
        [CMD_LIST_LABELS] = cmd_list_labels,
        [CMD_RM_LABEL] = cmd_rm_label,
        [CMD_LABEL] = cmd_label,
        [CMD_UNLABEL] = cmd_unlabel,
        [CMD_COMPLETE] = cmd_complete,
    };

    cmd_handler_t handler = handlers[args.command];
    if (handler) {
        err = handler(&args);
    } else {
        output_error("dispatch", "internal", "Internal error: unhandled command.");
        err = TODOC_ERR_INVALID;
    }

    db_close();
    /* Show the (possibly stale) cached update warning at the very end,
     * after the command's own output, so it never interrupts the result. */
    update_check_show_warning(args.command);
    cli_args_free(&args);
    return (err == TODOC_OK) ? 0 : 1;
}
