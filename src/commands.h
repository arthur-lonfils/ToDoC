#ifndef TODOC_COMMANDS_H
#define TODOC_COMMANDS_H

#include "cli.h"

todoc_err_t cmd_init(const cli_args_t *args);
todoc_err_t cmd_add(const cli_args_t *args);
todoc_err_t cmd_list(const cli_args_t *args);
todoc_err_t cmd_show(const cli_args_t *args);
todoc_err_t cmd_edit(const cli_args_t *args);
todoc_err_t cmd_done(const cli_args_t *args);
todoc_err_t cmd_rm(const cli_args_t *args);
todoc_err_t cmd_stats(const cli_args_t *args);
todoc_err_t cmd_export(const cli_args_t *args);

todoc_err_t cmd_add_project(const cli_args_t *args);
todoc_err_t cmd_list_projects(const cli_args_t *args);
todoc_err_t cmd_show_project(const cli_args_t *args);
todoc_err_t cmd_edit_project(const cli_args_t *args);
todoc_err_t cmd_rm_project(const cli_args_t *args);
todoc_err_t cmd_use(const cli_args_t *args);
todoc_err_t cmd_assign(const cli_args_t *args);
todoc_err_t cmd_unassign(const cli_args_t *args);

todoc_err_t cmd_update(const cli_args_t *args);
todoc_err_t cmd_move(const cli_args_t *args);

todoc_err_t cmd_add_label(const cli_args_t *args);
todoc_err_t cmd_list_labels(const cli_args_t *args);
todoc_err_t cmd_rm_label(const cli_args_t *args);
todoc_err_t cmd_label(const cli_args_t *args);
todoc_err_t cmd_unlabel(const cli_args_t *args);

todoc_err_t cmd_changelog(const cli_args_t *args);
todoc_err_t cmd_mode(const cli_args_t *args);
todoc_err_t cmd_uninstall(const cli_args_t *args);
todoc_err_t cmd_completions(const cli_args_t *args);
todoc_err_t cmd_complete(const cli_args_t *args);

#endif
