# Turn Timer and Presentation Cues

Implemented locally on `android/testing`, 2026-09-24. Host scenarios, Android unit
tests and firmware builds pass. **No hardware acceptance yet**: LED cadence,
buzzer tone and the Sigil/phone/portal agreement all need the bench list below.

This restores TurnHub's original purpose, a turn timer (see
[Generation History](GENERATION_HISTORY.md), Generation 0), on the current
authoritative architecture, and separates LED and audio presentation from game
logic.

## Feature gate

| Question | Answer |
| --- | --- |
| State owner | Atlas. `GameSettings.turnTimerMs` is next-match table setup; `GameEngine` captures it at start. The countdown is derived from the existing turn anchor, never stored or ticked. |
| Intent | Existing `ConfigureGame`; new payload field `durationMs` (0 = OFF). No new gameplay Intent: expiry changes nothing. |
| Validator | `handleGameSettingsIntent`: host primary seat, lobby only, `validTurnTimerMs`. `GameEngine::start` rejects invalid settings too. |
| Persistence | `gamecfg` schema 2 (below). The running match's timer rides in the recovery checkpoint. |
| Rendering clients | Sigil LEDs/buzzer (driven by Atlas's cue layer), browser portal, Android. None of them computes phases. |
| Contract change | HTTP/state only: `settings.turnTimerMs`, `turnTimer {phase, remainingMs}`, `/api/game/settings` fields. **No radio change**; Sigil firmware is unchanged. |
| Dependencies | None added. |
| Accessibility | Phase is always available as text (portal, app); LED warning versus expiry differ by cadence, not only hue; audio can be silenced without affecting LEDs; the 10 s warning and expiry never act on the game. |

## Timer semantics (Verified by host scenarios)

- **Model.** One value, `turnTimerMs`. `0` is OFF. Otherwise 15,000-3,600,000 ms in
  whole seconds (`TURN_TIMER_MIN_MS`, `TURN_TIMER_MAX_MS`). Captured at game start;
  lobby changes apply to the next match only.
- **Presets and custom.** Presets (`TURN_TIMER_PRESETS_MS` in `game_profile.h`:
  Off, 1, 2, 3, 5 minutes) are UI shortcuts served by `GET /api/game/settings`.
  A custom length is just another valid value; a 120 s custom timer and the
  2-minute preset are identical. Add a preset by appending to that array.
- **Timing.** The countdown uses the existing `turnStartedAtMs_` anchor, so pause
  freezes it, resume continues it, and a new turn (pass or elimination of the
  active player) starts a fresh countdown. No second timing system exists; the
  former per-call `warningMs` parameters and `currentWarningMs_` were removed.
- **Phases** (`TurnTimerPhase`, derived per call):
  - `NORMAL` - nothing to show.
  - `WARNING` - `TURN_TIMER_WARNING_MS` (10 s) or less remaining.
  - `EXPIRED` - the countdown reached zero. **The turn continues.** Atlas never
    passes, pauses or penalizes on expiry; no TurnHub document establishes such a
    rule. The player passes normally; the portal/app show time over.
  - `LONG_TURN` - timer OFF and the turn reached `TURN_TIMER_LONG_TURN_MS` (5 min):
    the preserved "green at 5 minutes" gentle cue. No sound.
- **Removed behavior.** The unused 75% "Caution" phase and the unwired `Warning`
  sound were replaced by the 10-second warning. Timers were always OFF before
  this change, so no deployed behavior changed.
- **Recovery.** Checkpoint schema 1 already had a per-turn `warningMs` word that was
  always 0. It now carries `settings.turnTimerMs` (0 = OFF), so old records restore
  as OFF and no schema bump was needed. Restored matches stay paused as before.

## Wire contract

- `GET /api/v1/state`: `settings.turnTimerMs`; `turnTimer.phase`
  (`NORMAL|WARNING|EXPIRED|LONG_TURN`) and `turnTimer.remainingMs` (null when OFF or
  no turn is running). Both are clock samples: they change without a revision
  change, like `turnElapsedMs`. Both are optional in the schema, so older
  firmware stays valid; Android treats absence as OFF.
- `GET /api/game/settings`: adds `turnTimerMs` and `turnTimer {presetsMs, minMs,
  maxMs, warningMs, longTurnMs}`.
- `POST /api/game/settings`: `turnTimerMs` is accepted; **omitted fields now keep
  their current value**, so a client can change one setting. Invalid values return
  400; non-host or non-lobby returns 409 through the Intent handler.
- `/api/status` (portal) adds `turnTimerMs`, `turnElapsedMs`, `turnRemainingMs`,
  `timerPhase`.

## Storage

`gamecfg` in the `turnhub` namespace: schema 1 is six bytes (profile, life) and
reads as timer OFF; schema 2 appends a little-endian `turnTimerMs` (ten bytes).
Writes always use schema 2 and skip unchanged values. Wrong size for the schema
or an invalid timer is `Corrupt`; schema 3+ is `UnsupportedSchema`; neither is
overwritten.

## LED cue architecture

```text
Atlas state --selectSigilLedState()--> SigilLedState --LedCueProfile--> blue/red/green
            (led_renderer.cpp)          (cue + overlays)  (led_cues.h)     (LedRenderer::set)
```

- `SigilLedState` is semantic: a primary `LedCue` (Unassigned, Joined, Starting,
  TurnStarted, YourTurn, Waiting, Paused, ConfirmationNeeded, EliminationSelect,
  GameOver) plus `LedOverlay` facets (Host, Starter, Winner, TurnWarning,
  TimerExpired, LongTurn) and parameters (player number, seat, anchor time).
- `LedCueProfile` maps each cue and overlay to per-channel `ChannelStyle` patterns
  (Solid, Blink, Breathe, PlayerCount, SeatPulse, Window). `defaultLedCueProfile()`
  reproduces the prototype's established cadences exactly; host scenarios check the
  lobby, turn and timer outputs.
- Default timer styles: warning = slow red pulse (1.5 s cycle), expired = steady red,
  long turn = steady green, all layered over the active player's blue breathe.
  Warning versus expiry differ by cadence. Every cadence is at or below 2.5 Hz.
- `LedRenderer::setProfile()` is the single configuration boundary. A future setting
  (palette, reduced motion, monochrome-safe) supplies another profile; selection
  and game logic do not change. There is no editor or persistence yet.
- `Pairing`, `Disconnected` and `Error` are in the vocabulary with styles, but Sigil
  firmware still renders them locally (Atlas cannot drive an unpaired or offline
  Sigil). Routing them through a shared profile needs a Sigil-side change.

## Audio cue architecture

- `AudioCue` names semantic events. `AudioCueProfile` maps each to a note pattern
  (empty = silent) and has a master `enabled` flag. `AudioController::setProfile()`
  is the boundary; muting clears queued cues immediately and leaves LEDs untouched.
- Existing sounds are preserved as the defaults. The pass sound is now
  `TurnStarted` (heard by the new active Sigil, as before); `TurnPassed` (the
  passer's confirmation) exists but is silent by default, preserving behavior.
- New: `TurnWarning` (one short 1.6 kHz chirp), `TimerExpired` (two low notes; rhythm
  differs from the warning, not only pitch), `ActionRequired` (defined, not yet
  emitted by any handler).
- Timer cues are one-shot: `updateTurnTimerCues()` in `gameplay_intents.cpp` watches phase
  transitions of the running turn and plays each cue once per turn on the active
  Sigil. Pause keeps the last phase, so resuming does not repeat a cue.
- **Not implemented:** a persisted or user-facing audio on/off setting. Whether it
  is table-level or a per-player accessibility preference that follows the profile
  (ACCESSIBILITY.md, "Accessibility profiles") is an owner decision.

## Clients

- **Portal:** Turn timer select (presets plus custom seconds) in the host's game
  settings; the running hero line shows time left, warning, time over or long turn
  as text with a badge.
- **Android:** the table header shows "Time left", "Over time +m:ss" or "Turn"
  with a text notice (live region) for warning, expiry and long turn; the countdown
  is extrapolated between 1 s polls and replaced by every snapshot. The host sees
  preset chips and a custom-seconds field in the lobby.
- **Sigil e-ink:** unchanged. Showing timer state on e-ink needs a radio-contract
  field (both display packets are full) and a reflash of every Sigil; staged.

## Bench acceptance (Needs verification)

1. Update Atlas only (no Sigil flash needed). Existing `gamecfg` (schema 1) loads as
   timer Off; profiles/stats intact.
2. Host sets 1 minute on the phone and in the portal; both show the same setting.
3. Active Sigil: blue flash then breathe; at 0:10 left, one short chirp and a slow
   red pulse; at 0:00, two low notes and steady red. Waiting Sigils unchanged.
4. Let it run a minute past zero: no pass, pause or extra sound; portal/app show time over.
5. Pause during the warning; resume; no repeated chirp; remaining time continues.
6. Pass: the next turn starts a fresh countdown; LEDs return to normal.
7. Timer Off: after five minutes the active Sigil shows steady green, no sound.
8. Power-cycle Atlas mid-turn with a timer: restores paused with the same timer.
9. Check the red pulse versus steady red is distinguishable without color (for
   example, in a monochrome photo or by a color-blind tester).
