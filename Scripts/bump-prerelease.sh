#!/usr/bin/env bash
# Computes the next rc version from conventional commits since the last tag
# and prints it to stdout. Does not modify any files.
#
# Usage: bump-prerelease.sh
# Output: the new version string, e.g. 0.3.0-rc.2
# Exit 0: version bumped; exit 2: no releasable commits found

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
VERSION_FILE="$REPO_ROOT/VERSION.txt"

LAST_TAG="$(git describe --tags --abbrev=0 2>/dev/null || echo "")"
LAST_STABLE_TAG="$(git describe --tags --abbrev=0 --exclude '*-rc.*' 2>/dev/null || echo "")"

# Sets HAS_BREAKING, HAS_FEAT, HAS_FIX from commits in the given range.
scan_commits() {
    local range="$1"
    HAS_BREAKING=0; HAS_FEAT=0; HAS_FIX=0
    while IFS= read -r line; do
        if echo "$line" | grep -qE '^(feat|fix|perf|refactor|build|ci|revert)(\(.+\))?!:'; then
            HAS_BREAKING=1
        elif echo "$line" | grep -qE '^feat(\(.+\))?:'; then
            HAS_FEAT=1
        elif echo "$line" | grep -qE '^(fix|perf|refactor|revert)(\(.+\))?:'; then
            HAS_FIX=1
        fi
    done < <(git log "$range" --format="%s" 2>/dev/null)
    # Also scan commit bodies for the BREAKING CHANGE footer (conventional commits spec)
    if git log "$range" --format="%b" 2>/dev/null | grep -qE '^BREAKING CHANGE:'; then
        HAS_BREAKING=1
    fi
}

# 1. Any new releasable commits since the last tag (rc or stable)?
#    If not, there is nothing to release.
scan_commits "${LAST_TAG:+${LAST_TAG}..}HEAD"
if [ $HAS_BREAKING -eq 0 ] && [ $HAS_FEAT -eq 0 ] && [ $HAS_FIX -eq 0 ]; then
    exit 2
fi

# 2. Determine the correct next stable version from ALL commits since the last
#    stable release. This ensures a breaking change added after an rc tag
#    (e.g. v0.3.0-rc.1) produces 1.0.0-rc.1, not 0.3.0-rc.2.
scan_commits "${LAST_STABLE_TAG:+${LAST_STABLE_TAG}..}HEAD"

# Prefer the stable tag as the base — it stays correct even when VERSION.txt
# on develop lags behind main after a stable release.
if [ -n "$LAST_STABLE_TAG" ]; then
    BASE="$(echo "$LAST_STABLE_TAG" | sed 's/^v//')"
else
    BASE="$(tr -d '[:space:]' < "$VERSION_FILE" | sed 's/-.*$//')"
fi
MAJOR="$(echo "$BASE" | cut -d. -f1)"
MINOR="$(echo "$BASE" | cut -d. -f2)"
PATCH="$(echo "$BASE" | cut -d. -f3)"

if [ $HAS_BREAKING -eq 1 ]; then
    MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0
elif [ $HAS_FEAT -eq 1 ]; then
    MINOR=$((MINOR + 1)); PATCH=0
else
    PATCH=$((PATCH + 1))
fi
NEXT_VERSION="${MAJOR}.${MINOR}.${PATCH}"

# 3. If we are already in an RC series for this exact version, increment the
#    counter. Otherwise start a fresh series at rc.1.
RC_BASE=""
RC_N=0
if echo "$LAST_TAG" | grep -qE '^v[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]+$'; then
    RC_BASE="$(echo "$LAST_TAG" | sed 's/^v//' | sed 's/-rc\.[0-9]*$//')"
    RC_N="$(echo "$LAST_TAG" | sed 's/.*-rc\.//')"
fi

if [ "$RC_BASE" = "$NEXT_VERSION" ]; then
    echo "${NEXT_VERSION}-rc.$((RC_N + 1))"
else
    echo "${NEXT_VERSION}-rc.1"
fi
