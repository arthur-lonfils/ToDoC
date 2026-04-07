#ifndef TODOC_COMPLETIONS_H
#define TODOC_COMPLETIONS_H

#include <stddef.h>

/* One embedded shell completion script. The body is the full text of
 * the script — bash/zsh/fish — generated at build time by
 * scripts/embed_completions.sh from scripts/completions/. */
typedef struct {
    const char *shell; /* "bash", "zsh", "fish" */
    const char *body;
} completion_script_t;

extern const completion_script_t completion_scripts[];
extern const int completion_scripts_count;

/* Look up an embedded script by shell name. Returns NULL if unknown. */
const completion_script_t *completions_find(const char *shell);

#endif
