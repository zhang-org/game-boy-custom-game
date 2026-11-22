#!/usr/bin/env bash
# Shared by session-start.sh and check-main.sh. Run it after `git fetch`.
# Keeps the local `main` matching origin/main without anyone running `git pull`.
# Safe by design: it only fast-forwards, and it never touches uncommitted changes or a feature branch's files.
update_local_main() {
  local branch
  branch=$(git branch --show-current)
  if [ "$branch" = "main" ]; then
    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
      echo "Local main NOT updated: there are uncommitted changes on main. Move them to a branch first (CLAUDE.md section 2)."
    elif git merge --ff-only --quiet origin/main 2>/dev/null; then
      echo "Local main is up to date with origin/main (updated automatically)."
    else
      echo "Local main NOT updated: it has commits that aren't on origin/main. Tell your human."
    fi
  else
    # Moves the local main ref forward without checking it out. Git refuses if it isn't a fast-forward.
    git fetch --quiet origin main:main 2>/dev/null
    if [ -n "$branch" ] && [ "$(git rev-list --count HEAD..origin/main 2>/dev/null)" != "0" ]; then
      echo "Branch '$branch' is behind origin/main. When you're between steps, run: git rebase origin/main"
    fi
  fi
}
