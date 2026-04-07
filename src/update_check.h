#ifndef TODOC_UPDATE_CHECK_H
#define TODOC_UPDATE_CHECK_H

#include "cli.h"

/* Returns 1 if the update check is suppressed for this invocation —
 * either because TODOC_NO_UPDATE_CHECK is set in the environment, or
 * because the command is one we deliberately stay quiet around
 * (help, version, update, changelog). */
int update_check_disabled(command_t cmd);

/* Spawn a detached background process that fetches the latest release
 * tag from the GitHub API and writes it to ~/.todoc/update_check.
 * Returns immediately. No-op if the cache is fresh (<24h old) or if
 * the check is disabled. The fetch is silent — failures (offline,
 * 404, etc.) leave the cache untouched and try again next TTL. */
void update_check_refresh_async(command_t cmd);

/* Read the cache and print a one-line warning to stderr if a newer
 * version is available. No-op if disabled, if no cache exists, or if
 * the cached version is not newer than the running binary. */
void update_check_show_warning(command_t cmd);

#endif
