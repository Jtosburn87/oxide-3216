#!/usr/bin/env bash
# Pulls new commits from GitHub and rebuilds the Linux version when there are any.
# Run by the oxide-3216-update systemd user timer; safe to run by hand too.
#   ./update-linux.sh          only if GitHub has something new
#   ./update-linux.sh --force  rebuild anyway
set -uo pipefail
cd "$(dirname "$0")"
log=build-linux/update.log
mkdir -p build-linux

notify() { notify-send -a Oxide-3216 -i oxide-3216 "$1" "$2" 2>/dev/null || true; echo "$1: $2"; }

git fetch --quiet origin main || { echo "fetch failed (offline?)"; exit 0; }
if [[ "${1:-}" != "--force" && "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]]; then
    exit 0
fi

# Never overwrite work done on this PC: stop if there are uncommitted or unpushed changes.
if ! git diff --quiet || ! git diff --cached --quiet; then
    notify "Linux update skipped" "There are uncommitted changes in ~/Documents/tapeconsole."
    exit 1
fi
if ! git merge --ff-only --quiet origin/main; then
    notify "Linux update skipped" "This PC has commits that aren't on GitHub. Push or merge them first."
    exit 1
fi

version="$(git log -1 --format=%s)"
# If compiling fails the link step never runs, so the previous app stays in place.
if ./build.sh Release > "$log" 2>&1; then
    notify "Oxide-3216 updated" "$version"
else
    notify "Linux build FAILED" "$version. See build-linux/update.log"
    exit 1
fi
