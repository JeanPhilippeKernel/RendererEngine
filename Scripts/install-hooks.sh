#!/usr/bin/env sh
# Installs project git hooks from Scripts/hooks/ into .git/hooks/.
# Run once after cloning: sh Scripts/install-hooks.sh

REPO_ROOT="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
HOOKS_SRC="$REPO_ROOT/Scripts/hooks"
HOOKS_DST="$REPO_ROOT/.git/hooks"

for hook in "$HOOKS_SRC"/*; do
    name="$(basename "$hook")"
    dst="$HOOKS_DST/$name"

    if [ -e "$dst" ] && [ ! -L "$dst" ]; then
        echo "Backing up existing hook: $dst -> $dst.bak"
        mv "$dst" "$dst.bak"
    fi

    ln -sf "$hook" "$dst"
    chmod +x "$hook"
    echo "Installed: $name"
done

echo "Git hooks installed successfully."
