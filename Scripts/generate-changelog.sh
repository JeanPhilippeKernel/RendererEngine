#!/usr/bin/env bash
# Generates a markdown changelog from conventional commits between two refs.
#
# Usage: generate-changelog.sh <from-ref> <to-ref> <version>
# Output: markdown string printed to stdout

set -euo pipefail

FROM_REF="${1:-}"
TO_REF="${2:-HEAD}"
VERSION="${3:-unreleased}"

if [ -z "$FROM_REF" ]; then
    COMMIT_RANGE="$TO_REF"
else
    COMMIT_RANGE="$FROM_REF..$TO_REF"
fi

declare -a BREAKING=()
declare -a FEATURES=()
declare -a FIXES=()
declare -a PERF=()
declare -a REFACTORS=()
declare -a OTHER=()

while IFS= read -r line; do
    [ -z "$line" ] && continue

    if echo "$line" | grep -qE '^(feat|fix|perf|refactor|build|ci|revert)(\(.+\))?!:' || \
       echo "$line" | grep -qE 'BREAKING CHANGE:'; then
        BREAKING+=("$line")
    elif echo "$line" | grep -qE '^feat(\(.+\))?:'; then
        FEATURES+=("$line")
    elif echo "$line" | grep -qE '^fix(\(.+\))?:'; then
        FIXES+=("$line")
    elif echo "$line" | grep -qE '^perf(\(.+\))?:'; then
        PERF+=("$line")
    elif echo "$line" | grep -qE '^refactor(\(.+\))?:'; then
        REFACTORS+=("$line")
    fi
done < <(git log "$COMMIT_RANGE" --format="%s" 2>/dev/null)

echo "## $VERSION"
echo ""
echo "$(date -u +%Y-%m-%d)"
echo ""

if [ ${#BREAKING[@]} -gt 0 ]; then
    echo "### ⚠ Breaking Changes"
    echo ""
    for entry in "${BREAKING[@]}"; do echo "* $entry"; done
    echo ""
fi

if [ ${#FEATURES[@]} -gt 0 ]; then
    echo "### Features"
    echo ""
    for entry in "${FEATURES[@]}"; do echo "* $entry"; done
    echo ""
fi

if [ ${#FIXES[@]} -gt 0 ]; then
    echo "### Bug Fixes"
    echo ""
    for entry in "${FIXES[@]}"; do echo "* $entry"; done
    echo ""
fi

if [ ${#PERF[@]} -gt 0 ]; then
    echo "### Performance"
    echo ""
    for entry in "${PERF[@]}"; do echo "* $entry"; done
    echo ""
fi

if [ ${#REFACTORS[@]} -gt 0 ]; then
    echo "### Refactors"
    echo ""
    for entry in "${REFACTORS[@]}"; do echo "* $entry"; done
    echo ""
fi
