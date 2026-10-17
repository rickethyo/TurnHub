# Death May Die staged turns and community card catalogue

## TH-026: Death May Die staged next turn

### 2026-10-17 / Codex follow-up: community card catalogue

- Active owner: Codex on `codex/master`, baseline `5bef4aa`; clean tree,
  master fetched/merged, #131 remains open. Owner has the full collection and
  authorizes choosing a community source to try, retaining custom entry if poor.
- Scope: searchable condition/relic presets in the existing editor, every known
  entry available without collection filtering, editable names/notes and card
  references where verified. Community coverage is incomplete and is labeled;
  no invented card effects or images. Related follow-up stays in #131.
- Data choice: wiki condition-name list and BGG game inventories/card reference
  galleries; the randomizer spreadsheet download is unavailable. Bundle only
  factual names and reference identifiers/URLs, never artwork or card text.
  Images are fetched only when opened and cached on the device, like tile art.
- StandaloneGame remains the assignment validator; InvestigatorCard carries an
  optional catalogue ID, normalized against known entries and slot. StandaloneTable
  persists the ID with editable text. Compose owns disposable search/preview state;
  Save/Cancel/Remove retain existing behavior. No Atlas protocol/endpoint or library
  addition. Update the IP tracker for CMON/BGG references. Labeled native search,
  scrollable results and zoom controls support accessibility; device checks remain.
- Implementation: `41fb969`, added to [PR #131](https://github.com/rickethyo/TurnHub/pull/131).
  First-source coverage: 35 conditions, 20 relics and verified image references for
  18 entries. Catalogue names are available offline; images fetch only on opening.
  Seven new JVM scenarios cover catalogue search/normalization, slot validation,
  assignment/recovery isolation and image-cache/retry behavior.
- [CI](https://github.com/rickethyo/TurnHub/actions/runs/39029421036) passes
  Android compilation, the complete JVM suite and release-bundle creation.
  Local docs links, design tokens, manual export and whitespace checks pass.
  The revised manual was rendered to PDF and pages 5–6 visually reviewed.
  No local Android SDK/device: phone/tablet, large text, rotation and TalkBack
  remain acceptance checks. Next owner: try the source coverage and review/integrate
  #131 with a merge commit, retaining codex/master. No merge/release performed.

### 2026-10-17 / Codex follow-up: stage the next investigator

- Active owner: Codex on `codex/master`, baseline `0dffe4d` (merged #130).
  Clean tree, current refs fetched; no open PR or overlapping active work.
- Owner requests **Take the next turn** to stage a recipient until the current
  investigator passes. Pass then finishes/counts the turn and skips the chooser;
  without a selection the existing Pass to flow remains. Also research sources
  for condition/relic references and art; no catalogue import requested yet.
- StandaloneGame owns one validated, cancellable next-player selection. Existing
  counter action stages it; pass/timeout use the shared turn transition and clear
  it. StandaloneTable JSON persists it, and TableSummary supplies the UI's Up next
  label and direct handoff. Invalid/eliminated selections cannot receive a turn.
  No Atlas protocol, runtime endpoint, library or third-party data additions.
  Text status and a labeled Cancel next turn button expose the selection without
  relying on color. Regression tests cover accounting, lifecycle and recovery;
  phone/tablet and TalkBack acceptance remain device checks.
- Implementation: `5dc6fda`, regression-test refinement `6e7d0db`,
  [PR #131](https://github.com/rickethyo/TurnHub/pull/131). A new PR is needed
  because #130 is already integrated. Local documentation links, design tokens,
  manual export and whitespace checks pass. The updated manual rendered to PDF;
  pages 4–5 were visually reviewed. Android SDK/device validation is unavailable
  locally. [CI](https://github.com/rickethyo/TurnHub/actions/runs/39026863968)
  passes Android compilation, the complete JVM suite and release-bundle creation
  for `6e7d0db`. Phone/tablet, rotation and TalkBack acceptance remain open.
  Next owner: review/integrate #131 with a merge commit and retain codex/master.
  No merge or release performed.
- Research: the [community wiki insanity list](https://cthulhu-death-may-die.fandom.com/wiki/Insanaties),
  [CDMD/FOTU randomizer spreadsheet](https://boardgamegeek.com/filepage/254978/randomizer-for-cdmd-and-fotu)
  and [original insanity artist portfolio](https://filipe-pagliuso.artstation.com/projects/RY3gaE)
  provide reference leads. No complete condition/relic image API was verified.
  A curated offline catalogue could combine verified entries and available art;
  current cards remain editable references. No third-party content imported.

Pass to and card assignments merged in [PR #130](https://github.com/rickethyo/TurnHub/pull/130); [prior validation and handoff](2026-10-17-death-may-die-pass-and-cards.md).


Owner merged PR #131 as `6b90f42`. Current behavior is in
[Gameplay](../GAMEPLAY.md); device acceptance remains in
[Staged changes](../STAGED_CHANGES.md).
