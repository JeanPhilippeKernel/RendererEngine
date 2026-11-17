#!/usr/bin/env bash
# One-shot: capture the editor and compare it with the Figma frame.
#   run.sh <build dir, e.g. Result.Darwin.arm64.Debug> <tag> [python]
# Writes /tmp/fid/<tag>.png and /tmp/fid/out-<tag>/ (see compare.py / zoom.py).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:?build dir}"; TAG="${2:?tag}"; PY="${3:-/tmp/fidenv/bin/python}"
mkdir -p /tmp/fid
"$ROOT/Scripts/ui-fidelity/capture.sh" "$ROOT/$BUILD/Obelisk/Debug/Obelisk" "$ROOT/SampleProject/project.json" "/tmp/fid/$TAG.png" 14 2>&1 | grep -v Terminated || true
"$PY" "$ROOT/Scripts/ui-fidelity/compare.py" "/tmp/fid/$TAG.png" "/tmp/fid/out-$TAG"
