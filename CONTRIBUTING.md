# Contributing to todoc

## Prerequisites

- **GCC** (C11 support)
- **SQLite3** dev library (`sudo apt install libsqlite3-dev`)
- **clang-format** (`sudo apt install clang-format`)
- **git-cliff** (`cargo install git-cliff`) — for changelog generation

## Setup

```bash
git clone git@github.com:arthur-lonfils/ToDoC.git
cd ToDoC
make setup    # installs git hooks (commit-msg + pre-commit)
make          # build
make test     # run test suite
```

## Git Conventions

### Branch Naming

```
story/<description>     # Features, refactors, improvements, tests, docs
defect/<description>    # Bug fixes
```

Branch names are validated by CI. Use lowercase with hyphens (e.g., `story/add-export-json`, `defect/fix-limit-clause`).

### Workflow

1. Create a branch from `main` following the naming convention
2. Make changes with conventional commits
3. Open a PR to `main` — CI runs build, test, format-check, commit lint
4. Squash merge — CI auto-bumps version, updates changelog, tags, creates GitHub release

### Commit Messages

We follow [Conventional Commits](https://conventionalcommits.org). The `commit-msg` hook enforces this automatically.

```
{type}({scope}): {description}
```

**Types:** `feat`, `fix`, `refactor`, `perf`, `test`, `chore`, `docs`, `ci`, `build`

**Scopes:** `db`, `cli`, `model`, `display`, `export`, `migrate`, `util`, `deps`

**Examples:**
```
feat(cli): add export command with CSV and JSON output
fix(db): prevent LIMIT clause from being overwritten
refactor(display): extract color helpers into separate functions
docs: add CONTRIBUTING.md
chore(deps): update sqlite to 3.45
feat(cli)!: change --format flag to require explicit value
```

**Rules:**
- Header max 72 characters
- Use imperative mood ("add", not "added")
- Lowercase description start
- No period at the end
- Use `!` before `:` for breaking changes

### Merge Strategy

Squash merge feature branches into `main`.

## Code Quality

### Formatting

All C code must be formatted with `clang-format`:

```bash
make format        # auto-format all source files
make format-check  # check without modifying (used by pre-commit hook)
```

The pre-commit hook runs `format-check` on staged `.c`/`.h` files automatically.

### Compiler Warnings

We compile with `-Wall -Wextra -Wpedantic -Werror -Wshadow`. All warnings are errors. No exceptions.

### Memory Safety

```bash
make test-valgrind   # run full test suite under valgrind
```

Every code path must be leak-free. The test suite covers all commands, filters, and error paths.

## Testing

```bash
make test            # fast: 56 tests, ~1s
make test-valgrind   # with leak checking, ~30s
```

Tests run against an isolated temporary database (overrides `$HOME`). Your real `~/.todoc/todoc.db` is never touched.

## Releasing

Versions follow [Semantic Versioning](https://semver.org/). While pre-v1.0.0, `feat` bumps minor, `fix` bumps patch.

### Automated (CI)

Releases happen automatically when a PR with `feat`/`fix`/`perf` commits is merged to `main`. The release workflow:

1. Detects the version bump from conventional commits (via git-cliff)
2. Updates `.version` and `src/cli.c` version string
3. Generates `CHANGELOG.md`
4. Commits, tags, pushes
5. Creates a GitHub Release with changelog notes

### Manual

```bash
make release                    # auto-detect bump from commits
./scripts/release.sh 0.2.0     # explicit version
git push origin main --tags     # push release
```

The release script:
1. Validates clean working tree
2. Bumps `.version` and `src/cli.c` version string
3. Generates `CHANGELOG.md` via git-cliff
4. Commits and creates an annotated tag

## Project Structure

```
src/
├── main.c          # Entry point, dispatch table
├── cli.h/c         # Argument parsing
├── commands.h/c    # Subcommand handlers
├── db.h/c          # SQLite layer
├── display.h/c     # ANSI terminal output
├── export.h/c      # CSV/JSON export
├── model.h/c       # Task struct, enums
├── migrate.h/c     # Migration runner
├── migrations.c    # Auto-generated (do not edit)
└── util.h/c        # Helpers (alloc, paths, dates)

sql/migrations/     # Versioned .sql migration files
hooks/              # Git hook scripts
scripts/            # Release automation
tests/              # Test suite
```

## Adding a Database Migration

1. Create `sql/migrations/002_your_change.sql`
2. Run `make embed` (or just `make` — it auto-regenerates)
3. The SQL is embedded in the binary at compile time
