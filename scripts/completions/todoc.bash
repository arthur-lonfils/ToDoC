# todoc bash completion
# Install:   todoc completions bash > /etc/bash_completion.d/todoc
# Or inline: eval "$(todoc completions bash)"

_todoc() {
    local cur prev words cword
    _init_completion || return 2>/dev/null

    # Manual fallback when bash-completion's _init_completion is not loaded
    if [ -z "$cur" ]; then
        cur="${COMP_WORDS[COMP_CWORD]}"
        prev="${COMP_WORDS[COMP_CWORD-1]}"
    fi

    local cmd=""
    if [ "${COMP_CWORD:-0}" -ge 1 ] 2>/dev/null; then
        cmd="${COMP_WORDS[1]}"
    fi

    # Static enum completion on specific flag values
    case "$prev" in
        --type|-t)
            COMPREPLY=( $(compgen -W "bug feature chore idea" -- "$cur") )
            return 0 ;;
        --priority|-p)
            COMPREPLY=( $(compgen -W "critical high medium low" -- "$cur") )
            return 0 ;;
        --status|-s)
            case "$cmd" in
                add-project|list-projects|edit-project)
                    COMPREPLY=( $(compgen -W "active completed archived" -- "$cur") ) ;;
                *)
                    COMPREPLY=( $(compgen -W "todo in-progress done blocked cancelled abandoned" -- "$cur") ) ;;
            esac
            return 0 ;;
        --format|-f)
            COMPREPLY=( $(compgen -W "csv json" -- "$cur") )
            return 0 ;;
        --project|-P)
            COMPREPLY=( $(compgen -W "$(todoc complete projects 2>/dev/null)" -- "$cur") )
            return 0 ;;
        --label|-l)
            COMPREPLY=( $(compgen -W "$(todoc complete labels 2>/dev/null)" -- "$cur") )
            return 0 ;;
        --sub)
            COMPREPLY=( $(compgen -W "$(todoc complete task-ids 2>/dev/null) none" -- "$cur") )
            return 0 ;;
    esac

    # Flag completion (current word starts with -)
    case "$cur" in
        -*)
            local flags="--help --json"
            case "$cmd" in
                add)
                    flags="$flags --type -t --priority -p --status -s --scope --due --desc --project -P --label -l --sub" ;;
                edit)
                    flags="$flags --title --desc --type -t --priority -p --status -s --scope --due --sub" ;;
                list|ls|export|stats)
                    flags="$flags --type -t --priority -p --status -s --scope --project -P --label -l --limit --all" ;;
                add-project)
                    flags="$flags --desc --color --status -s --due" ;;
                edit-project)
                    flags="$flags --desc --color --status -s --due" ;;
                list-projects)
                    flags="$flags --status -s" ;;
                use)
                    flags="$flags --clear" ;;
                move)
                    flags="$flags --global" ;;
                add-label)
                    flags="$flags --color" ;;
                changelog)
                    flags="$flags --all --since --list" ;;
                uninstall)
                    flags="$flags --yes -y --purge" ;;
                export)
                    flags="$flags --format -f --status -s --type -t --priority -p --scope --project -P --label -l --all" ;;
            esac
            COMPREPLY=( $(compgen -W "$flags" -- "$cur") )
            return 0 ;;
    esac

    # First word: top-level command
    if [ "${COMP_CWORD:-0}" -eq 1 ]; then
        COMPREPLY=( $(compgen -W "$(todoc complete commands 2>/dev/null)" -- "$cur") )
        return 0
    fi

    # Positional completion that depends on the command
    case "$cmd" in
        show|edit|done|rm|remove|delete)
            COMPREPLY=( $(compgen -W "$(todoc complete task-ids 2>/dev/null)" -- "$cur") ) ;;
        use|show-project|edit-project|rm-project)
            COMPREPLY=( $(compgen -W "$(todoc complete projects 2>/dev/null)" -- "$cur") ) ;;
        rm-label)
            COMPREPLY=( $(compgen -W "$(todoc complete labels 2>/dev/null)" -- "$cur") ) ;;
        assign|unassign|move)
            # First positional: task id. Second: project name.
            if [ "$COMP_CWORD" = "2" ]; then
                COMPREPLY=( $(compgen -W "$(todoc complete task-ids 2>/dev/null)" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(todoc complete projects 2>/dev/null)" -- "$cur") )
            fi ;;
        label|unlabel)
            if [ "$COMP_CWORD" = "2" ]; then
                COMPREPLY=( $(compgen -W "$(todoc complete task-ids 2>/dev/null)" -- "$cur") )
            else
                COMPREPLY=( $(compgen -W "$(todoc complete labels 2>/dev/null)" -- "$cur") )
            fi ;;
        help)
            COMPREPLY=( $(compgen -W "$(todoc complete topics 2>/dev/null)" -- "$cur") ) ;;
        completions)
            COMPREPLY=( $(compgen -W "bash zsh fish install uninstall" -- "$cur") ) ;;
    esac
    return 0
}

complete -F _todoc todoc
