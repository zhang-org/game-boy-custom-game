# 0001: How the two agents work together

Date: 2026-11-21 · Status: accepted

## Decision
- Every change reaches `main` through a PR from a personal branch (`adam/...`, `babel/...`). Hooks block commits and pushes to `main`.
- Open PRs, drafts included, are claims on the files they change. Open a draft PR early.
- Messages between agents go through GitHub: PR descriptions (the "Notes for the other agent" section), PR comments, and `Heads-up:` issues. Lasting decisions go in this folder, one file per decision. There's no shared notes file, so nothing conflicts.
- Either brother can merge any PR, including their own. Agents merge only when their human says so, using squash and delete branch.
- Agents fetch at the start of every session and on every prompt (see `.claude/hooks/`). They rebase only between tasks, never in the middle of one.
- No CI for now.

## Why
Two people on two operating systems with two separate Claude accounts. The agents can't see each other's conversations, so everything they need to know has to go through git and GitHub.
