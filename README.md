# todoc

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.1.0-green.svg)](.version)
[![CI](https://github.com/arthur-lonfils/ToDoC/actions/workflows/ci.yml/badge.svg)](https://github.com/arthur-lonfils/ToDoC/actions/workflows/ci.yml)

A fast, full-featured command-line task manager written in C. SQLite-backed with ANSI color output, filtering, export, and zero memory leaks.

## Features

- Full CRUD: add, list, show, edit, done, delete
- Task attributes: type, priority, status, scope, due date, description
- Filtered views by any attribute combination
- Export to CSV or JSON
- Color-coded terminal output (respects `NO_COLOR`)
- SQLite storage with versioned migrations (embedded in binary)
- Automated changelog and semantic versioning

## Install

**Requirements:** GCC (C11), libsqlite3-dev

```bash
# Build
make

# Install system-wide
sudo make install

# Or install locally (no sudo)
make install PREFIX=~/.local

# Initialize the database
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
make test            # run 56 tests
make test-valgrind   # run tests with leak checking
make format          # auto-format with clang-format
make quality         # format check
make release         # auto-version, changelog, tag
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for full guidelines.

## Branch Conventions

| Prefix              | Purpose                                     |
|---------------------|---------------------------------------------|
| `story/<desc>`      | Features, refactors, improvements, tests, docs |
| `defect/<desc>`     | Bug fixes, security patches                 |

All branches target `main` via pull request with squash merge.

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
scripts/          Release automation
tests/            Automated test suite
```

## License

[MIT](LICENSE) - Arthur Lonfils
