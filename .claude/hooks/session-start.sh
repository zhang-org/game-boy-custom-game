#!/usr/bin/env bash
# Runs at the start of every Claude session. What it prints is given to the agent as context.
# It fetches, and fast-forwards main if you're on main with no uncommitted changes. It never changes a feature branch's files.
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

git fetch --quiet --prune origin 2>/dev/null || echo "(git fetch failed: offline? The report below may be out of date.)"

branch=$(git branch --show-current)
echo "=== Repo sync report (read CLAUDE.md section 1 and act on this) ==="
echo "Current branch: ${branch:-detached}"
if [ -n "$branch" ] && git rev-parse --verify --quiet origin/main >/dev/null; then
  read -r behind ahead < <(git rev-list --left-right --count origin/main...HEAD)
  echo "Compared with origin/main: $ahead ahead, $behind behind"
  if [ "$behind" -gt 0 ]; then
    echo "New on main since this branch started:"
    git log --oneline --no-decorate HEAD..origin/main | head -15
  fi
fi
. "$(dirname "$0")/update-main.sh"
update_local_main
dirty=$(git status --porcelain | head -10)
[ -n "$dirty" ] && { echo "Uncommitted changes:"; echo "$dirty"; }

if command -v gh >/dev/null 2>&1; then
  echo
  echo "Signed in to GitHub as: $(gh api user --jq .login 2>/dev/null || echo '?')"
  echo "Open PRs (these are claims; don't edit files another person's PR is changing):"
  gh pr list --limit 20 --json number,title,author,headRefName,isDraft \
    --template '{{range .}}  #{{.number}} {{if .isDraft}}[draft] {{end}}{{.title}} ({{.author.login}}, {{.headRefName}}){{"\n"}}{{else}}  (none){{"\n"}}{{end}}' 2>/dev/null
  echo "Open issues (heads-ups and ideas):"
  gh issue list --limit 20 --json number,title,author \
    --template '{{range .}}  #{{.number}} {{.title}} ({{.author.login}}){{"\n"}}{{else}}  (none){{"\n"}}{{end}}' 2>/dev/null
else
  echo "(gh CLI isn't installed, so PRs and issues can't be listed. Ask your human to install it.)"
fi

# Record main as seen, so the per-prompt check only reports changes made after this point
git rev-parse --verify --quiet origin/main > "$(git rev-parse --git-dir)/claude-seen-main" 2>/dev/null
exit 0
