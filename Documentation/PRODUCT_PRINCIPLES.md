# TurnHub Product Principles

This document records the durable product values that should guide TurnHub design decisions across hardware, firmware, software, documentation, support, and commercialization.

These are product-level principles rather than implementation details. Engineering choices may change as prototypes mature, but proposed changes should be checked against these values before they become product direction.

## 1. Local-first by default

TurnHub's core play experience must not depend on an internet connection, cloud account, external API, or remote service.

Atlas should continue to own and serve the essential table experience locally. Internet-connected features may add convenience, but loss of internet access must not prevent normal local play with otherwise functional hardware.

## 2. Purchased hardware should remain owned by the purchaser

A complete TurnHub device should remain useful after purchase without requiring continuing permission from TurnHub.

Core functionality must not depend on:

- Mandatory subscriptions.
- Activation servers that can later disappear.
- Remote authorization for ordinary local use.
- Forced cloud accounts.
- Artificial feature disabling after purchase.

A subscription or paid hosted service may be considered only for genuinely optional functionality that creates an ongoing service or hosting cost. Losing that optional service must not disable the purchased device's core local capabilities.

The long-term goal is straightforward: if the hardware still functions years or decades later, the owner should still be able to use it.

## 3. Repairability over planned obsolescence

TurnHub should favor designs that can be inspected, serviced, repaired, and maintained.

Prefer, where practical:

- Standard and replaceable components.
- Accessible screws and fasteners instead of destructive assembly.
- Replaceable batteries when batteries are used.
- Documented connectors and interfaces.
- Reproducible wiring, schematics, pinouts, and revision records.
- Modular replacement of failed components rather than disposal of the entire product.
- Common tools for service instead of unnecessary proprietary fixtures.

Manufacturing cost, size, durability, and assembly complexity are valid constraints, but convenience in manufacturing alone should not justify making a product unnecessarily difficult to repair.

## 4. Atlas remains authoritative

Product convenience must not weaken the core architecture.

Atlas owns canonical table and game state. Sigils, browser clients, future native applications, and assistive controllers request actions and render Atlas state. They do not become independent competing game engines or state authorities.

Product features should preserve the architectural invariants documented in `Documentation/engineering/ARCHITECTURAL_INVARIANTS.md`.

## 5. User choice is a feature

When multiple reasonable behaviors can be supported safely, TurnHub should prefer giving the owner an understandable choice instead of silently imposing one preference.

Examples include:

- Privacy choices.
- Profile and statistics visibility.
- Accessibility preferences.
- Device behavior and startup preferences.
- Optional network integration.
- Presentation preferences where hardware permits them.

User choice does not override safety, security, data integrity, or game-state authority, but implementation difficulty alone is not a sufficient reason to remove meaningful choice.

## 6. Privacy should be conservative by default

TurnHub should collect and expose only the information needed for the feature being provided.

Core play should remain local. Sensitive or derived personal statistics should default toward restricted visibility, with explicit user control over broader sharing where supported.

Privacy enforcement belongs at the authoritative data boundary. A client should not receive private information merely because the interface intends to hide it visually.

Optional analytics or future cloud-connected features should be clearly distinguished from local operation and should not become prerequisites for using purchased hardware.

## 7. Accessibility is part of the product, not an add-on

Accessibility requirements apply across physical controls, displays, web interfaces, future applications, setup flows, alerts, and documentation.

Essential information and actions should not rely on only one sensory cue or one interaction method when a practical alternative exists.

Accessibility paths should use the same Atlas authorization and semantic Intent boundaries as standard controls rather than creating a separate or reduced game system.

The detailed implementation requirements live in `Documentation/engineering/ACCESSIBILITY.md`.

## 8. Graceful degradation is better than unnecessary failure

TurnHub should continue providing the maximum safe local functionality available when a nonessential subsystem fails.

Examples include:

- Internet loss should not stop local play.
- Loss of an optional service should not brick hardware.
- A presentation client reconnecting should rebuild from Atlas state rather than corrupting the game.
- Invalid recovery data should fail safely to a fresh table while preserving unrelated durable data where possible.

Failure modes should be understandable, recoverable, and proportionate to the failed component.

## 9. Open, documented interfaces reduce lock-in

TurnHub should document the interfaces required to maintain, integrate, and repair the product where doing so does not create an unreasonable security risk.

Shared contracts, hardware revisions, pinouts, storage ownership, protocol behavior, and compatibility boundaries should be recorded in the repository rather than existing only in developer memory or chat history.

Security-sensitive material such as secrets and private keys is an exception. Documentation should explain the boundary without publishing secrets.

## 10. Dependencies and licensing should be intentional

Third-party software, assets, reference designs, trademarks, and libraries should enter the product deliberately and with known provenance.

A useful dependency is not automatically an acceptable shipping dependency. License obligations, replaceability, maintenance risk, security, and long-term availability should be considered before a dependency becomes difficult to remove.

The current legal and dependency record lives under `Documentation/legal/`.

## 11. Hardware and software should age gracefully

TurnHub should avoid design choices whose primary effect is to make otherwise functional hardware obsolete.

New capabilities may require newer hardware when there is a genuine technical reason, but existing hardware should retain the functionality it can reasonably support. Compatibility layers and migrations are preferable to arbitrary cutoffs when practical.

Firmware updates should improve or repair the product without making remote continued support a requirement for basic ownership.

## 12. Honest status beats optimistic claims

A successful compile is not a hardware verification. A bench test is not a field test. A prototype is not a production release.

Documentation and public claims should distinguish clearly among planned, implemented, tested, verified, experimental, and production-ready behavior.

Known limitations should be documented rather than hidden to make the product appear more complete.

## 13. Preserve the development record

When product direction changes, update the durable repository documentation. Do not rely on chat history as the sole record of an important decision.

Historical implementations should be marked historical instead of being rewritten as if they never existed. This makes future engineering, maintenance, legal review, and product decisions easier to understand.

## Decision test

When a significant product decision is unclear, ask:

1. Does this preserve local core functionality?
2. Does the purchaser still meaningfully own the device?
3. Does it make repair, maintenance, or replacement unnecessarily difficult?
4. Does Atlas remain authoritative for canonical state?
5. Are privacy and accessibility treated as system requirements?
6. Are users given reasonable control where multiple safe behaviors are possible?
7. Would the product still behave sensibly if TurnHub's servers or company disappeared?
8. Is the dependency, service, or restriction genuinely necessary, or merely convenient for us?
9. Are we accurately describing what has actually been verified?
10. Has the lasting decision been recorded in Git?

If a proposal performs poorly against these questions, it should be reconsidered before becoming product direction.

Last established: 2026-09-22
