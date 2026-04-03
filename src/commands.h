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

#endif
