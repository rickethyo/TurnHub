## TH-025: Death May Die active investigator dashboard

- Date / owner: 2026-10-17 / Codex. Death May Die dashboard on
  `codex/master`, baseline `2faabb8` (merged #128). Owner screenshots show
  Stress clipped in portrait and round status/Take the next turn clipped in
  landscape. Owner expanded scope: active investigator takes at least 60% of
  the play area, with interactive abilities, levels and insanity tracking.
  Implemented 70% active / 30% other-player lists, natural-height cards and
  independently scrolling areas. Wide active cards put abilities beside counters.
  Feature gate: StandaloneGame owns markers and reference edits, existing counter
  requests carry sanity/skill updates, typed investigatorSkills edits reference
  text locally. JSON recovery owns persistence; no Atlas wire changes, runtime
  endpoint or library additions. Level choices and effect resolution remain
  manual (companions/corrections), threshold sanity loss is validated in domain
  state. Native labels, text effects and wrapping 48dp controls support TalkBack
  and large text. Core-box presets are paraphrases checked against dashboard
  photos in Make Craft Game's character guide; other investigators have editable
  reference slots. Also found/fixed the final living investigator's frozen Pass.
  No overlapping active implementation found. Implementation: `d4fe209`,
  [PR #129](https://github.com/rickethyo/TurnHub/pull/129). Documentation links,
  tokens, manual export and whitespace checks pass. The manual renders to PDF;
  its new section was visually checked on pages 4–5. Android compilation and
  the complete JVM suite passed for final code `92084c0` in
  [CI](https://github.com/rickethyo/TurnHub/actions/runs/38996021399), including
  nine new dashboard/last-investigator regression tests. Final follow-ups add
  Preview upgrades without moving markers, label eliminated compact cards and
  propagate the active card's minimum height. No Android SDK or device is attached
  here; phone/foldable/tablet layouts, large text, TalkBack and lifecycle/device
  acceptance remain open. Owner merged PR #129 as `a336660`; device acceptance remains open. No release was performed by Codex.


