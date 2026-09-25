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

The Atlas touchscreen follows the same rules: buttons at least 60 px tall with
text labels, a pressed state that changes luminance and border rather than hue
alone, and a visible seconds countdown for every hold (end match, 5 s; unlock
admin, 3 s). Touch actions use the same Intents as Sigils and the portal. With
no master button (removed 2026-09-24), the touchscreen is Atlas's only physical
input, so Unlock admin needs a 3 s hold on it; a player who cannot hold it can
ask anyone at the table to, and the protected action is then finished in the
portal by the account holder.

Atlas's speaker (2026-09-24) plays only table-wide cues, and each one also shows
on the Atlas screen, the Sigils and the portal. Its volume (Off to High) is an
Admin setting separate from each player's Sigil sound preference.

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

## Implemented accessibility settings

Inventory as of 2026-09-24. *Implemented* means in source with host, browser
and Android unit tests passing; none of the Sigil-side behavior has hardware
acceptance yet (*Needs verification* on the bench list below).

| Setting | Where it is chosen | Stored by | Default |
| --- | --- | --- | --- |
| Sigil sound on/off | Portal My Account > Sigil accessibility; Android > Sigil accessibility | Atlas, with the profile (`x<profileId>`) | On |
| Sigil light style: Standard, Reduced motion, Monochrome-safe | Same | Same | Standard |
| Action long-press (pause) hold, 1-4 s | Same | Same | 2 s |
| Action win hold, 3-10 s, at least 1 s longer than the long press | Same | Same | 5 s |
| Portal theme, including High contrast | Portal My Account > Appearance | This browser (`localStorage`) | High contrast when the device asks for more contrast (`prefers-contrast: more`), otherwise Brass |
| Portal reduce motion | Portal My Account > Appearance | This browser | Off, but the device's reduced-motion setting always applies |
| Windows high contrast (`forced-colors`) | Operating system | - | Follows the OS |
| Browser feedback sound, vibration and volume | Portal My Account > Browser feedback | This browser | Sound and vibration on, medium volume |
| Android high contrast | Android 14+ Settings > Accessibility > Contrast | Operating system | Follows the OS; a raised level switches the app to fixed high-contrast colours |
| Android text size, TalkBack, live announcements | Operating system | - | Follows the OS |

The four Sigil settings are *per-player* (owner decision, 2026-09-24): they follow
the player to whichever Sigil they sit at. Browser presentation choices stay
per-browser because they describe the device in hand, not the player.

### Feature gate (Sigil accessibility preferences)

1. **State owner:** Atlas profile repository (`AccessibilityPrefs`,
   `accessibility_prefs.h`). Not table or game state.
2. **Intent:** none. Like the physical-use/privacy policy these are profile
   settings, changed through `GET/POST /api/session/accessibility` by the
   signed-in profile only. The portal and Android use that one endpoint and its
   one validator. Nothing here changes what a gameplay Intent means: a longer
   hold still produces the same `ActionLong`/`ActionWin` and the same Intent.
3. **Validator:** `validAccessibilityPrefs()` in Atlas (ranges, 250 ms steps,
   1 s gap), shared with the radio contract (`validInputTiming()` in `protocol.h`)
   and re-checked by the Sigil before applying.
4. **Persistence:** Atlas NVS, `x<profileId>`, schema 1. Sigils keep the applied
   values in RAM only (Invariant 7).
5. **Rendering clients:** Sigil LEDs (`LedRenderer` per-Sigil profile), buzzer
   (`AudioController` mute mask), Sigil buttons (`InputTiming`); portal and
   Android only edit them.
6. **Contract change:** new HTTP endpoint and `accessibility-v1.schema.json`;
   radio `InputTiming = 24` and `CAPABILITY_INPUT_TIMING = 0x08` (backward
   compatible; Sigil firmware 0.5.4 needed for adjustable holds).
7. **Dependencies:** none added.
8. **Accessibility:** this is the accessibility path. Every option also has a
   non-Sigil route (the portal/app show all state as text and offer pause and
   win claims as buttons).

### Sharing a Sigil

When two players share a Sigil (seats A and B), Atlas merges their choices so
sharing never removes an accommodation either one asked for: sound is off if
either turned it off; Reduced motion beats Monochrome-safe beats Standard; the
longer hold times apply. During a match Atlas uses the profiles captured at the
start; otherwise the Sigil's current seat bindings. Atlas revisits each Sigil
about every two seconds and immediately after a save.

### Light styles

- **Standard:** the prototype's cadences (TURN_TIMER_AND_CUES.md).
- **Reduced motion:** no breathing, pulsing or counting flashes. Lights are
  steady, dim, or blink no faster than one 2-second change per 4 seconds. Your
  turn is bright blue and waiting is dim blue. Cues that can appear together also
  differ by cadence, so this style is monochrome-safe as well. The player number
  and which shared seat is meant are left to the e-ink display and portal/app.
- **Monochrome-safe:** Standard, except where two cues in the same situation
  differed only by colour: time over (steady red) versus a long untimed turn (now
  a short green blink every 4 s), and confirming a win (short pulses) versus
  choosing a player to eliminate (now long pulses).

### Sigil-rendered light (2026-09-25)

Sigils that render their own light (`LedState`, see PROTOCOL_AND_PAIRING.md)
receive the same style choice and apply it locally with the same rules: Reduced
motion turns breathing into steady light and faster blinks into the 4-second
slow blink; Monochrome-safe lengthens elimination pulses and makes a long
untimed turn blink. On the NeoPixel ring the player number is shown as that
many steady pixels and a shared seat as its half of the ring, so neither needs
counting flashes or colour. The light never carries information found nowhere
else: the Sigil display, Atlas screen and portal/app show it as text.

### Sigil menus (2026-09-25)

Menu actions are named in text on the Sigil screen (the compass legend or the
OLED list), never only by position, colour or sound. Deliberate actions keep the
seated players' hold thresholds, and hold progress shows both on the light (the
ring fills) and, on the OLED, as text. A menu choice produces the same Intent as
the equivalent button gesture or portal action.

### Decision-needed sound

`ActionRequired` (two short 1,150 Hz notes) now plays on the Sigil of the player
whose win confirmation is next, and on the recipient of a life-change request.
It respects that Sigil's sound setting. The same request is always shown as text
in the portal/app, and win confirmations on the Sigil display.

### Not yet implemented (see STAGED_CHANGES.md)

- Sigil-local Pairing/Disconnected/Error lights still use fixed firmware
  patterns, not the player's light style (Atlas cannot style an unpaired Sigil).
- Text/display scale on the e-ink screen, an extended life-approval window
  (15 s), and a monochrome-safe portal theme separate from High contrast.
- On hold (owner, 2026-09-24): LED intensity and buzzer volume. The current
  Sigil hardware cannot vary them.

Atlas's pairing window is adjustable (15, 30 or 60 s, admin Device Settings,
2026-09-24). The Sigil's own window stays 15 s, so with a longer setting press
Atlas's Pair first. Forgetting a pairing has a remote path (admin portal) as
well as the 10-second Sigil Pair hold, and ending a match as a draw (5-second
Atlas master hold) is signalled by a fast status-LED blink during the hold and
by "Draw" text in the portal and Android app.
- Accessibility preferences for players without a profile (guests).

### Bench acceptance (Needs verification)

1. Flash Atlas and one Sigil with 0.5.4; a second Sigil stays on older firmware.
2. Sign in on a phone, bind to the new Sigil, choose Reduced motion, sound off,
   3 s / 6 s. Within a few seconds: no breathing or pulsing, no buzzer, and
   pause needs a 3 s hold and a win claim 6 s (also on the Pause / Win button).
3. Reboot only the Sigil: it returns to 2 s / 5 s until Atlas resends (≤ 10 s).
4. Share the Sigil with a second profile that keeps defaults: the merged rules
   above apply. Leave: defaults return.
5. On the older Sigil, the same profile gets the light style and mute but
   keeps 2 s / 5 s holds.
6. Claim a win: only the next confirmer's Sigil plays the two-note decision cue.
7. Monochrome-safe and Reduced motion: check time over versus long turn, and
   win confirmation versus elimination, in a monochrome photo or with a
   colour-blind tester.

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

Last established: 2026-09-20; implemented settings added 2026-09-24
