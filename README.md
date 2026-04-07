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
- **Subtasks** (one nesting level) with parent-done validation, tree
  display, and an `abandoned` terminal status alongside `cancelled`
- **`move`** command to swap a task's project assignments in one step
  (parents move with all their children)
- **Labels** — many-to-many free-form tags, auto-created on first use,
  alongside the existing single-string `scope` field
- **Embedded changelog** — `todoc changelog` shows release notes from
  inside the binary, no network needed
- **Update check** — quietly notices when a new release is available
  and warns once at the end of any command, with a louder message for
  major or breaking releases
- **Agent mode** — `todoc mode ai` flips todoc into a structured-JSON
  output mode for LLM-driven workflows; `--json` is the one-shot
  equivalent
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

After an update, see what changed:

```bash
todoc changelog                  # latest release notes
todoc changelog 0.4.0            # one specific release (also accepts v0.4.0)
todoc changelog --since 0.3.0    # everything since you last upgraded
todoc changelog --all            # full history
todoc changelog --list           # just version names + dates
```

The full `CHANGELOG.md` is embedded in the binary at build time, so
this works fully offline — no network round-trip to GitHub.

todoc also checks for new releases in the background once per day and
prints a one-line warning at the end of the next command. Major or
breaking releases get a louder warning that points at
`todoc changelog --since <your-version>`. To opt out:

```bash
export TODOC_NO_UPDATE_CHECK=1
```

### Agent mode

LLM-driven agents that drive todoc as a tool can switch into a
structured-output mode where every command emits a single JSON
envelope on stdout (and a JSON error envelope on stderr if it fails).
Colors, info messages, warnings, and the update-check notification
are all suppressed in this mode — stdout is guaranteed to be exactly
one parseable JSON object.

```bash
todoc mode ai            # switch persistently
todoc mode               # show current mode (then 'ai')
todoc mode user          # switch back to colored human output
```

The persistent mode lives in `~/.todoc/mode`. Two ways to override
it without flipping the file:

```bash
TODOC_MODE=ai todoc list           # process scope
todoc list --json                  # one-shot for one command
```

Resolution order (highest precedence first): `--json` flag, then
`TODOC_MODE` env var, then `~/.todoc/mode`, then default `user`.

A typical agent workflow:

```bash
export TODOC_MODE=ai
todoc add "Fix login" --type bug --priority high
# → {"schema":"todoc/v1","command":"add","ok":true,"data":{"task":{"id":42,...}}}
todoc list | jq '.data.tasks[] | {id, title, status}'
```

Errors:

```bash
todoc show 9999
# stderr: {"schema":"todoc/v1","command":"show","ok":false,
#          "error":{"code":"not_found","message":"Task #9999 not found."}}
# exit:   1
```

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

## Subtasks

Tasks support a single level of subtasks. Use `--sub <parent-id>` when
creating a task (or via `edit`) to attach it to a parent.

```bash
todoc add "Big feature" --type feature --priority high
# → Task #42 created.
todoc add "Reproduce on staging" --sub 42
todoc add "Write integration test" --sub 42

todoc list                     # tree view: parent + indented children
todoc show 42                  # detail + children list
```

A parent can only be marked done once **every** child is in a terminal
status (`done`, `cancelled`, or `abandoned`):

```bash
todoc done 42                  # ✗ "has 2 open subtask(s)..."
todoc edit 43 --status abandoned
todoc done 44
todoc done 42                  # ✓
```

The new `abandoned` status sits next to `cancelled` — both unblock
parent completion. Deleting a parent (`todoc rm`) does **not** delete
its children: they are promoted to top-level tasks.

To promote a subtask manually:

```bash
todoc edit 43 --sub none       # detach from parent
```

## Move

`assign` is additive (a task can belong to many projects). `move`
**replaces** the task's project assignments with exactly the target,
and cascades to all of the task's children atomically:

```bash
todoc move 42 backend          # task 42 (and any subtasks) → backend
todoc move 42 --global         # remove all project links from 42 + children
```

Subtasks cannot be moved directly — move the parent instead.

## Labels

Labels are many-to-many cross-cutting tags. Use `scope` for the area
of the system (one per task) and labels for everything that cuts
across — `urgent`, `blocked-on-x`, `v2`, `external`, etc. Labels are
auto-created on first use, so there's no ceremony required.

```bash
# Create + attach in one step
todoc add "Fix login" --type bug --label urgent,security

# Or attach to an existing task (auto-creates the label)
todoc label 12 blocked-on-design

# Filter
todoc list --label urgent

# Detach
todoc unlabel 12 blocked-on-design

# Manage labels explicitly (optional)
todoc add-label v2 --color green
todoc list-labels
todoc rm-label v1
```

`edit` does not accept `--label` (it would force a destructive
replace) — use `label`/`unlabel` for additive operations.

## Task Attributes

| Attribute  | Values                                          |
|------------|--------------------------------------------------|
| **Type**     | `bug`, `feature`, `chore`, `idea`               |
| **Priority** | `critical`, `high`, `medium`, `low`             |
| **Status**   | `todo`, `in-progress`, `done`, `blocked`, `cancelled`, `abandoned` |
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
