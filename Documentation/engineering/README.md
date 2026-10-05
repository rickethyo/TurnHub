# TurnHub Engineering Reference

The engineering record for TurnHub hardware, firmware, architecture,
protocols and accessibility. Reference documents describe how things work
now; dated records keep what was found or decided on a given day.

## Confidence labels

- **Verified** - confirmed by repository source, hardware, or a contemporaneous artifact.
- **Reconstructed** - strongly supported by history and notes, not checked against the original hardware or commit.
- **Planned** - agreed direction, not yet the implemented specification.
- **Experimental** - existed during development, not necessarily part of the intended product.
- **Needs verification** - a useful placeholder, not authoritative yet.

Host tests passing is not hardware acceptance. As of 2026-10-02 the owner
reports that playtesting and bench testing have verified nearly everything
implemented; older "needs verification on hardware" notes in the dated
records predate that.

## Start here

- [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md) - hard rules every implementation preserves.
- [Staged Changes](STAGED_CHANGES.md) - the queue of agreed, unimplemented work.
- [Multi-phone stability plan](STABILITY_PLAN_2026_10_02.md) - staged investigation,
  stack headroom, HTTP resource reductions and the hardware release gate.
- [Prototype v1 Verification](PROTOTYPE_V1_VERIFICATION.md) - field-test gates still open.
- [Verification Backlog](VERIFICATION_BACKLOG.md) - historical facts and production decisions still open.

## Architecture and contracts

- [Software Architecture](SOFTWARE_ARCHITECTURE.md) - game-state ownership and controller responsibilities, and how they evolved.
- [Intent Model](INTENT_MODEL.md) - the common request boundary for every controller.
- [Identity and Storage Contracts](IDENTITY_AND_STORAGE.md) - typed identities, persistence ownership, NVS and microSD.
- [Protocol and Pairing](PROTOCOL_AND_PAIRING.md) - the radio contract, capability bits and protocol rules.
- [Manual Pairing](MANUAL_PAIRING.md) - pairing window, forgetting and factory reset.
- [Secure Link](SECURE_LINK.md) - the encrypted Atlas-Sigil radio link.
- [Completed-match Recovery Ordering](COMPLETION_RECOVERY.md) - checkpoint before statistics, and the remaining limit.
- HTTP client contract: [`protocol/http-v1.md`](../../protocol/http-v1.md).

## Features

- [Profile Login and Virtual Play](PROFILE_LOGIN_AND_VIRTUAL_PLAY.md) - independent login, phone-only tables, phone and Sigil together.
- [Physical Profile Selection](PHYSICAL_PROFILE_SELECTION.md) - the Sigil profile picker and reusable Sigils.
- [Accounts and Moderation](ACCOUNTS_AND_MODERATION.md) - first Admin, permissions, presence codes, moderation counts.
- [Game Profiles and Life Counters](GAME_PROFILES_AND_LIFE.md) - formats, starting life, life controls.
- [Life Approval and Commander Damage](LIFE_APPROVAL_AND_COMMANDER.md) - recipient approvals and linked counters.
- [Turn Timer and Cues](TURN_TIMER_AND_CUES.md) - the turn timer and the LED and audio cue layers.
- [First-run Setup](FIRST_RUN_SETUP.md) - guided setup from the app.
- [Sigil OTA](SIGIL_OTA.md) - signed firmware packages and updates for Atlas and Sigils.
- [SD Diagnostics](SD_DIAGNOSTICS.md) - optional card logs, hot-plug and failure behavior.
- [Web Portal Design System](WEB_PORTAL_DESIGN.md) - stylesheet, themes and the portal test contract.
- [Web Portal Pack](PORTAL_PACK.md) - the V1 portal as a signed pack on the microSD card, with the flash portal as fallback.
- [Accessibility Specification](ACCESSIBILITY.md) - requirements and the implemented settings.

## Hardware and tooling

- [Hardware Reference](HARDWARE_REFERENCE.md) - boards, pins, displays, controls and indicators.
- [Board Inventory](BOARD_INVENTORY.md) - development boards by MAC, used by the flash scripts.
- [Continuous Integration](CONTINUOUS_INTEGRATION.md) - GitHub Actions checks and artifacts.
- [Serial logs](logs/README.md) - captured boot and OTA logs.
- [Legal and IP Working Reference](../legal/README.md) - dependencies, notices, IP hygiene.

## History and dated records

- [Generation History](GENERATION_HISTORY.md) - from the first standalone timer to the ESP32 Atlas/Sigil system.
- [Size and Change History](SIZE_AND_CHANGE_HISTORY.md) - source size and firmware RAM/flash snapshots.
- [Code review 2026-10-01](CODE_REVIEW_2026_10_01.md) and [2026-09-26](CODE_REVIEW_2026_09_26.md).
- [Playtest notes 2026-09-29](PLAYTEST_NOTES_2026_09_29.md).
- [Prototype 1.0 gap review 2026-09-26](PROTOTYPE_1_0_GAP_REVIEW.md).
- [Stabilization 2026-09-22](STABILIZATION_2026_09_22.md).
- [Manual V0.2 review 2026-09-22](MANUAL_V02_REVIEW.md).
- [Atlas Intent migration verification 2026-09-19](ATLAS_INTENT_VERIFICATION.md).
- [PASS audio observation 2026-09-19](PASS_AUDIO_OBSERVATION.md).

Dated records are not updated as the code moves on; check the reference
documents for current behavior.

## Rules

**Atlas is authoritative.** It owns table and game state. Sigils, phones, the
touchscreen and simulators are controllers and views; every action converges
on a semantic Intent before game behavior runs.

**Accessibility is a hard requirement.** Essential information and actions
never depend on one sensory cue or one input method when a practical
alternative exists, and alternatives use the same Intent and authorization
path.

**Feature gate.** Before a significant feature, define: (1) state owner,
(2) Intent, (3) validator, (4) persistence owner, (5) rendering clients,
(6) protocol/contract change, (7) third-party dependency impact (update
`Documentation/legal/`), (8) accessibility impact.

**Structural-change preflight.** Before changing architecture boundaries,
state ownership, persistence, shared contracts, controller abstractions,
module layout or hardware/software interfaces, review this index,
`ARCHITECTURAL_INVARIANTS.md`, `STAGED_CHANGES.md` and the affected
references. If the change conflicts with them, resolve the documentation
first rather than letting the code drift.

**Keeping the record.**

- Agreed, unimplemented work goes in `STAGED_CHANGES.md`, not long-lived branches.
- When a decision changes, update the reference document. Abandoned
  experiments are marked historical, not erased.
- A new dependency, asset or third-party reference updates `Documentation/legal/` in the same cycle.
- A changed user-facing interaction, cue, display or timeout gets an `ACCESSIBILITY.md` review, and the user manual, UI text and docs stay in agreement.
- Firmware-version bumps and large feature commits append a snapshot to `SIZE_AND_CHANGE_HISTORY.md`.
