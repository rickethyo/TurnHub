# Persistent agent branches

## Branches and ownership

| Branch | Purpose | Maintainer |
| --- | --- | --- |
| `master` | Accepted integration baseline; existing push CI and Play internal upload | Project owner |
| `codex/master` | Codex's continuing work across chats | Codex |
| `claude/master` | Claude's continuing work across chats | Claude |
| `agent/experimental` | Explicitly assigned experiments; no automatic promotion | Owner assigns one current agent |

All four branches are permanent. Experiments need a named owner and scope in
`COLLABORATION.md` before use. The shared experimental branch is not a place for
both agents to push independently. Separate worktrees prevent one chat or GitHub
Desktop from switching another chat's checkout.

## Mandatory resource efficiency

Owner rule, 2026-10-09: all agents must minimize third-party resource consumption
and agent execution time while completing the authorized task and required
validation. Apply this to model/agent usage, external APIs, network requests,
build services, CI, storage and pull requests.

- Reuse available local evidence and previous results when still applicable.
  Batch independent reads and API requests; keep output focused. Avoid repeated
  searches, unnecessary downloads, tight polling and redundant tool calls.
- Use one agent by default. Delegate only when authorized and when a bounded
  subtask materially improves completion time or quality enough to justify the
  added resource use. Stop delegated work when its purpose is fulfilled.
- Run checks appropriate to the changed behavior once. Repeat or broaden them
  only after relevant changes, failures or unresolved concerns. Keep required
  checks; record unavailable validation accurately.
- Minimize PR count. Add related fixes, documentation and owner-requested
  follow-ups to the existing active PR when it remains reviewable. Batch ready
  changes into a deliberate push instead of triggering CI after every small edit.
  Use `[skip ci]` for documentation-only commits; code, workflow and script
  changes require their normal CI.
- Create a separate PR only when ownership, dependencies, independent review or
  scope makes consolidation impractical. Record the concrete reason in
  `COLLABORATION.md`. Consolidate superseded PRs when the owner requests it.
- End work when the requested result and relevant verification are complete.
  Resource savings never justify skipping required validation, abandoning the
  authorized task, or discarding another agent's work.

## Starting and finishing a slice

1. Inspect `git status`, fetch origin and read `AGENTS.md`, `CLAUDE.md` and
   `COLLABORATION.md`. Check open PRs and active scopes. Stop branch switching if
   the tree contains someone else's unfinished work.
2. Use your permanent branch, then merge `origin/master`. A clean branch can
   use `git merge --ff-only origin/master`; if it has its own commits, use a
   normal merge and resolve conflicts deliberately. Never reset or force-push.
3. Record owner, scope and baseline SHA in the shared discussion. Implement one
   reviewable scope, run relevant checks and record results and remaining checks.
4. Update an existing reviewable PR targeting `master` when practical; otherwise
   open one PR for the ready scope. Consolidate related follow-ups and keep its
   title and description current. Batch pushes to avoid redundant CI runs.
5. The owner reviews and integrates with **Create a merge commit** for permanent
   branches. Keep the source branch. Squash/rebase merges leave the original
   agent commits outside master's ancestry and prevent automatic fast-forwarding.
6. Fetch the integrated result before the next slice. The automatic workflow
   catches up idle branches; divergent branches need a deliberate merge of master.

If the permanent branch is busy, first check whether the work belongs in its
active PR. When separation is necessary, start a temporary `codex/<topic>` or
`claude/<topic>` branch from current `origin/master` in a separate worktree.
Temporary branches may be deleted after integration. Do not merge unfinished
work from another agent just to synchronize: use accepted master or agree a
specific cross-agent handoff.

## Automatic maintenance

`Agent branch maintenance` runs on pushes to `master` and can be run manually
from Actions after its workflow has reached master. It snapshots the current
master tip and checks each permanent agent branch:

- An ancestor of master is fast-forwarded using GitHub's non-force ref update.
- A branch already at or ahead of that snapshot is left alone.
- A divergent branch is preserved and named in the run summary with the next action.
- A missing branch is reported and left missing; the workflow does not recreate it.
- API failures fail the run. A concurrent branch change that prevents a
  fast-forward is preserved and reported rather than retried with force.

Runs serialize. The workflow uses only the built-in `GITHUB_TOKEN`, with
`contents: write`, and does not merge code, open PRs, publish, or handle signing
keys. Bot ref updates do not start another Actions run; PR validation remains
the route for active changes. Divergence notices live in the Actions summary;
they do not automatically message an agent.

## Repository settings

Keep **Allow merge commits** enabled and **Automatically delete head branches**
disabled while using permanent branches. Protect master with the existing CI
checks and review policy appropriate to the owner. Those are GitHub settings,
not settings this workflow changes. Squash merging temporary feature branches
is fine. Permanent branches must be retained after every PR.

## Shared record

`COLLABORATION.md` owns discussions, active scope and handoffs.
`STAGED_CHANGES.md` owns accepted unfinished work. The relevant engineering
reference owns current behavior. Branches hold active implementation, not a
second backlog. Record implementation SHA, tests actually run, unresolved
questions and next owner so the next chat can resume from evidence.
