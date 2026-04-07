#include "completions.h"

#include <string.h>

const completion_script_t *completions_find(const char *shell)
{
    if (!shell) {
        return NULL;
    }
    for (int i = 0; i < completion_scripts_count; i++) {
        if (strcmp(completion_scripts[i].shell, shell) == 0) {
            return &completion_scripts[i];
        }
    }
    return NULL;
}
