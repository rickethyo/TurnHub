# Death May Die Pass to and assigned cards

- 2026-10-17 / Codex, `codex/master`, accepted baseline `a336660` (#129).
  Clean working tree, fetched current refs, no open PR or overlapping active
  scope. The prior handoff-only commits are retained in this follow-up.
- Owner requests Pass -> **Pass to?** rather than choosing clockwise.
  Selecting a recipient atomically finishes the current turn and assigns the
  next; Cancel changes nothing. At a round boundary every living investigator
  can start, including the previous holder. Other game modes keep their flow.
- Feature gate: StandaloneGame owns eligibility and one pass transition;
  typed local TableControls action validates game/holder/round/completed-turn
  identity before commit. Compose owns a disposable, scrollable chooser with
  player/investigator labels and Back/Cancel. JSON saves the resulting existing
  turn state; no storage layout, Atlas protocol, dependency or endpoint changes.
  Timed automatic passes retain their configured behavior. Acceptance: target
  validation, stale dialogs, clock/stat accounting and round/solo transitions;
  device/TalkBack checks remain separate.
- A new PR is necessary because the owner has already merged #129.
- Implementation: `a407a30`, [PR #130](https://github.com/rickethyo/TurnHub/pull/130).
  [CI](https://github.com/rickethyo/TurnHub/actions/runs/39011395997) passes
  Android compilation, the complete JVM suite (including eight new pass-to
  scenarios) and release-bundle creation. Docs links, tokens, manual export and
  whitespace checks pass. The manual rendered to PDF; updated pages 4 and 7
  were visually checked. No local Android SDK/device; phone/tablet layout,
  rotation and TalkBack acceptance remain open. Next owner: review and merge
  with a merge commit, retaining codex/master. No merge or release performed.

### 2026-10-17 / Codex follow-up: sanity condition and relic slots

- Owner requests assignment framework for one sanity condition and one relic
  per investigator. Scope: named cards with editable rules/notes in
  setup and live dashboards/menus; no catalogue or automatic card effects.
  Same owner/branch, baseline `4a1d70b`; no overlap and #130 remains open,
  so this related follow-up stays in that PR.
- StandaloneGame owns typed assignment/replacement/removal and validation;
  each InvestigatorDashboard owns its cards. JSON snapshot recovery persists
  them. Back-to-setup retains assignments; Set up rematch returns to the lobby
  with empty slots for fresh draws. Changing investigator clears the dashboard. Names and rules have
  explicit size bounds. Native labeled editors support Save, Cancel and Remove;
  plain text references remain readable with wrapping and scrolling.
- No new Atlas protocol, dependencies, endpoints or third-party card data.
  Eight new card JVM scenarios cover isolation/seat swaps, both slots, input bounds,
  save/recovery and corrupt card data, lifecycle, investigator changes and
  rematch behavior. Documentation, token, manual-export and whitespace checks
  pass locally. Initial card implementation `d1c9be4` passed the complete Android
  run (39012247210). Device checks remain separate from JVM/build validation.
- Owner clarification, baseline `d1c9be4`: base-game sanity conditions must be
  assigned before starting. Relics are optional, off by default, with a setup-only
  Use relics switch; when enabled every investigator needs one. Disabling clears
  relic references. StandaloneGame validates readiness atomically at start;
  Compose uses the same result for disabled Start/countdown cancellation.
  Rematch returns to setup without starting clocks; corrections remain available
  during play. Practice supplies disposable references, leaving setup unchanged.
  Snapshot recovery persists the option, reset retains it; no protocol/dependency
  change. Two added card scenarios plus one practice scenario cover readiness,
  optional-rule guards, recovery and independent practice.
- Final implementation: `45c1380`, [PR #130](https://github.com/rickethyo/TurnHub/pull/130).
  [CI](https://github.com/rickethyo/TurnHub/actions/runs/39012533387) passes
  Android compilation, the complete JVM suite and release-bundle creation. The
  PR adds seventeen JVM scenarios (eight pass-to, eight cards, one practice).
  Docs links, design tokens, manual export and whitespace checks pass. The
  revised DOCX was rendered to PDF; pages 5–6 were visually checked. Device
  layout, keyboard, rotation and TalkBack acceptance remain pending. Ready for
  owner review/integration with a merge commit; retain codex/master. No merge
  or release performed.

Owner integrated PR #130 as `0dffe4d`. Device acceptance remains open in
[Staged changes](../STAGED_CHANGES.md).
