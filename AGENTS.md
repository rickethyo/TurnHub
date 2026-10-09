# Working in TurnHub

Read `CLAUDE.md`, `COLLABORATION.md` and the relevant engineering references before
editing. `Documentation/engineering/AGENT_WORKFLOW.md` owns the Git workflow.

- Fetch current remote refs before work. Codex normally uses `codex/master`;
  Claude normally uses `claude/master`. These branches persist across chats.
- Check the working tree and open PRs before switching or updating a branch.
  Preserve uncommitted work. Never reset, force-push or delete a persistent branch.
- Merge `origin/master` into your branch before starting a new slice. Do not
  rebase a published persistent branch or merge another agent's unfinished work
  without an explicit handoff.
- Record scope, owner, branch and baseline SHA in `COLLABORATION.md`. Read the
  other agent's active scope before editing overlapping files.
- Keep one reviewable slice per PR targeting `master`. If your agent branch
  already has active work, use a temporary `codex/<topic>` or `claude/<topic>`
  branch in a separate worktree instead of stacking unrelated changes.
- Run the relevant checks and record actual results. Only documentation-only
  commits use `[skip ci]`; workflow and script changes require CI.
- Use a merge commit for persistent-branch PRs and retain their source branches.
  After integration, fetch again; fast-forward if possible, otherwise merge
  `origin/master`. The maintenance workflow never discards divergent work.
- The owner decides integration and releases. Do not merge PRs, publish releases
  or change repository settings unless the session authorizes those actions.

Git preserves code and handoff notes, not chat memory. A new chat must read the
shared documents and inspect current Git state before continuing.
