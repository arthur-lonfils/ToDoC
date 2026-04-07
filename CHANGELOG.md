# Changelog

All notable changes to **todoc** are documented here.

Format based on [Conventional Commits](https://conventionalcommits.org).

## [0.5.0] — 2026-04-07

### Documentation

- Document projects feature and add CLAUDE.md(f9ac0f9)

### Features

- **cli:** Add topic-based help (todoc help <topic>)(eb3b23a)
- **cli:** Add 'todoc update' subcommand with auto backup and migrate(8ba2ca5)
- Add subtasks, move command, and abandoned status(39dbe4a)
- Add labels (many-to-many cross-cutting tags)(bf5960e)

## [0.4.0] — 2026-04-07

### Bug Fixes

- Read version from .version in tests instead of hardcoding(c989ccf)

### Features

- Add projects for grouping and scoping tasks(429f19f)
- Add install/update script and document one-line install(4b3e8e7)

## [0.3.0] — 2026-04-03

### Bug Fixes

- **cli:** Use TODOC_VERSION macro for version output(87eb31b)

### CI/CD

- Add versioning, changelog, hooks, and contrib guide(fdbad0b)
- Add GitHub Actions for CI and auto-release(d6c5f66)
- Fix git-cliff download URL in release workflow(4561a5c)
- Use official git-cliff action instead of curl(ce90986)
- Install git-cliff via pip instead of action(10ff203)
- Checkout main explicitly in publish job (#4)(7efebac)
- Use merge_commit_sha for publish checkout(0ff2c78)
- Simplify release to tag-triggered workflow(a4379db)
- Check TODOC_VERSION in cli.h instead of cli.c(a0f7e9d)

### Documentation

- Regenerate changelog with conventional commits(f7463a6)
- Add README and MIT license(0db9c59)
- Update changelog(8387b90)

### Features

- **cli:** Add export command with CSV and JSON(f48810c)


