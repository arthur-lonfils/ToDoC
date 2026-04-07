#compdef todoc
# todoc zsh completion
# Install:   todoc completions zsh > ~/.zfunc/_todoc
#            fpath+=(~/.zfunc); autoload -U compinit; compinit
# Or via:    todoc completions install

_todoc() {
    local curcontext="$curcontext" state line
    typeset -A opt_args

    local commands
    commands=(
        'init:Initialize the task database'
        'add:Add a new task'
        'list:List tasks'
        'ls:List tasks (alias)'
        'show:Show task details'
        'edit:Edit a task'
        'done:Mark a task as done'
        'rm:Delete a task'
        'remove:Delete a task (alias)'
        'delete:Delete a task (alias)'
        'stats:Show task statistics'
        'export:Export tasks (csv or json)'
        'add-project:Create a new project'
        'list-projects:List projects'
        'show-project:Show project details'
        'edit-project:Edit a project'
        'rm-project:Delete a project'
        'use:Set active project context'
        'assign:Link a task to a project'
        'unassign:Remove a task from a project'
        'move:Replace a task project assignments'
        'add-label:Create a new label'
        'list-labels:List all labels'
        'rm-label:Delete a label'
        'label:Attach a label to a task'
        'unlabel:Detach a label from a task'
        'changelog:Show release notes'
        'mode:Show the current output mode and its source'
        'help:Show help'
        'version:Show version'
        'update:Update todoc'
        'uninstall:Remove the todoc binary'
        'completions:Generate shell completion scripts'
    )

    _arguments -C \
        '1: :->cmd' \
        '*:: :->args'

    case $state in
        cmd)
            _describe -t commands 'todoc command' commands
            ;;
        args)
            case $line[1] in
                add)
                    _arguments \
                        '--type[Task type]:type:(bug feature chore idea)' \
                        '-t[Task type]:type:(bug feature chore idea)' \
                        '--priority[Priority]:priority:(critical high medium low)' \
                        '-p[Priority]:priority:(critical high medium low)' \
                        '--status[Status]:status:(todo in-progress done blocked cancelled abandoned)' \
                        '-s[Status]:status:(todo in-progress done blocked cancelled abandoned)' \
                        '--scope[Scope tag]' \
                        '--due[Due date YYYY-MM-DD]' \
                        '--desc[Description]' \
                        '--project[Project]:project:($(todoc complete projects 2>/dev/null))' \
                        '-P[Project]:project:($(todoc complete projects 2>/dev/null))' \
                        '--label[Labels (comma separated)]:label:($(todoc complete labels 2>/dev/null))' \
                        '-l[Labels]:label:($(todoc complete labels 2>/dev/null))' \
                        '--sub[Parent task id]:parent:($(todoc complete task-ids 2>/dev/null) none)' \
                        '--json[One-shot ai mode]'
                    ;;
                show|edit|done|rm|remove|delete)
                    _arguments \
                        '1:task id:($(todoc complete task-ids 2>/dev/null))' \
                        '--json[One-shot ai mode]'
                    ;;
                use|show-project|edit-project|rm-project)
                    _arguments \
                        "1:project:($(todoc complete projects 2>/dev/null))" \
                        '--json[One-shot ai mode]'
                    ;;
                rm-label)
                    _arguments "1:label:($(todoc complete labels 2>/dev/null))"
                    ;;
                assign|unassign|move)
                    _arguments \
                        '1:task id:($(todoc complete task-ids 2>/dev/null))' \
                        '2:project:($(todoc complete projects 2>/dev/null))'
                    ;;
                label|unlabel)
                    _arguments \
                        '1:task id:($(todoc complete task-ids 2>/dev/null))' \
                        '2:label:($(todoc complete labels 2>/dev/null))'
                    ;;
                list|ls|export|stats)
                    _arguments \
                        '--type[Task type]:type:(bug feature chore idea)' \
                        '-t[Task type]:type:(bug feature chore idea)' \
                        '--priority[Priority]:priority:(critical high medium low)' \
                        '-p[Priority]:priority:(critical high medium low)' \
                        '--status[Status]:status:(todo in-progress done blocked cancelled abandoned)' \
                        '-s[Status]:status:(todo in-progress done blocked cancelled abandoned)' \
                        '--scope[Scope]' \
                        '--project[Project]:project:($(todoc complete projects 2>/dev/null))' \
                        '-P[Project]:project:($(todoc complete projects 2>/dev/null))' \
                        '--label[Label]:label:($(todoc complete labels 2>/dev/null))' \
                        '-l[Label]:label:($(todoc complete labels 2>/dev/null))' \
                        '--all[Bypass active project]' \
                        '--limit[Limit results]' \
                        '--json[One-shot ai mode]'
                    ;;
                help)
                    _arguments "1:topic:($(todoc complete topics 2>/dev/null))"
                    ;;
                completions)
                    _arguments "1:shell:(bash zsh fish install uninstall)"
                    ;;
                changelog)
                    _arguments \
                        '--all[Full history]' \
                        '--since[From version]' \
                        '--list[Version names only]'
                    ;;
                uninstall)
                    _arguments \
                        '--yes[No prompt]' \
                        '-y[No prompt]' \
                        '--purge[Also remove data]'
                    ;;
            esac
            ;;
    esac
}

_todoc "$@"
