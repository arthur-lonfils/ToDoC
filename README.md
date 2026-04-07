# todoc

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.4.0-green.svg)](.version)
[![CI](https://github.com/arthur-lonfils/ToDoC/actions/workflows/ci.yml/badge.svg)](https://github.com/arthur-lonfils/ToDoC/actions/workflows/ci.yml)

A fast, full-featured command-line task manager written in C. SQLite-backed with ANSI color output, filtering, export, and zero memory leaks.

## Features

- Full CRUD: add, list, show, edit, done, delete
- Task attributes: type, priority, status, scope, due date, description
- **Projects** for grouping tasks, with an active-project context that
  scopes `list`, `stats`, and `export` by default
- Many-to-many task ↔ project relationships
- Filtered views by any attribute combination
- Export to CSV or JSON
- Color-coded terminal output (respects `NO_COLOR`)
- SQLite storage with versioned migrations (embedded in binary)
- Automated changelog and semantic versioning

## Install

### Quick install (Linux x86_64)

One-liner that downloads the latest release into `~/.local/bin`:

```bash
curl -sSL https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/install.sh | sh
```

System-wide install:

```bash
curl -sSL https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/install.sh | sudo PREFIX=/usr/local sh
```

Pin a specific version:

```bash
curl -sSL https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/install.sh | sh -s v0.4.0
```

The installer also backs up your existing `~/.todoc/todoc.db` (if any)
and runs `todoc init` to apply pending schema migrations, so it is
safe to use both for first install and for upgrades.

### Update

If todoc is already installed, just run:

```bash
todoc update
```

This shells out to the same install script and performs the full
backup → download → migrate flow. The backup path is printed at the
end so you can roll back with:

```bash
cp ~/.todoc/todoc.db.backup-<timestamp> ~/.todoc/todoc.db
```

Equivalent one-liner (works even before todoc is installed):

```bash
curl -sSL https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/install.sh | sh
```

For a system-wide update, prepend `sudo` and `PREFIX=/usr/local`
the same way as the install command above.

### Build from source

**Requirements:** GCC (C11), libsqlite3-dev

```bash
make                            # build
sudo make install               # system-wide
make install PREFIX=~/.local    # local, no sudo
```

### First run

The installer runs `todoc init` for you. If you built from source, run
it manually once:

```bash
todoc init
```

## Usage

```bash
# Add tasks
todoc add "Fix login bug" --type bug --priority high --scope auth
todoc add "Add dark mode" --type feature --priority medium --scope ui --due 2026-05-01

# List and filter
todoc list
todoc list --status todo --priority critical
todoc list --type bug --scope auth --limit 10
todoc ls                             # alias

# View details
todoc show 1

# Edit
todoc edit 1 --priority low --status in-progress

# Mark as done
todoc done 1

# Delete
todoc rm 1                           # aliases: remove, delete

# Statistics
todoc stats

# Export
todoc export                         # CSV to stdout
todoc export --format json > tasks.json
todoc export --status done --format csv > done.csv
```

## Projects

Group tasks into projects and switch context with `todoc use` so the
common commands operate on a single project by default.

```bash
# Create a project with metadata
todoc add-project auth --desc "Authentication system" --color blue --due 2026-06-01

# Add a task directly to a project
todoc add "Fix login bug" --type bug --priority high --project auth

# Or assign / unassign existing tasks (a task can belong to many projects)
todoc assign 42 auth
todoc unassign 42 auth

# Set the active project — list/stats/export now scope to it automatically
todoc use auth
todoc list                  # only auth tasks
todoc list --all            # bypass active project for one command
todoc stats                 # scoped to auth

# Clear the active project
todoc use --clear

# Manage projects
todoc list-projects                              # all projects
todoc list-projects --status active              # filter by lifecycle
todoc show-project auth                          # detail + task count
todoc edit-project auth --status completed
todoc rm-project auth                            # tasks survive, links removed
```

Project lifecycle statuses: `active`, `completed`, `archived`.

## Task Attributes

| Attribute  | Values                                          |
|------------|--------------------------------------------------|
| **Type**     | `bug`, `feature`, `chore`, `idea`               |
| **Priority** | `critical`, `high`, `medium`, `low`             |
| **Status**   | `todo`, `in-progress`, `done`, `blocked`, `cancelled` |
| **Scope**    | Any string tag (e.g., `auth`, `ui`, `backend`)  |
| **Due date** | `YYYY-MM-DD` format                             |

## Development

```bash
make setup           # install git hooks (commit lint + format check)
make                 # build
make test            # run the test suite
make test-valgrind   # run tests with leak checking
make format          # auto-format with clang-format
make quality         # format check
make release         # auto-version, changelog, tag
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for full guidelines.

## Branch Conventions

| Prefix              | Target | Purpose                                     |
|---------------------|--------|---------------------------------------------|
| `story/<desc>`      | `main` | Features, refactors, improvements, tests, docs |
| `defect/<desc>`     | `main` | Bug fixes, security patches                 |

All branches target `main` via pull request with squash merge. Releases are done from `main` via `make release`.

## Project Structure

```
src/
  main.c          Entry point, dispatch table
  cli.h/c         Argument parsing
  commands.h/c    Subcommand handlers
  db.h/c          SQLite layer
  display.h/c     ANSI terminal output
  export.h/c      CSV/JSON export
  model.h/c       Task struct, enums
  migrate.h/c     Migration runner
  util.h/c        Helpers (alloc, paths, dates)

sql/migrations/   Versioned SQL migration files
hooks/            Git hook scripts
scripts/          Release automation, install/update script
tests/            Automated test suite
```

## License

[MIT](LICENSE) - Arthur Lonfils
