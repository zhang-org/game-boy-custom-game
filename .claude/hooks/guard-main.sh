#!/usr/bin/env bash
# PreToolUse hook for Bash. It blocks commits to main and pushes to main.
# Exiting with code 2 blocks the command, and the agent sees the message on stderr.
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
input=$(cat)
# Use jq to get tool_input.command if it's installed. Windows Git Bash often doesn't have jq, so fall back
# to scanning the raw JSON. That can only cause an extra block, never a missed one.
if command -v jq >/dev/null 2>&1; then
  cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty')
else
  cmd=$input
fi
[ -z "$cmd" ] && exit 0

block() { echo "Blocked by .claude/hooks/guard-main.sh: $1 Work on a branch and open a PR (CLAUDE.md section 2)." >&2; exit 2; }
branch=$(git branch --show-current 2>/dev/null)

if printf '%s' "$cmd" | grep -Eq '(^|[;&|"[:space:]])git[[:space:]]+(-C[[:space:]]+[^[:space:]]+[[:space:]]+)?commit\b'; then
  [ "$branch" = "main" ] && block "you're on main."
fi
if printf '%s' "$cmd" | grep -Eq '(^|[;&|"[:space:]])git[[:space:]]+(-C[[:space:]]+[^[:space:]]+[[:space:]]+)?push\b'; then
  # Keep only the push command's own arguments, cut off at the next ; & | newline or quote
  # (e.g. in `git push && gh pr create --base main` the `--base main` isn't a push).
  push_part=$(printf '%s' "$cmd" | tr '\n' ';' | sed -E 's/\\n/;/g; s/.*git[[:space:]]+(-C[[:space:]]+[^[:space:]]+[[:space:]]+)?push//; s/[;&|"].*//')
  if printf '%s' "$push_part" | grep -Eq '(^|[[:space:]:+])main([[:space:]"]|$|;|&)'; then
    block "that pushes to main."
  fi
  # A bare `git push` while on main
  if [ "$branch" = "main" ] && ! printf '%s' "$push_part" | grep -Eq '[[:space:]][^-[:space:]]+[[:space:]]+[^-[:space:]]+'; then
    block "you're on main."
  fi
fi
exit 0
