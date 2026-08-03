#!/usr/bin/env bash
# Generates a markdown changelog from conventional commits between two refs.
# When GITHUB_TOKEN and GITHUB_REPOSITORY are set, commit authors are resolved
# to GitHub usernames via the API. Otherwise falls back to git author name.
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

# Resolve a commit SHA to a GitHub username, falling back to git author name.
github_username() {
    local sha="$1"
    local fallback="$2"
    if [ -n "${GITHUB_TOKEN:-}" ] && [ -n "${GITHUB_REPOSITORY:-}" ]; then
        local username
        username="$(curl -sf \
            -H "Authorization: Bearer $GITHUB_TOKEN" \
            -H "Accept: application/vnd.github+json" \
            "https://api.github.com/repos/${GITHUB_REPOSITORY}/commits/${sha}" \
            | jq -r '.author.login // empty' 2>/dev/null || true)"
        if [ -n "$username" ]; then
            echo "@${username}"
            return
        fi
    fi
    echo "$fallback"
}

declare -a BREAKING=()
declare -a FEATURES=()
declare -a FIXES=()
declare -a PERF=()
declare -a REFACTORS=()

while IFS='|' read -r sha subject author; do
    [ -z "$subject" ] && continue
    contributor="$(github_username "$sha" "$author")"
    entry="$subject ($contributor)"

    if echo "$subject" | grep -qE '^(feat|fix|perf|refactor|build|ci|revert)(\(.+\))?!:' || \
       echo "$subject" | grep -qE 'BREAKING CHANGE:'; then
        BREAKING+=("$entry")
    elif echo "$subject" | grep -qE '^feat(\(.+\))?:'; then
        FEATURES+=("$entry")
    elif echo "$subject" | grep -qE '^fix(\(.+\))?:'; then
        FIXES+=("$entry")
    elif echo "$subject" | grep -qE '^perf(\(.+\))?:'; then
        PERF+=("$entry")
    elif echo "$subject" | grep -qE '^refactor(\(.+\))?:'; then
        REFACTORS+=("$entry")
    fi
done < <(git log "$COMMIT_RANGE" --format="%H|%s|%an" 2>/dev/null)

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
