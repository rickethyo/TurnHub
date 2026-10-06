# Engineering History

Dated records and long-form history. **Nothing here is updated as the code
moves on**, so don't read these to learn how TurnHub works today; use the
topic documents one level up. Read them when you need to know why something
was decided, what a bench run showed, or what a build measured.

| File | What |
|---|---|
| [GENERATION_HISTORY.md](GENERATION_HISTORY.md) | From the standalone Arduino timer through the Raspberry Pi generation to the ESP32 Atlas/Sigil system, with historical pin maps and facts still to recover |
| [SIZE_AND_CHANGE_HISTORY.md](SIZE_AND_CHANGE_HISTORY.md) | Source size and firmware RAM/flash snapshots. **Still appended to** on firmware-version bumps and large feature commits |
| [logs/](logs/README.md) | Captured serial logs that back *Verified* claims |
| [2026-09-19-atlas-intent-verification.md](2026-09-19-atlas-intent-verification.md) | The Intent migration on the ESP32 port and its owner-reported hardware acceptance |
| [2026-09-19-pass-audio-observation.md](2026-09-19-pass-audio-observation.md) | Missing PASS beeps and where audio could be dropped |
| [2026-09-22-stabilization.md](2026-09-22-stabilization.md) | Audit that added `/api/v1/info` and `/api/v1/state` and the client read model |
| [2026-09-22-manual-v02-review.md](2026-09-22-manual-v02-review.md) | User manual V0.2 against the code at the time |
| [2026-09-26-code-review.md](2026-09-26-code-review.md) | Code review of everything but Android |
| [2026-09-26-prototype-1-0-gap-review.md](2026-09-26-prototype-1-0-gap-review.md) | The Prototype 1.0 field-test lane against the tree |
| [2026-09-29-playtest-notes.md](2026-09-29-playtest-notes.md) | Playtest bugs and requests, the fix pass, and what the SD log showed about the phone crashes |
| [2026-10-01-code-review.md](2026-10-01-code-review.md) | Whole-repository review and the bugs it fixed |
| [2026-10-02-stability-plan.md](2026-10-02-stability-plan.md) | The staged multi-phone stability plan, with the first implementation (Atlas 0.6.7). Current status: [Diagnostics](../DIAGNOSTICS.md) |
| [2026-10-05-shared-sigil-turn-order.md](2026-10-05-shared-sigil-turn-order.md) | Playtest fix: turn order for both seats of a shared Sigil |

Superseded topic documents (the old `SECURE_LINK.md`, `SIGIL_OTA.md`,
`MANUAL_PAIRING.md` and so on, with their step-by-step build logs) were
merged into the current set on 2026-10-06; read them in git history
(`git log --follow` or `git show <commit>:Documentation/engineering/<file>`).

Add a dated record here as `YYYY-MM-DD-<topic>.md` when a review, playtest or
investigation produces findings worth keeping, and move anything that stays
true into the topic documents.
