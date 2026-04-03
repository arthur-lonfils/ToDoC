#!/bin/sh
# Release script for todoc.
# Usage:
#   ./scripts/release.sh          # auto-detect version bump from commits
#   ./scripts/release.sh 0.2.0    # explicit version
#
# What it does:
#   1. Validates clean working tree and current branch is main
#   2. Determines the new version (from git-cliff or argument)
#   3. Updates .version and src/cli.c
#   4. Generates CHANGELOG.md
#   5. Builds and tests to verify
#   6. Commits and tags
#   7. Pushes (tag triggers CI → GitHub Release with binary)

set -e

# ── Helpers ──────────────────────────────────────────────────────

RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
RESET='\033[0m'

info()  { printf "${GREEN}[release]${RESET} %s\n" "$1"; }
warn()  { printf "${YELLOW}[release]${RESET} %s\n" "$1"; }
error() { printf "${RED}[release]${RESET} %s\n" "$1" >&2; exit 1; }

# ── Preconditions ────────────────────────────────────────────────

if ! command -v git-cliff > /dev/null 2>&1; then
    error "git-cliff not found. Install: cargo install git-cliff"
fi

BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" != "main" ]; then
    error "Must be on main branch (currently on '$BRANCH')."
fi

if [ -n "$(git status --porcelain)" ]; then
    error "Working tree is not clean. Commit or stash changes first."
fi

# Ensure we're up to date
git fetch origin main --quiet
LOCAL=$(git rev-parse HEAD)
REMOTE=$(git rev-parse origin/main)
if [ "$LOCAL" != "$REMOTE" ]; then
    error "Local main is not up to date with origin. Run 'git pull' first."
fi

# Get current version
CURRENT_VERSION=$(cat .version 2>/dev/null | tr -d '[:space:]')
if [ -z "$CURRENT_VERSION" ]; then
    CURRENT_VERSION="0.0.0"
fi

info "Current version: v$CURRENT_VERSION"

# ── Determine new version ───────────────────────────────────────

if [ -n "$1" ]; then
    NEW_VERSION="$1"
    info "Using explicit version: v$NEW_VERSION"
else
    NEW_VERSION=$(git-cliff --bumped-version 2>/dev/null | sed 's/^v//')
    if [ -z "$NEW_VERSION" ] || [ "$NEW_VERSION" = "$CURRENT_VERSION" ]; then
        error "No version bump detected from commits. Use conventional commits or pass an explicit version."
    fi
    info "Auto-detected version: v$NEW_VERSION"
fi

# Validate semver format
if ! echo "$NEW_VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    error "Invalid version format: '$NEW_VERSION'. Expected: X.Y.Z"
fi

# Check tag doesn't already exist
if git rev-parse "v$NEW_VERSION" >/dev/null 2>&1; then
    error "Tag v$NEW_VERSION already exists."
fi

# ── Update version references ───────────────────────────────────

info "Updating .version..."
printf "%s\n" "$NEW_VERSION" > .version

info "Updating src/cli.h..."
sed -i "s/#define TODOC_VERSION \".*\"/#define TODOC_VERSION \"$NEW_VERSION\"/" src/cli.h

# Verify substitution worked
if ! grep -q "\"$NEW_VERSION\"" src/cli.h; then
    error "Failed to update TODOC_VERSION in src/cli.h"
fi

# ── Generate changelog ───────────────────────────────────────────

info "Generating CHANGELOG.md..."
git-cliff --tag "v$NEW_VERSION" --output CHANGELOG.md

# ── Build and test ───────────────────────────────────────────────

info "Building..."
make clean
make

info "Testing..."
make test

# Verify binary version
BINARY_VERSION=$(./build/todoc version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
if [ "$BINARY_VERSION" != "$NEW_VERSION" ]; then
    error "Binary reports v$BINARY_VERSION but expected v$NEW_VERSION"
fi
info "Binary version verified: v$NEW_VERSION"

# ── Commit, tag, push ───────────────────────────────────────────

info "Committing..."
git add .version src/cli.h CHANGELOG.md
git commit -m "chore: release v$NEW_VERSION"

info "Tagging v$NEW_VERSION..."
git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION"

info "Pushing to origin..."
git push origin main --tags

# ── Done ─────────────────────────────────────────────────────────

echo ""
info "Released v$NEW_VERSION"
info "CI will now build the binary and create the GitHub Release."
info "Track progress: https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]//;s/.git$//')/actions"
echo ""
