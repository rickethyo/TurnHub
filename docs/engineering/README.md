# TurnHub Engineering Reference

This directory is the working engineering record for TurnHub hardware, firmware, software architecture, protocols, and historical generations.

The documents are intentionally useful before they are perfect. Early TurnHub development moved quickly and some historical details were never formally recorded at the time. Rather than invent precision, these references use explicit confidence labels so older information can be corrected later without losing the development story.

## Confidence labels

- **Verified** - confirmed directly by repository source, surviving hardware, or a contemporaneous artifact.
- **Reconstructed** - strongly supported by development history and project notes, but not yet checked against original hardware or a commit from that exact moment.
- **Planned** - an agreed product direction that is not yet the implemented specification.
- **Experimental** - code or hardware that existed during development but is not necessarily part of the intended product architecture.
- **Needs verification** - useful placeholder that should not be treated as authoritative yet.

## Reference set

- [Architectural Invariants](ARCHITECTURAL_INVARIANTS.md) - hard design rules that implementations must preserve.
- [Intent Model](INTENT_MODEL.md) - the common request boundary for physical, browser, simulated, and future controllers.
- [Generation History](GENERATION_HISTORY.md) - development generations from the earliest standalone timer through the ESP32 Atlas/Sigil migration.
- [Hardware Reference](HARDWARE_REFERENCE.md) - known controllers, pin assignments, displays, buttons, indicators, and hardware-revision notes.
- [Software Architecture](SOFTWARE_ARCHITECTURE.md) - how game-state ownership and controller responsibilities evolved.
- [Protocol and Pairing](PROTOCOL_AND_PAIRING.md) - historical transports, message concepts, current pairing direction, and protocol design rules.
- [Verification Backlog](VERIFICATION_BACKLOG.md) - facts that should be confirmed against physical prototypes, commits, schematics, or future design decisions.

## Current architectural rule

The Atlas is the authoritative owner of table and game state. Sigils, browser clients, and future applications are controllers and views of that state rather than independent game engines.

Controller actions should converge on a semantic Intent before authoritative game behavior executes. Transport-specific code moves/authenticates requests; it does not own game semantics.

This principle is intended to reduce duplicate logic, avoid state disagreement, and allow physical and virtual controllers to use the same higher-level actions.

## Mandatory feature gate

Before implementing a significant feature, define:

1. State owner.
2. Intent/request.
3. Validator.
4. Persistence owner, if any.
5. Rendering/presentation clients.
6. Shared protocol/contract changes, if any.

If those boundaries are unclear, define them before implementation proceeds.

## Document maintenance rule

When a hardware or software decision changes, update the relevant reference rather than relying on chat history alone. If an implemented experiment is abandoned, preserve it in the generation history and mark it historical instead of rewriting history to imply it never existed.

Last reconstructed: 2026-09-19
