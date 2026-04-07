# todoc fish completion
# Install: todoc completions fish > ~/.config/fish/completions/todoc.fish
# Or via:  todoc completions install

# ── Helpers ─────────────────────────────────────────────────────
function __todoc_needs_command
    set -l cmd (commandline -opc)
    test (count $cmd) -eq 1
end

function __todoc_using_command
    set -l cmd (commandline -opc)
    test (count $cmd) -ge 2; and test "$cmd[2]" = $argv[1]
end

function __todoc_projects
    todoc complete projects 2>/dev/null
end

function __todoc_labels
    todoc complete labels 2>/dev/null
end

function __todoc_task_ids
    todoc complete task-ids 2>/dev/null
end

function __todoc_commands
    todoc complete commands 2>/dev/null
end

function __todoc_topics
    todoc complete topics 2>/dev/null
end

# ── Top-level commands ──────────────────────────────────────────
complete -c todoc -f -n __todoc_needs_command -a '(__todoc_commands)'

# ── Shared enum values on flags ─────────────────────────────────
complete -c todoc -l type    -s t -f -a 'bug feature chore idea'
complete -c todoc -l priority -s p -f -a 'critical high medium low'
complete -c todoc -l format  -s f -f -a 'csv json'
complete -c todoc -l json    -s j -f -d 'One-shot ai mode'

# --status: task statuses for most commands; project statuses for -project cmds
complete -c todoc -l status -s s -f \
    -n 'not __todoc_using_command add-project; and not __todoc_using_command list-projects; and not __todoc_using_command edit-project' \
    -a 'todo in-progress done blocked cancelled abandoned'
complete -c todoc -l status -s s -f \
    -n '__todoc_using_command add-project; or __todoc_using_command list-projects; or __todoc_using_command edit-project' \
    -a 'active completed archived'

# --project / --label expect dynamic values from the DB
complete -c todoc -l project -s P -f -a '(__todoc_projects)'
complete -c todoc -l label   -s l -f -a '(__todoc_labels)'
complete -c todoc -l sub     -f    -a '(__todoc_task_ids) none'

# ── Per-command positional arguments ────────────────────────────
# Tasks by id
for cmd in show edit done rm remove delete
    complete -c todoc -f -n "__todoc_using_command $cmd" -a '(__todoc_task_ids)'
end

# Projects by name
for cmd in use show-project edit-project rm-project
    complete -c todoc -f -n "__todoc_using_command $cmd" -a '(__todoc_projects)'
end

# Labels by name
complete -c todoc -f -n '__todoc_using_command rm-label' -a '(__todoc_labels)'

# help <topic>
complete -c todoc -f -n '__todoc_using_command help' -a '(__todoc_topics)'

# completions bash|zsh|fish|install|uninstall
complete -c todoc -f -n '__todoc_using_command completions' \
    -a 'bash zsh fish install uninstall'

# ── Command-specific flags ──────────────────────────────────────
complete -c todoc -l all      -f -n '__todoc_using_command list; or __todoc_using_command stats; or __todoc_using_command export; or __todoc_using_command changelog'
complete -c todoc -l limit    -x -n '__todoc_using_command list; or __todoc_using_command export'
complete -c todoc -l scope    -x -d 'Scope tag'
complete -c todoc -l due      -x -d 'YYYY-MM-DD'
complete -c todoc -l desc     -x -d 'Description'
complete -c todoc -l color    -x -d 'Color label'
complete -c todoc -l title    -x -d 'Task title'
complete -c todoc -l clear    -f -n '__todoc_using_command use'
complete -c todoc -l global   -f -n '__todoc_using_command move'
complete -c todoc -l since    -x -n '__todoc_using_command changelog'
complete -c todoc -l list     -f -n '__todoc_using_command changelog'
complete -c todoc -l yes      -s y -f -n '__todoc_using_command uninstall'
complete -c todoc -l purge    -f -n '__todoc_using_command uninstall'
