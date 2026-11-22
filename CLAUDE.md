# Game Boy mini games: rules for Claude agents

Two brothers, Adam and Charlie, work on this repo, each with their own Claude Code agent, on separate computers and GitHub accounts:

| Human | GitHub | Computer | Branch prefix |
|---|---|---|---|
| Adam | `adamzhang06` | Mac | `adam/` |
| Charlie | `Babelfisher-txz` | Windows | `charlie/` |

You are one of those agents. The other agent can't see your conversation. It only sees what reaches GitHub: commits, PRs, PR comments and issues. Everything in this file exists so the two of you never overwrite each other's work and always know what the other one is doing.

What we're making: a collection of Game Boy Color mini games starring real family members, given as Christmas 2026 gifts. Read [README.md](README.md) for how to build and run, and [DEVELOPMENT_NOTES.md](DEVELOPMENT_NOTES.md) for the toolchain gotchas and lessons from the first prototypes. Don't redo something those notes say already failed.

## 1. Before you start any new task

A hook prints a sync report at the start of every session and tells you whenever `main` has moved. Act on it:

1. **Find out who you're working for.** Run `gh api user --jq .login` and use the table above. Use that person's branch prefix, and refer to the other one by name.
2. **Read what changed.** If `main` has new commits, skim them (`git log --oneline HEAD..origin/main`). Also read any new files in [docs/decisions/](docs/decisions/).
3. **Check what the other side is doing.** Look at the open PRs and issues in the report. Open PRs (including drafts) are claims: don't edit the files a PR from the other person is changing. If your task overlaps one, tell your human before writing any code.
4. **Start from the latest `main`.** A new task gets a new branch:
   ```
   git fetch origin && git switch -c <prefix>/<short-name> origin/main
   ```
   If you're continuing your own branch, update it first: `git rebase origin/main`.

Never auto-pull in the middle of a task. Fetching is fine. Your working files only change when you rebase between steps.

## 2. While working

- **Claim the work early.** After your first commit, push it and open a **draft PR** describing what you plan to do. The draft PR is how the other agent knows the area is taken.
- **One topic per branch.** Keep PRs small and merge them often. A two-day branch is much easier to merge than a two-week branch.
- **Never commit or push to `main`.** A hook blocks it. All changes reach `main` through a PR.
- **Only push to your own human's branches.** If you need a change on the other person's branch, leave a PR comment.
- Commit often, with messages that say what changed and why.

### Who owns which files

| Area | Owner | Rule |
|---|---|---|
| `src/<game>/` | Whoever has the open PR for that game | Edit freely inside your own game. |
| `src/common/` (characters, font, gfx, music) | Shared | Make shared changes in their **own small PR**, separate from game work, and get it merged quickly so the other side can rebase. Don't change an existing function's behaviour without checking who uses it (`grep -r name src/`). |
| `build.ps1`, `build.sh`, `deploy.ps1` | Shared | Keep the Windows and Mac scripts doing the same thing. |
| `CLAUDE.md`, `.claude/`, `.github/`, `docs/decisions/` | Shared | Workflow changes go in their own PR, and the PR must explain the change. |
| Binary files (PNG, `.aseprite`, audio) | Whoever creates the file | Binary files can't be merged. If you change someone else's binary file, the PR must say so. |

## 3. Messages to the other agent

There's no shared notes file, on purpose: it would cause merge conflicts. Messages go in these places instead:

- **PR description:** fill in the template, including "Notes for the other agent". Say what's unfinished, what you changed in shared code, and anything they need to do (for example, "rebase, `draw_face()` now takes a palette argument").
- **PR comments:** for discussion about a specific PR.
- **Issues:** for anything not tied to a PR. Title it `Heads-up: ...` for a notice, or describe the work for an idea or bug. Whoever acts on it closes it.
- **[docs/decisions/](docs/decisions/):** decisions that should last, such as palette rules, the menu structure, or ROM size. Add **one new file per decision** (`NNNN-short-title.md`) and never rewrite an old one. To change a decision, add a new file that replaces the old one.

Write these for an agent who knows nothing about your conversation.

## 4. Reviewing and merging

- Either brother can merge any PR, including their own. **Merge only when your human tells you to.**
- **To review a PR:** run `gh pr view <n>`, `gh pr diff <n>` and `gh pr view <n> --comments`. To build it without disturbing your own work, use a separate worktree: `git worktree add ../gb-review-<n> origin/<branch>`. Remove it afterwards with `git worktree remove ../gb-review-<n>`.
- Before a PR is merged, it must be rebased on the latest `main`, build with no errors (warnings 158 and 110 are harmless), and have a filled-in PR description.
- Merge with `gh pr merge <n> --squash --delete-branch`. Afterwards, check whether any of your human's open branches need a rebase.
- If a rebase hits conflicts in someone else's files, stop and ask your human. Don't guess.

## 5. Building

- Mac: `./build.sh` or `./build.sh -r <game>`. Windows: `.\build.ps1` or `.\build.ps1 -Run <game>`. Both scripts build every folder in `src/` except `common` into `build/<name>.gbc`.
- You can't see the emulator. After a change, build it, then ask your human to playtest. Describe exactly what to try.
- The project must build on both machines. Don't add tools that only work on one OS without a matching version for the other.

## 6. Content rules

- **The repo is public.** Characters use first names or nicknames only. No last names, addresses, schools or photos of real people.
- All art, music and level design must be our own. You can borrow game *genres* and *mechanics* (a flappy-style game, a tennis game). Don't copy another game's code, art, music, characters or levels. Open-source libraries are fine if their license allows it; note the license in the PR.
