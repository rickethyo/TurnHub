# TurnHub Engineering Reference

This directory is the working engineering record for TurnHub hardware, firmware, software architecture, protocols, accessibility, and historical generations.

The documents are intentionally useful before they are perfect. Early TurnHub development moved quickly and some historical details were never formally recorded at the time. Rather than invent precision, these references use explicit confidence labels so older information can be corrected later without losing the development story.

## Confidence labels

- **Verified** - confirmed directly by repository source, surviving hardware, or a contemporaneous artifact.
- **Reconstructed** - strongly supported by development history and project notes, but not yet checked against original hardware or a commit from that exact moment.
- **Planned** - an agreed product direction that is not yet the implemented specification.
- **Experimental** - code or hardware that existed during development but is not necessarily part of the intended product architecture.
- **Needs verification** - useful placeholder that should not be treated as authoritative yet.

## Reference set

- [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md) - hard design rules that implementations must preserve.
- [Accessibility Specification](ACCESSIBILITY.md) - cross-platform requirements for color, contrast, redundant cues, physical input, assistive controllers, digital accessibility, and accessibility testing.
- [Intent Model](INTENT_MODEL.md) - the common request boundary for physical, browser, simulated, and future controllers.
- [Staged Changes](STAGED_CHANGES.md) - concise durable queue for agreed work that has not yet been implemented or fully verified.
- [Generation History](GENERATION_HISTORY.md) - development generations from the earliest standalone timer through the ESP32 Atlas/Sigil migration.
- [Hardware Reference](HARDWARE_REFERENCE.md) - known controllers, pin assignments, displays, buttons, indicators, and hardware-revision notes.
- [Software Architecture](SOFTWARE_ARCHITECTURE.md) - how game-state ownership and controller responsibilities evolved.
- [Identity and Storage Contracts](IDENTITY_AND_STORAGE.md) - typed identities, persistence ownership, legacy compatibility and migration/failure rules.
- [Profile Login and Virtual Play](PROFILE_LOGIN_AND_VIRTUAL_PLAY.md) - independent login, phone-only tables and simultaneous phone/Sigil control.
- [Physical Profile Selection](PHYSICAL_PROFILE_SELECTION.md) - planned two-button e-ink selection, reusable controllers, authorization decision and migration slices.
- [Protocol and Pairing](PROTOCOL_AND_PAIRING.md) - historical transports, message concepts, current pairing direction, and protocol design rules.
- [Verification Backlog](VERIFICATION_BACKLOG.md) - facts that should be confirmed against physical prototypes, commits, schematics, or future design decisions.
- [Legal and IP Working Reference](../legal/README.md) - dependency provenance, third-party notices, licensing/trademark tracking, and IP hygiene rules.

## Current architectural rule

The Atlas is the authoritative owner of table and game state. Sigils, browser clients, and future applications are controllers and views of that state rather than independent game engines.

Controller actions should converge on a semantic Intent before authoritative game behavior executes. Transport-specific code moves/authenticates requests; it does not own game semantics.

This principle is intended to reduce duplicate logic, avoid state disagreement, and allow physical and virtual controllers to use the same higher-level actions.

Accessibility is also a hard system requirement. Essential information and actions must not depend on one sensory cue or one input method when a practical alternative exists. Accessibility alternatives must still use the same Intent and authorization boundaries rather than creating parallel game logic.

## Mandatory feature gate

Before implementing a significant feature, define:

1. State owner.
2. Intent/request.
3. Validator.
4. Persistence owner, if any.
5. Rendering/presentation clients.
6. Shared protocol/contract changes, if any.
7. Third-party dependency/asset impact, if any, and update `docs/legal` when applicable.
8. Accessibility impact and alternate presentation/input path, if user-facing.

If those boundaries are unclear, define them before implementation proceeds.

## Mandatory structural-change preflight

Before making any structural change to TurnHub, review the current Git documentation first. This is a project rule, not optional process guidance.

At minimum, review:

1. This engineering index.
2. `ARCHITECTURAL_INVARIANTS.md`.
3. `STAGED_CHANGES.md`.
4. The specific reference documents affected by the proposed change, such as the Accessibility specification, Intent model, software architecture, protocol/pairing, hardware reference, verification backlog, or legal/IP references.

Structural changes include architecture boundaries, state ownership, persistence models, shared contracts/protocols, controller abstractions, major file/module organization, branch/workflow structure, and hardware/software interface boundaries.

If the proposed change conflicts with the documented architecture, resolve the documentation and decision explicitly before changing structure. Do not silently make the codebase contradict the engineering record.

## Staging rule

Planning and agreed near-term changes stay in `STAGED_CHANGES.md` on the active development branch rather than creating long-lived planning branches. Short-lived branches are reserved for code changes that genuinely need isolation, review, or experimental protection.

## Document maintenance rule

When a hardware or software decision changes, update the relevant reference rather than relying on chat history alone. If an implemented experiment is abandoned, preserve it in the generation history and mark it historical instead of rewriting history to imply it never existed.

When a new external dependency, asset, reference design, copied implementation, or third-party branding reference enters the project, update the legal/IP working reference in the same development cycle.

When a user-facing interaction, cue, display, timeout, or controller path changes, review `ACCESSIBILITY.md` in the same development cycle.

Last reconstructed: 2026-09-20
