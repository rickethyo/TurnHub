# TurnHub Engineering Reference

How TurnHub works today, one document per topic. Each describes current
behavior; dated findings and superseded plans live in [history/](history/README.md)
and are not kept up to date.

**Start with:** [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md) (the
hard rules) and [Staged Changes](STAGED_CHANGES.md) (everything open). Then
read only the topic you're working on.

Cross-agent discussion and project-wide handoffs live in [`COLLABORATION.md`](../../COLLABORATION.md). Read it when starting work and update it when handing work back.

## Topics

| Document | Covers |
|---|---|
| [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md) | Rules every change preserves: Atlas authority, one handler per action, the feature gate, accessibility |
| [Architecture](ARCHITECTURE.md) | Ownership, the Intent pipeline and the full Intent catalog |
| [Storage and Recovery](STORAGE_AND_RECOVERY.md) | Identities, every NVS and SD record, the microSD card, match recovery, completion ordering |
| [Radio Protocol](RADIO_PROTOCOL.md) | ESP-NOW packets, capability bits, menus, lights, Atlas lost |
| [Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md) | Pairing with a code check, the BOOT button, forget, factory reset, sleep, link encryption |
| [Players and Accounts](PLAYERS_AND_ACCOUNTS.md) | Profiles, sign-in, joining from phones and Sigils, the picker, permissions, presence codes, moderation |
| [Gameplay](GAMEPLAY.md) | Game profiles, turns, turn timer, life and approvals, Commander damage, light and sound cues |
| [First-run Setup](FIRST_RUN_SETUP.md) | Guided setup of a new Atlas from the app or portal |
| [Firmware Updates](FIRMWARE_UPDATES.md) | `.thfw` packages, signing, releases, Sigil OTA, update notices, spare Sigils |
| [Web Portal](WEB_PORTAL.md) | The SD portal pack, the flash fallback portal, themes, layout and test contract |
| [Diagnostics](DIAGNOSTICS.md) | Serial, RAM and SD logs, tracing, and the multi-phone stability work |
| [Hardware](HARDWARE.md) | Atlas and Sigil boards, pins, the touchscreen, displays, controls, identifying and flashing boards |
| [Accessibility](ACCESSIBILITY.md) | Requirements and the implemented accessibility settings |
| [Continuous Integration](CONTINUOUS_INTEGRATION.md) | GitHub Actions checks, artifacts and local equivalents |
| [Planned Designs](PLANNED_DESIGNS.md) | Designs for features not built yet: venue model, Sigil sleep, front LED strip, shared SD cards |

Elsewhere: the HTTP client contract is [`protocol/http-v1.md`](../../protocol/http-v1.md);
Sigil screens and menus are [`Sigil/DISPLAY.md`](../../Sigil/DISPLAY.md); the
host test suites are [`Atlas/tests/host/README.md`](../../Atlas/tests/host/README.md);
the design system is [`design/README.md`](../../design/README.md); the Android
app is [`Android/README.md`](../../Android/README.md); dependencies and IP are
in [`Documentation/legal/`](../legal/README.md); build commands and
cross-file rules are in `CLAUDE.md`.

## Confidence labels

- **Verified:** confirmed by source, hardware, or a captured log.
- **Reconstructed:** strongly supported by history, not checked on the original hardware.
- **Planned:** agreed, not built.
- **Experimental:** existed during development, not necessarily the product.
- **Needs verification:** a placeholder, not authoritative.

Host tests passing is not hardware acceptance. As of 2026-10-02 the owner
reports that playtesting and bench testing verified nearly everything
built; the hardware checks still open are listed in
[Staged Changes](STAGED_CHANGES.md), "Open checks". Unlabelled statements in
these documents describe the code as it is.

## Keeping the record

- **One home per fact.** Put a fact in the topic document that owns it and
  link to it from elsewhere rather than repeating it. Keep documents about
  current behavior; don't narrate how a feature was built step by step
  (commit messages do that).
- **When something lands,** update its topic document in the same change and
  delete its line from Staged Changes. When a decision changes, rewrite the
  topic text; record the reason briefly if it matters ("owner, date").
- **Dated findings** (reviews, playtests, investigations) go in `history/` as
  `YYYY-MM-DD-<topic>.md`; lasting conclusions move into a topic document.
- **Feature gate.** Before a significant feature, define: (1) state owner,
  (2) Intent, (3) validator, (4) persistence owner, (5) rendering clients,
  (6) protocol/contract change, (7) third-party dependency impact (update
  `Documentation/legal/`), (8) accessibility impact. Put the answers in the
  topic document.
- **Structural changes:** review the invariants, Staged Changes and the
  affected topic first; if code would contradict the docs, resolve the docs
  explicitly.
- A changed user-facing interaction, cue, display or timeout gets an
  accessibility review, and the user manual, UI text and docs stay in
  agreement.
- Firmware-version bumps and large feature commits append a snapshot to
  [history/SIZE_AND_CHANGE_HISTORY.md](history/SIZE_AND_CHANGE_HISTORY.md).
- Never commit real board MAC addresses, keys, passwords or tokens.
