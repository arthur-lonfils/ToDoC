# todoc — Project Notes for Claude

A CLI task manager in C with SQLite storage. Strict compilation
(`-Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes`).

## Build & test

```sh
make             # build → ./build/todoc
make clean       # wipe build/
make format      # clang-format all sources (run before committing)
make format-check
make test        # full test suite (tests/run.sh)
make test-valgrind
make embed       # regenerate src/migrations.c from sql/migrations/*.sql
```

After adding a new file to `sql/migrations/`, run `make embed` so it
gets baked into the binary.

## Branch naming (CI-enforced)

Pattern: `^(story|defect)/[a-z0-9][a-z0-9-]+$`

- `story/<desc>`  — features, refactors, docs, tests
- `defect/<desc>` — bug fixes, security patches

Lowercase with hyphens. **Do not** use `feature/...` — CI will fail.

## Commit messages (CI-enforced via commit-lint)

Conventional commits: `^(type)(\(scope\))?!?: subject`

- **types:** `feat`, `fix`, `refactor`, `perf`, `test`, `chore`,
  `docs`, `ci`, `build`
- **scopes (optional):** `db`, `cli`, `model`, `display`, `export`,
  `migrate`, `util`, `deps`

The release script auto-detects the version bump from these commit
types via `git-cliff`, so use them correctly (`feat:` → minor,
`fix:` → patch, `!` or `BREAKING CHANGE` → major).

## Pre-commit checklist

1. `make format` — CI's `format-check` will fail otherwise
2. `make test` — keep the suite green
3. Conventional commit message
4. Branch name matches `story/...` or `defect/...`

## Release flow

Releases are fully automated by `scripts/release.sh`:

```sh
git checkout main && git pull
make release            # auto-detects version from commits
# or
make release 0.4.0      # explicit version (script takes positional arg)
```

The script:
1. Verifies clean tree, on `main`, up to date with `origin/main`
2. Determines new version (`git-cliff --bumped-version`)
3. Updates `.version` and `TODOC_VERSION` in `src/cli.h`
4. Regenerates `CHANGELOG.md` via `git-cliff`
5. `make clean && make && make test`
6. Verifies the built binary reports the expected version
7. Commits `chore: release vX.Y.Z`, tags `vX.Y.Z`, `git push --tags`
8. Tag push triggers `release.yml` → builds binary, publishes
   GitHub Release

**There is no manual version bump step.** Don't edit `.version` or
`TODOC_VERSION` by hand — the release script owns those.

Requires `git-cliff` installed (`cargo install git-cliff`).

## Source layout

- `src/cli.{c,h}` — argument parsing, command enum, usage text
- `src/commands.{c,h}` — handlers (one per subcommand)
- `src/db.{c,h}` — SQLite CRUD, prepared statements, filters
- `src/display.{c,h}` — ANSI-colored terminal output
- `src/export.c` — CSV/JSON export
- `src/migrate.c`, `src/migrations.c` — migration runner + embedded SQL
- `src/model.{c,h}` — structs, enums, free functions
- `src/util.{c,h}` — paths, active project file, helpers
- `src/main.c` — dispatch table, top-level flow
- `sql/migrations/NNN_*.sql` — schema changes (run `make embed` after
  adding)
- `tests/run.sh` — POSIX sh test suite (`assert_ok`, `assert_fail`,
  `assert_output`)

## Coding conventions

- Prepared statements everywhere; never interpolate SQL strings
- Zero-initialize structs (`task_t t = {0};`); each struct with heap
  fields has a matching `*_free()` function
- Dynamic SQL WHERE built with a buffer + bind index counter
- Enum ↔ string conversion functions in `model.c` with lookup tables
- All public display functions respect `NO_COLOR` and TTY detection
- No new files unless necessary; prefer editing existing modules
