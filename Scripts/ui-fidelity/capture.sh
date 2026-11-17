#!/usr/bin/env bash
# Launch the editor, capture its window, and quit it.
#   capture.sh <Obelisk binary> <project.json> <out.png> [settle-seconds]
# Output: <out.png> (window only, no shadow, native resolution) and <out.png>.json with the
# window's logical size. Requires macOS Screen Recording permission for the calling terminal.
set -euo pipefail
BIN="$1"; PROJECT="$2"; OUT="$3"; SETTLE="${4:-14}"
HERE="$(cd "$(dirname "$0")" && pwd)"

pkill -x Obelisk 2>/dev/null || true
# A saved dock layout overrides the code defaults; start from the defaults.
rm -f "$(dirname "$BIN")/ZodiacEngine/Settings/zui_layout.ini"
( cd "$(dirname "$BIN")" && "$BIN" --launchEditor true --projectConfigFile "$PROJECT" >/tmp/obelisk_capture.log 2>&1 & )
sleep "$SETTLE"

# Size the window so its content area is exactly the 1600x1000 Figma frame (32 px title bar on top).
# Needs a display with room for it: WIN_POS is the top-left position in AppleScript coordinates
# (default: the external 2560x1440 display to the right of the laptop screen).
WIN_POS="${WIN_POS:-1600,450}"; WIN_SIZE="${WIN_SIZE:-1600,1032}"
osascript -e "tell application \"System Events\" to tell process \"Obelisk\" to set position of window 1 to {${WIN_POS}}" \
          -e "tell application \"System Events\" to tell process \"Obelisk\" to set size of window 1 to {${WIN_SIZE}}" >/dev/null 2>&1 || echo "could not resize window (Accessibility permission?)" >&2
sleep 3

INFO="$(osascript -l JavaScript "$HERE/window-bounds.js")"
# Optional clicks ("x,y;x,y" in window-content logical px) to put the editor in a state, e.g. select an actor.
osascript -e 'tell application "System Events" to set frontmost of process "Obelisk" to true' >/dev/null 2>&1; sleep 1
if [ -n "${CLICKS:-}" ]; then
  WX="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[0]["x"])' "$INFO")"
  WY="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])[0]["y"])' "$INFO")"
  IFS=';' read -ra PTS <<< "$CLICKS"
  for pt in "${PTS[@]}"; do
    BTN=""; case "$pt" in r*) BTN="r"; pt="${pt#r}";; esac
    CX="${pt%,*}"; CY="${pt#*,}"
    osascript -l JavaScript "$HERE/click.js" "$(python3 -c "print($WX + $CX)")" "$(python3 -c "print($WY + 32 + $CY)")" $BTN >/dev/null 2>&1
    sleep 1
  done
  sleep 1
fi
ID="$(python3 -c 'import json,sys; w=json.loads(sys.argv[1]); print(w[0]["id"] if w else "")' "$INFO")"
if [ -z "$ID" ]; then echo "no Obelisk window found" >&2; pkill -x Obelisk || true; exit 1; fi
screencapture -o -x -l "$ID" "$OUT"
echo "$INFO" > "$OUT.json"
pkill -x Obelisk || true
echo "captured window $ID -> $OUT"
