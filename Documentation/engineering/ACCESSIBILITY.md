# TurnHub Accessibility Specification

Accessibility is a first-class TurnHub engineering requirement. It is not a post-launch enhancement and it is not limited to the web portal.

This document defines the current accessibility baseline for Atlas, physical Sigils, Virtual Sigils, the web portal, Android clients, and future TurnHub controllers.

Status: **Planned product requirement** unless a requirement is explicitly marked verified in implementation.

## Core rule

No essential TurnHub state, warning, instruction, or action may depend on a single sensory characteristic or a single input method when a practical alternative exists.

Examples:

- Color MUST NOT be the only way to distinguish game states.
- Sound MUST NOT be the only way to communicate a warning or confirmation.
- Flashing/pulsing MUST NOT be the only way to communicate a state.
- A timed long-press MUST NOT be the only practical route to an essential action when an accessible companion/controller can provide an equivalent semantic Intent.
- Pointer input MUST NOT be the only way to perform essential web/app actions.

Accessibility changes presentation and input. It does not create a second game-state authority. Atlas remains authoritative.

## Standards baseline

### Web and app surfaces

TurnHub web and application interfaces SHOULD be designed to meet **WCAG 2.2 Level AA** as the product baseline.

This is a design target, not a claim of certified conformance. Formal conformance claims require implementation review and testing.

Relevant principles include:

- Do not use color as the only visual means of conveying information.
- Maintain sufficient text and non-text contrast.
- Make functionality keyboard operable where applicable.
- Provide programmatic names/roles/states for controls so assistive technology can understand them.
- Avoid inaccessible dragging-only interactions.
- Provide visible focus indication.
- Avoid uncontrolled moving, blinking, or auto-updating presentation where practical.
- Avoid seizure-risk flashing.

Reference: https://www.w3.org/TR/WCAG22/

### Physical hardware

WCAG is primarily a digital-content standard, so physical Atlas/Sigil requirements are defined separately in this document. Hardware should use redundant sensory cues, distinguishable controls, adjustable interaction timing where practical, and an assistive digital path for essential actions.

Before commercial launch, accessibility obligations for every intended sales region, customer class, and distribution channel MUST be reviewed separately from this engineering specification.

## Information redundancy

Essential state should be communicated through at least two practical channels when hardware permits.

Preferred channels include:

- Text or icon on the display.
- LED color.
- LED pattern or cadence.
- Sound pattern.
- Haptic feedback where supported.
- Accessible digital representation through a browser/app.

Examples:

| State | Color | Pattern | Display / semantic alternative |
| --- | --- | --- | --- |
| Your turn | configurable | steady or distinct cadence | `YOUR TURN` / turn icon |
| Warning | configurable | pulse pattern | warning icon/text + optional audio |
| Paused | configurable | breathing pattern | `PAUSED` |
| Pairing | configurable | distinct pairing cadence | `PAIRING` / pairing status |
| Error | configurable | error cadence | text/code in accessible client |

No row may rely on color alone.

The turn-timer cues follow this table: a slow pulse for the ten-second warning,
a steady light when time has run out, and the same states as text in the portal
and app. LED and audio styles live in replaceable cue profiles, so palettes,
reduced-motion patterns and muting can change presentation without touching game
logic. See [Turn timer and cues](TURN_TIMER_AND_CUES.md).

## Color-vision requirements

TurnHub MUST remain usable when users cannot reliably distinguish commonly confused color pairs.

Requirements:

1. Never encode an essential distinction only as red/green, blue/purple, or another color pair.
2. Pair meaningful colors with text, iconography, pattern, position, or cadence.
3. Provide at least one high-contrast palette.
4. Provide a monochrome-safe mode for interfaces where practical.
5. User-selectable palettes should be treated as presentation preferences, not game rules.
6. Testing should include common color-vision-deficiency simulations, but simulation does not replace human testing.

## Visual accessibility

Web/app baseline:

- Normal text should meet WCAG 2.2 AA contrast targets.
- Meaningful UI components and graphical objects should meet the applicable non-text contrast requirement.
- Text should remain usable with browser/OS scaling.
- Controls should not become unusable when text size increases.
- Focus indicators must be visible.
- Critical information should not disappear solely because of hover state.

Physical display baseline:

- Essential Sigil information should favor high legibility over decorative density.
- Icons must have a textual or semantic equivalent somewhere in the system.
- E-ink layouts should remain understandable without color.
- Where screen real estate is limited, essential game state takes priority over decorative or statistical information.

## Audio accessibility

Audio is supplementary unless a non-audio equivalent is impossible for the underlying function.

Requirements:

- Critical tones require a visual/display equivalent.
- Users should be able to reduce or disable non-essential sounds.
- Distinct events should not rely only on pitch differences that may be hard to distinguish.
- Browser/app audio must not be required to operate the game.
- Volume and mute preferences should be configurable where the hardware permits it.

## Motion, flashing, and attention cues

TurnHub SHOULD avoid rapid flashing entirely.

Requirements:

- Do not use seizure-risk flashing patterns.
- Prefer slow pulse/breathe patterns for persistent attention states.
- Provide reduced-motion/reduced-animation behavior in digital clients.
- Non-essential animations should be suppressible.
- Persistent auto-updating interfaces should avoid unnecessary motion.

## Physical controls and dexterity

Physical Sigil controls should be distinguishable without relying solely on printed labels or color.

Design goals:

- Distinct button shape, position, size, texture, or tactile marking where practical.
- Generous activation areas and spacing.
- Avoid requiring fine simultaneous gestures for normal play.
- Long-press durations should be configurable where practical.
- Essential long-press actions should have an equivalent accessible Intent route through an authorized companion interface.
- Debounce and hold thresholds should tolerate realistic motor variability without making accidental activation unsafe.

Accessibility settings MUST NOT bypass authorization for protected actions. They provide alternate input paths to the same validated Intent.

## Digital input and assistive technology

Web and future app clients should:

- Use semantic controls rather than clickable generic containers where practical.
- Expose accessible names, roles, values, and states.
- Support keyboard navigation for essential actions.
- Maintain logical focus order.
- Announce meaningful state changes to assistive technology without excessive interruption.
- Avoid drag-only controls.
- Use sufficiently large touch targets.
- Respect platform accessibility settings when practical, including text scaling and reduced motion.

## Timers and time-limited interactions

Because TurnHub is itself a timing product, accessibility must distinguish between **game rules** and **UI timeouts**.

A game timer intentionally chosen as part of a game's rules may remain time-limited. Interface timeouts, confirmation windows, pairing windows, and administrative prompts should be adjustable or designed with sufficient time where practical.

Examples:

- A five-minute turn timer is game state and should not silently change because of an accessibility setting.
- A victory-confirmation dialog may need an extended or non-expiring accessible presentation.
- A long-press threshold may be adjusted without changing the meaning of the resulting Intent.

## Accessibility profiles

TurnHub should support player-level accessibility preferences independently of game profiles.

Potential player accessibility preferences include:

- Color palette.
- High-contrast mode.
- Monochrome-safe presentation.
- Text/display scale where supported.
- Reduced motion.
- LED intensity.
- Audio enabled/disabled and volume where supported.
- Alternate alert patterns.
- Extended interaction/confirmation timing where allowed.
- Long-press timing.
- Preferred companion/assistive interface.

State ownership:

- Player accessibility preferences that should follow a player are owned/persisted by Atlas with that profile.
- Device-specific calibration or hardware limitations remain device-local unless promoted to a table-level setting.
- Clients render the applicable preferences but do not become authoritative for player/game state.

## Physical, digital, and hybrid participation policies

TurnHub may support GM/host policies such as physical-only, digital-only, hybrid, or physical-plus-companion sessions.

Accessibility must not be accidentally blocked by these policies.

A policy that requires a physical Sigil may still authorize an assistive companion interface bound to the same player identity. That companion is another controller for the same player, not another player and not another state authority.

Conceptually:

```text
Physical Sigil ---------\
Assistive companion -----+--> same player identity --> semantic Intent --> Atlas
Virtual Sigil ----------/
```

Atlas still validates permissions and game rules for every Intent.

## Testing requirements

Accessibility verification should become part of feature completion rather than a final launch audit.

For applicable changes, test:

1. Meaning remains understandable without color.
2. Essential states have an alternate cue to sound.
3. Keyboard-only operation for web controls.
4. Screen-reader labels/roles/states for important digital controls.
5. Text scaling and high-contrast presentation.
6. Reduced-motion behavior.
7. Touch target usability.
8. Long-press/timeout behavior where relevant.
9. Physical button distinguishability where hardware changes.
10. At least one realistic assistive-companion path for actions otherwise dependent on physical dexterity.

Automated accessibility tooling is useful but MUST NOT be treated as a substitute for manual testing.

## Feature accessibility gate

Before implementing a significant user-facing feature, answer:

1. What information does the feature convey?
2. Does any essential meaning depend only on color, sound, motion, position, or timing?
3. What alternate presentation exists?
4. What inputs are required, and is there a practical alternate path?
5. How will the feature behave with keyboard/screen-reader/text-scaling/reduced-motion use where applicable?
6. Does the feature interact with an accessibility profile?
7. Does the alternate input path converge on the same semantic Intent and Atlas authorization as the default path?

If these questions expose an inaccessible essential path, resolve it before treating the feature as complete.

## Launch gate

Before commercial release, TurnHub should complete:

- Manual accessibility review of web/app surfaces against the chosen WCAG baseline.
- Hardware review for redundant cues, tactile operability, timing, and assistive alternatives.
- Color-vision testing.
- Screen-reader and keyboard testing.
- Testing with real users with relevant accessibility needs where feasible.
- Region-specific legal/accessibility review for intended markets and customers.
- Documentation of known accessibility limitations rather than silently claiming universal accessibility.

## Design review question

For every user-facing feature, ask:

> If the user cannot perceive one of our cues or cannot perform our default physical gesture, is there another practical way for them to obtain the same information or request the same authorized action?

The expected answer is **yes** whenever a practical alternative exists.

Last established: 2026-09-20
