#!/usr/bin/env bash
# Computes the next rc version from conventional commits since the last tag,
# writes VERSION.txt, and prints the new version to stdout.
#
# Usage: bump-prerelease.sh
# Output: the new version string, e.g. 0.3.0-rc.2
# Exit 0: version bumped; exit 2: no releasable commits found

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
VERSION_FILE="$REPO_ROOT/VERSION.txt"

# Read current version
CURRENT="$(cat "$VERSION_FILE" | tr -d '[:space:]')"

# Strip any existing prerelease suffix to get the base semver
BASE="$(echo "$CURRENT" | sed 's/-.*$//')"
MAJOR="$(echo "$BASE" | cut -d. -f1)"
MINOR="$(echo "$BASE" | cut -d. -f2)"
PATCH="$(echo "$BASE" | cut -d. -f3)"

# Extract current rc counter (0 if not an rc version)
if echo "$CURRENT" | grep -qE '\-rc\.([0-9]+)$'; then
    ALPHA_N="$(echo "$CURRENT" | sed 's/.*-rc\.//')"
else
    ALPHA_N=0
fi

# Find the last tag on this branch to scope commit search
LAST_TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo "")"

if [ -n "$LAST_TAG" ]; then
    COMMIT_RANGE="$LAST_TAG..HEAD"
else
    COMMIT_RANGE="HEAD"
fi

# Analyse commits since the last tag
HAS_BREAKING=0
HAS_FEAT=0
HAS_FIX=0

while IFS= read -r line; do
    if echo "$line" | grep -qE '^(feat|fix|perf|refactor|build|ci|revert)(\(.+\))?!:'; then
        HAS_BREAKING=1
    elif echo "$line" | grep -qE 'BREAKING CHANGE:'; then
        HAS_BREAKING=1
    elif echo "$line" | grep -qE '^feat(\(.+\))?:'; then
        HAS_FEAT=1
    elif echo "$line" | grep -qE '^(fix|perf|refactor|revert)(\(.+\))?:'; then
        HAS_FIX=1
    fi
done < <(git log "$COMMIT_RANGE" --format="%s" 2>/dev/null)

# Determine bump type and compute next version
if [ $HAS_BREAKING -eq 1 ]; then
    MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0; ALPHA_N=0
elif [ $HAS_FEAT -eq 1 ]; then
    MINOR=$((MINOR + 1)); PATCH=0; ALPHA_N=0
elif [ $HAS_FIX -eq 1 ]; then
    PATCH=$((PATCH + 1)); ALPHA_N=0
else
    # No releasable commits
    exit 2
fi

ALPHA_N=$((ALPHA_N + 1))
NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}-rc.${ALPHA_N}"

printf '%s\n' "$NEW_VERSION" > "$VERSION_FILE"
echo "$NEW_VERSION"
