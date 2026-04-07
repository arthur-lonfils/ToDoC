#!/bin/sh
# todoc installer / updater
# Usage:
#   curl -sSL https://raw.githubusercontent.com/arthur-lonfils/ToDoC/main/scripts/install.sh | sh
#   ./scripts/install.sh                # install latest
#   ./scripts/install.sh v0.4.0         # install a specific version
#   PREFIX=/usr/local ./scripts/install.sh   # install system-wide (needs sudo)
#
# Re-running this script updates todoc to the latest release.

set -e

REPO="arthur-lonfils/ToDoC"
PREFIX="${PREFIX:-$HOME/.local}"
BIN_DIR="$PREFIX/bin"

# ── Helpers ──────────────────────────────────────────────────────

RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
RESET='\033[0m'

info()  { printf "${GREEN}[install]${RESET} %s\n" "$1"; }
warn()  { printf "${YELLOW}[install]${RESET} %s\n" "$1"; }
error() { printf "${RED}[install]${RESET} %s\n" "$1" >&2; exit 1; }

# ── Detect downloader ────────────────────────────────────────────

if command -v curl > /dev/null 2>&1; then
    DOWNLOAD="curl -fsSL"
    DOWNLOAD_OUT="curl -fsSL -o"
elif command -v wget > /dev/null 2>&1; then
    DOWNLOAD="wget -qO-"
    DOWNLOAD_OUT="wget -qO"
else
    error "Neither curl nor wget is installed."
fi

# ── Detect platform ──────────────────────────────────────────────

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

case "$OS-$ARCH" in
    linux-x86_64|linux-amd64)
        ASSET="todoc-linux-x86_64"
        ;;
    *)
        error "Unsupported platform: $OS-$ARCH (only linux-x86_64 is published today)."
        ;;
esac

# ── Determine version ────────────────────────────────────────────

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    info "Looking up latest release..."
    # Parse "tag_name": "vX.Y.Z" from the GitHub API without jq.
    VERSION=$($DOWNLOAD "https://api.github.com/repos/$REPO/releases/latest" \
              | sed -n 's/.*"tag_name":[[:space:]]*"\([^"]*\)".*/\1/p' \
              | head -n1)
    if [ -z "$VERSION" ]; then
        error "Could not determine latest release. Check your network or pass a version explicitly."
    fi
fi

# Normalise to "vX.Y.Z"
case "$VERSION" in
    v*) ;;
    *) VERSION="v$VERSION" ;;
esac

info "Target version: $VERSION"

# ── Skip if already installed ────────────────────────────────────

if [ -x "$BIN_DIR/todoc" ]; then
    CURRENT=$("$BIN_DIR/todoc" version 2>/dev/null | awk '{print $2}')
    if [ "v$CURRENT" = "$VERSION" ]; then
        info "todoc $VERSION is already installed at $BIN_DIR/todoc"
        exit 0
    fi
    info "Updating todoc v$CURRENT → $VERSION"
fi

# ── Download ─────────────────────────────────────────────────────

URL="https://github.com/$REPO/releases/download/$VERSION/$ASSET"
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

info "Downloading $ASSET..."
$DOWNLOAD_OUT "$TMP" "$URL" || error "Download failed: $URL"

# Sanity check the download
if [ ! -s "$TMP" ]; then
    error "Downloaded file is empty."
fi

# ── Install ──────────────────────────────────────────────────────

mkdir -p "$BIN_DIR"
chmod +x "$TMP"

# Atomic replace (works even if todoc is currently running on Linux)
mv "$TMP" "$BIN_DIR/todoc"
trap - EXIT

info "Installed to $BIN_DIR/todoc"

# ── Verify ───────────────────────────────────────────────────────

INSTALLED=$("$BIN_DIR/todoc" version 2>&1)
info "$INSTALLED"

# ── Backup existing database ─────────────────────────────────────

DB="$HOME/.todoc/todoc.db"
BACKUP=""
if [ -f "$DB" ]; then
    BACKUP="$DB.backup-$(date +%Y%m%d-%H%M%S)"
    cp "$DB" "$BACKUP"
    info "Backed up existing database to $BACKUP"
fi

# ── Apply pending migrations ─────────────────────────────────────

info "Applying database migrations (todoc init)..."
if "$BIN_DIR/todoc" init > /dev/null 2>&1; then
    info "Database is up to date."
else
    warn "todoc init failed. Run it manually to see the error."
    if [ -n "$BACKUP" ]; then
        warn "Your backup is at: $BACKUP"
    fi
fi

# ── Shell completion ─────────────────────────────────────────────
#
# When running via curl ... | sh, stdin is not a TTY, so todoc init's
# interactive prompt would silently skip itself. We auto-install the
# completion file from here instead, taking "yes" as the default — but
# we respect the no_completion marker that the user can drop to opt
# out persistently.

if [ -f "$HOME/.todoc/no_completion" ]; then
    info "Skipping shell completion (you opted out previously)."
elif [ -z "${SHELL:-}" ]; then
    warn "\$SHELL is unset; skipping shell completion."
    warn "Run 'todoc completions install' from a terminal to add it."
else
    if "$BIN_DIR/todoc" completions install > /dev/null 2>&1; then
        info "Installed shell completion for $(basename "$SHELL")."
        info "Restart your shell, or source the new completion file."
    else
        warn "Could not install shell completion automatically."
        warn "Run 'todoc completions install' from a terminal to add it."
    fi
fi

# ── PATH hint ────────────────────────────────────────────────────

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        warn "$BIN_DIR is not on your PATH."
        warn "Add this to your shell profile:"
        warn "  export PATH=\"$BIN_DIR:\$PATH\""
        ;;
esac

echo ""
info "Done."
if [ -n "$BACKUP" ]; then
    info "Database backup: $BACKUP"
fi
