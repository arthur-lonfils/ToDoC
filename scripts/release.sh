#!/bin/sh
# Release script for todoc.
# Usage:
#   ./scripts/release.sh          # auto-detect version bump from commits
#   ./scripts/release.sh 0.2.0    # explicit version
#
# What it does:
#   1. Validates clean working tree
#   2. Determines the new version (from git-cliff or argument)
#   3. Updates .version file
#   4. Updates TODOC_VERSION in src/cli.c
#   5. Generates CHANGELOG.md
#   6. Commits, tags, and optionally pushes

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

# Check clean working tree
if [ -n "$(git status --porcelain)" ]; then
    error "Working tree is not clean. Commit or stash changes first."
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

# ── Update version references ───────────────────────────────────

info "Updating .version..."
printf "%s\n" "$NEW_VERSION" > .version

info "Updating src/cli.c..."
sed -i "s/#define TODOC_VERSION \".*\"/#define TODOC_VERSION \"$NEW_VERSION\"/" src/cli.c

# ── Generate changelog ───────────────────────────────────────────

info "Generating CHANGELOG.md..."
git-cliff --tag "v$NEW_VERSION" --output CHANGELOG.md

# ── Commit and tag ───────────────────────────────────────────────

info "Committing..."
git add .version src/cli.c CHANGELOG.md
git commit -m "chore: release v$NEW_VERSION"

info "Tagging v$NEW_VERSION..."
git tag -a "v$NEW_VERSION" -m "Release v$NEW_VERSION"

# ── Done ─────────────────────────────────────────────────────────

echo ""
info "Released v$NEW_VERSION"
info ""
info "Next steps:"
info "  git push origin main --tags"
echo ""
