# Pass and Death May Die helpers

Integrated in PR #132, merge `250e36f`. Device acceptance remains in STAGED_CHANGES.

## TH-028: Death May Die setup and play helpers

- Owner: Codex on `codex/master`, baseline `d960d41`; fetched master, clean
  tree, related PR #132 remains open. Extend that reviewable slice.
- Owner requests: optional draw-two/choose-one conditions and relics, manual
  fourth/fifth abilities, an always-available turn reference, random investigator
  selection, previews and filtering by skills such as Brawling.
- Authority: standalone game validates setup-only house rule and stable random
  pairs, dynamic bounded skill lists and marker identity. Dashboard/JSON own
  offers and abilities; Compose owns draft editing, filtering and reminder views.
  Existing connected/protocol contracts remain unchanged. Legacy saves default
  to three skills with the house rule off; rematch clears acquired extra skills.
- Factual skill associations come from Make Craft Game's community index;
  existing original effect paraphrases are reused. Missing signature text stays
  explicitly unknown. No copied source implementation, art or rules bundled;
  sources/legal tracker and manual will be updated. Standard labeled controls,
  scrollable dialogs and text alternatives; no new dependency.
- Implemented in `ffe3543`, [PR #132](https://github.com/rickethyo/TurnHub/pull/132).
  Owner clarified that both physical and app-dealt draws must be available. The
  editor supports both; app pairs reserve both cards without replacement and
  survive cancel/reopen and recovery. No reopening reroll; exhausted pools offer
  manual entry. House-rule setup is frozen during play and retained for rematch.
  Acquired abilities support presets/custom text, eight total slots and marker
  preservation when removing an extra or renaming in place; fresh setup resets
  markers and clears extras while retaining edits to the original three slots.
  Random investigator selection respects search, skill filter and taken figures,
  and previews before committing. The factual common-skill index covers 94
  roster entries; ten core investigators have full summaries, other signature
  effects stay explicitly unknown. Turn phases are available in setup, on the
  active investigator and in Game menu, checked against the CMON rulebook.
- Validation: six focused JVM tests cover dealing, lifecycle/profile guards,
  physical assignments, persistence/legacy recovery, extra markers and resets,
  rename/remove preservation and random/filter availability. [CI passes](https://github.com/rickethyo/TurnHub/actions/runs/39044063208)
  Android compilation, the complete JVM suite and release-bundle creation for
  `ffe3543`. Local docs links, tokens, manual export and whitespace checks pass.
  Updated manual rendered and visually reviewed on pages 4–7. No new dependency,
  source implementation, third-party artwork or card text bundled.
- Next owner: integrate #132 with a merge commit, retaining `codex/master`, and
  playtest draw choices, recovery, additional skills, preview/filter/random,
  reminders and the Pass touch boundary on phone/tablet, including large text
  and TalkBack. Device checks remain in STAGED_CHANGES. No merge or release
  performed; no local Android SDK/emulator available.

## TH-027: Forgiving Pass touch targets

- Active owner: Codex on `codex/master`, baseline `6b90f42` (merged #131).
  Clean tree, current master fetched and merged; no open PR or overlapping scope.
- Owner reports accidental self-elimination after missing Pass and pressing the
  life decrement region, especially with impaired motor precision. Owner offers
  user-positionable/sized Pass controls or more margin around its touch target.
  Implement the margin option: physically separate life gestures from a bottom
  action strip, add a neutral gap and enlarge Pass. No draggable layout required.
- Root cause: non-cooperative panels put life gesture halves across the whole
  panel, behind gaps between the bottom controls. Missing Pass can start a life
  hold (450ms) before the 600ms pass hold would complete.
- Compose owns layout/gesture capture only. A weighted life area and separately
  measured action bar must have disjoint bounds even with wrapped controls, text
  scaling, rotation and Planechase's two-row bar. Pass is at least 64dp high;
  compact panels use remaining bar width and show their clock inside Pass.
  Preserve existing semantic actions, hold/undo timings and authoritative life/
  elimination rules. No Intent, persistence, protocol, dependency or asset change.
- Accessibility: neutral space cannot change life, no essential drag interaction,
  visible separation, generous Pass target and retained TalkBack actions. Check
  edge taps/holds and drags across the boundary on phone/tablet; no local SDK/device.
- A new PR is needed because #131 is integrated. Update the user manual and
  accessibility reference; use CI for Android compilation/the existing JVM suite.
- Implementation: `d1f9f4f`, [PR #132](https://github.com/rickethyo/TurnHub/pull/132).
  Local docs links, design tokens, manual export and whitespace checks pass.
  The updated DOCX renders correctly on reviewed page 8. No new dependencies or
  game-state tests: this change only moves input/layout and cancels gesture jobs.
  [CI](https://github.com/rickethyo/TurnHub/actions/runs/39042711784) passes Android
  compilation, the complete existing JVM suite and release-bundle creation for
  `d1f9f4f`. Physical hit-target behavior remains a device acceptance check, not
  a claim of automated pointer coverage. Final manual clarifications were exported,
  rendered and visually reviewed on page 8; docs/whitespace checks pass.
  Next owner: review/integrate #132 with a merge commit, retaining codex/master,
  then test physical edge taps/holds/drags and interrupted gestures. No merge or
  release performed.

