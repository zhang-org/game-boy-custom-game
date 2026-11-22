#!/usr/bin/env bash
# Runs on every prompt. It does a quick fetch and only prints something when origin/main has moved
# since the agent last heard about it. It never changes the working tree.
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
git fetch --quiet origin main 2>/dev/null || exit 0

marker="$(git rev-parse --git-dir)/claude-seen-main"
now=$(git rev-parse --verify --quiet origin/main) || exit 0
seen=$(cat "$marker" 2>/dev/null)
[ "$now" = "$seen" ] && exit 0

echo "=== Heads-up: origin/main has new commits (probably a merged PR) ==="
if [ -n "$seen" ] && git cat-file -e "$seen" 2>/dev/null; then
  git log --oneline --no-decorate "$seen..$now" | head -15
else
  git log --oneline --no-decorate -5 "$now"
fi
echo "Before starting new work: read these changes, then rebase your branch onto origin/main (CLAUDE.md section 1)."
echo "If you're in the middle of a task, finish the current step first."
echo "$now" > "$marker"
exit 0
