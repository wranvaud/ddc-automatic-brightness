#!/usr/bin/env bash
# Bump the app version, rebuild, and create the matching commit + git tag.
#
# Keeps APP_VERSION (src/main.c) and PKG_VERSION (src/Makefile) in sync in a
# single step - these previously drifted apart when a tag was cut by hand
# without updating both strings (see: v1.1.2 shipping with APP_VERSION 1.1.1).
#
# This script does NOT push or publish anything. It only creates a local
# commit and a local annotated tag; pushing/releasing is a separate step you
# run yourself.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

MAIN_C="src/main.c"
MAKEFILE="src/Makefile"

fail() { echo "Error: $*" >&2; exit 1; }

# --- Preconditions ---------------------------------------------------------

command -v git >/dev/null || fail "git not found"

if [ -n "$(git status --porcelain)" ]; then
    fail "working tree is not clean. Commit or stash your changes first, so the version bump is its own isolated commit."
fi

CURRENT_APP_VERSION=$(grep -oP '(?<=#define APP_VERSION ")[^"]+' "$MAIN_C") || fail "could not read APP_VERSION from $MAIN_C"
CURRENT_PKG_VERSION=$(grep -oP '(?<=^PKG_VERSION = ).+' "$MAKEFILE") || fail "could not read PKG_VERSION from $MAKEFILE"

if [ "$CURRENT_APP_VERSION" != "$CURRENT_PKG_VERSION" ]; then
    fail "APP_VERSION ($CURRENT_APP_VERSION) and PKG_VERSION ($CURRENT_PKG_VERSION) are already out of sync. Fix that by hand before bumping further."
fi

CURRENT_VERSION="$CURRENT_APP_VERSION"

if ! [[ "$CURRENT_VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    fail "current version '$CURRENT_VERSION' isn't in X.Y.Z form; bump it manually this once."
fi
MAJOR="${BASH_REMATCH[1]}"; MINOR="${BASH_REMATCH[2]}"; PATCH="${BASH_REMATCH[3]}"
SUGGESTED_VERSION="${MAJOR}.${MINOR}.$((PATCH + 1))"

# --- Ask for the target version ---------------------------------------------

echo "Current version: $CURRENT_VERSION"
read -r -p "New version [$SUGGESTED_VERSION]: " NEW_VERSION
NEW_VERSION="${NEW_VERSION:-$SUGGESTED_VERSION}"

if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    fail "'$NEW_VERSION' isn't a valid X.Y.Z version"
fi
if [ "$NEW_VERSION" = "$CURRENT_VERSION" ]; then
    fail "new version is the same as the current version ($CURRENT_VERSION)"
fi
if git rev-parse "v$NEW_VERSION" >/dev/null 2>&1; then
    fail "tag v$NEW_VERSION already exists"
fi

echo
echo "This will:"
echo "  - set APP_VERSION/PKG_VERSION to $NEW_VERSION"
echo "  - rebuild the app to confirm it compiles"
echo "  - commit both files (\"Bump version to $NEW_VERSION\")"
echo "  - create annotated tag v$NEW_VERSION"
echo "Nothing is pushed or published - that's a separate step."
read -r -p "Proceed? [y/N] " CONFIRM
case "$CONFIRM" in
    y|Y|yes|YES) ;;
    *) echo "Aborted."; exit 1 ;;
esac

# --- Apply the bump ----------------------------------------------------------

sed -i "s/#define APP_VERSION \"$CURRENT_VERSION\"/#define APP_VERSION \"$NEW_VERSION\"/" "$MAIN_C"
sed -i "s/^PKG_VERSION = $CURRENT_VERSION\$/PKG_VERSION = $NEW_VERSION/" "$MAKEFILE"

echo "Rebuilding..."
if ! (cd src && make clean >/dev/null && make); then
    echo "Build failed - reverting version bump." >&2
    git checkout -- "$MAIN_C" "$MAKEFILE"
    exit 1
fi

git add "$MAIN_C" "$MAKEFILE"
git commit -m "Bump version to $NEW_VERSION"
git tag -a "v$NEW_VERSION" -m "Version $NEW_VERSION"

echo
echo "Done. Created commit and tag v$NEW_VERSION locally."
echo "Next steps (not done by this script):"
echo "  git push origin main v$NEW_VERSION"
echo "  cd src && make package-deb   # or whichever package targets you need"
echo "  upload the package to a new GitHub release"
