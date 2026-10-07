# Planned Designs

Agreed designs for features not built yet, with their feature gates. The
short list of open work is [Staged Changes](STAGED_CHANGES.md); when one of
these is built, move what is true into its topic document and delete it here.

## SD cards across several Atlases (*Planned*, owner 2026-09-25)

A card records its owner Atlas. A card from another Atlas is offered as
**Game night** (its profiles, statistics and settings are usable here for
now; the card is not taken over) or **Merge in** (it becomes this Atlas's
data and card). Still to design: the same profile on both, whose PIN wins,
which statistics merge, and where game-night results are written. No
statistics rollback to NVS.

## Venue model: several games on one Atlas (*Planned*)

A venue Atlas runs more than one game at once. Profiles, statistics, pairing
and the portal stay shared; each game has its own lobby, engine and table
decisions. Owner decisions (2026-09-29):

- **Atlas speaker:** muted while more than one game is running. Cues then play
  only on Sigil buzzers and in browsers. With one game it behaves as today.
- **Atlas touchscreen:** shows one game at a time, with a "Game 1", "Game 2",
  ... selector. The selection is presentation state (Invariant 6), not a table
  decision.

Steps:

1. **Done 2026-09-29 (host-tested, firmware builds):** `GameTable` in
   `atlas_app.h` groups one game's `Lobby`, `GameEngine`, `ClientState` and
   table decisions.
2. **Done 2026-10-07 (*Experimental*, host-tested; Atlas 0.6.17, Sigil
   0.9.13, portal pack 1.3.0): two tables.** Defaults chosen: Game 1 and
   Game 2; everyone starts in Game 1.
   - *State owner:* each `GameTable`, plus `sigilTable[]` (the game an
     unseated Sigil goes to, RAM) and each web session's `table` (the game a
     phone follows, RAM). Next-game settings are per table; the last saved
     ones are both tables' settings after a boot.
   - *Routing:* code reaches its game through `table()`, set by a
     `TableScope`. Sigil handlers scope to `tableForController` (the table
     whose lobby or game holds the Sigil, else its chosen one); HTTP routes
     and `/api/status` scope to `TurnHubWebApi::requestGame` (a tablet's
     chosen game, else the profile's game, else the followed game, else
     `?game=N` without a session); `loop()` ticks each table. Controller IDs
     stay global, so a Sigil (with its seat B), a phone controller and a
     profile each sit at one table at a time.
   - *Intent:* `ChooseTable` (a Sigil's menu item "Switch game",
     `SigilAction::SwitchTable`): offered outside that Sigil's game; it leaves
     its lobby first. Phones switch with `POST /api/session/game`, which
     leaves a lobby through the usual LeaveProfile Intent and refuses a
     profile in a game.
   - *Device-wide gates:* OTA, Sigil updates, Atlas factory reset, sleep and
     setup need both games between games; forgetting or factory-resetting a
     Sigil refuses one seated at either table. Pairing follows Game 1, the
     game the Atlas screen shows. Resetting a game frees only its own
     controllers.
   - *Persistence:* each table has its own recovery record in `th_game_v1`
     (`checkpoint`, `checkpoint2`), both restored paused at boot; the
     checkpoint-before-statistics order holds per table.
   - *Contract:* `/api/v1/state` and `/api/status` add `game` and `games`;
     `revision` is one counter for both games ([HTTP v1](../../protocol/http-v1.md)).
   - *Clients:* the portal shows a Game 1 / Game 2 switch; Sigils show
     "Switch game". *Speaker:* quiet while both games are in progress.
   - *Accessibility:* the switch names each game and its state in words.
3. Atlas touchscreen game selector (presentation state, Invariant 6), so the
   screen, pairing and turn order can serve Game 2; the tablet page's switch.
4. Android app game switch, user manual.

## Sigil sleep (*Planned*)

*Shipped meanwhile (2026-10-02):* a manual **Sleep** in the Sigil device menu
and on Atlas's Menu > Device screen, using deep sleep and a pin wake (see
[Pairing and Secure Link](PAIRING_AND_SECURE_LINK.md), "Sleep"). The automatic, Atlas-managed light sleep below is still
planned.

Owner, 2026-09-30: after 5 to 10 minutes with no physical input, a Sigil
sleeps and mostly drops off the radio until its buttons are pressed or Atlas
wants it. Mainly for future battery power (the board's battery connector,
charger and battery ADC on GPIO 34 are unused so far, see the hardware
hardware doc), and also for fewer active radio devices in a large game where
people play from phones. A seated player's Sigil may sleep mid-game; Atlas
wakes it when it's needed.

**The radio constraint.** An ESP32 has no wake-on-radio: with the radio off a
Sigil can't hear Atlas. So "Atlas sends a wake" is a duty cycle. The Sigil
light-sleeps (RAM kept, radio and CPU off) and wakes every few seconds (3 to
5 s, to tune) to send one check-in and listen briefly. Atlas answers a
sleeping Sigil's check-in with Wake or Stay asleep. ESP-NOW doesn't buffer
frames for sleeping peers, so Atlas only ever answers in that window. Worst
wake latency is one check-in interval. Deep sleep (lower current, but a
reboot and reconnect on every wake, and no Atlas wake at all) is a later
option for a battery Sigil left unused for hours.

**Wake sources.**
- Buttons: light sleep can wake on any GPIO. Both Sigils: the stick click
  (GPIO 32) and BOOT; the analog directions (GPIO 34 and 35) can't wake it,
  so the Sigil screen says "Press the stick to wake".
- Atlas: at a check-in, when the Sigil's player becomes active (their turn, or
  a pass, win claim, elimination or life request that involves them), when a
  game starts or is armed, when pairing or an update targets it, and from the
  portal/app device list ("Wake").
- The press that wakes a Sigil only wakes it (the screen shows "Awake"); it
  is not also sent as input, so a sleeping Sigil never passes a turn by
  accident.

**Rules (Atlas owns them).**
- Never sleep while the Sigil's player is the active player, during a
  countdown, with a decision pending on that player, during an update, or
  while pairing.
- Timeout (owner, 2026-09-30): short outside a game, about 3 minutes in the
  lobby or with the Sigil unseated, and at least 10 minutes while a game is
  running (someone may go a long while between their own turns). Atlas
  picks which applies and sends it to Sigils like the input timing, so the
  Sigil's idle timer changes when a game starts or ends. Admins can
  lengthen either or turn sleep off; the in-game value never goes below 10
  minutes. Atlas can refuse a sleep request (answer Wake) if a rule above
  applies.
- A sleeping Sigil is **Asleep**, not **Offline**: its seat, participant and
  statistics are untouched, and it stays out of the "controller lost"
  handling. Asleep that stops checking in for longer than the link timeout
  plus one interval becomes Offline as today.

**Feature gate.**
1. **State owner:** Atlas (`SigilBus` keeps Awake/Asleep per Sigil, from the
   radio; the timeout is a game setting). The Sigil owns only its own idle
   timer.
2. **Intent:** none for the radio handshake (transport state, like Hello).
   Admin setting change: the existing settings Intent path. Portal/app
   "Wake": a new `WakeSigil` Intent (validator: Admin or the seated player).
3. **Validator:** Atlas answers each sleep request by the rules above.
4. **Persistence:** the two timeouts (out of game, in game) in
   `game_settings_store` (NVS); sleep state is RAM-only.
5. **Rendering clients:** Sigil screens ("Asleep", "Press ... to wake");
   Atlas touchscreen and portal/app device list show Asleep; `/api/devices`
   gains `asleep`.
6. **Protocol/contract:** `shared/include/protocol.h`: new packet types
   (SleepRequest, CheckIn, WakeDecision) and a capability for "can sleep",
   taking one of the bits freed by the baseline Sigil (0x01, 0x02, 0x04, 0x08,
   0x20), read through `helloCapabilities()` from the firmware that
   introduces it. Reflash Atlas and every Sigil; a good first update to
   deliver over Wi-Fi OTA rather than USB. `/api/devices` and the Android
   `DeviceInfo` model add the field.
7. **Third-party dependencies:** none (ESP-IDF light sleep in the Arduino
   core).
8. **Accessibility:** the wake-up press must not also act, so a player who
   can't see the screen never passes by accident. Sleep can't hide a cue: a
   Sigil whose player has a pending cue (turn, request) is woken first, and
   the cue plays on waking, not only by LED or buzzer while asleep. A
   per-player accessibility preference "Never sleep my Sigil" (like the hold
   timings) for players who rely on its LEDs/buzzer between turns.

**Open questions.** Exact defaults (about 3 and 10 minutes above); whether the e-paper keeps its last
image or shows an "Asleep" card (e-paper holds either with no power); LED ring
behavior (off while asleep); measured current in light sleep versus today
(ties into check H03); how long a battery lasts either way.

## E-ink Sigil: player-facing LED strip (possible, depends on the case)

*Planned, conditional* (owner, 2026-09-26). The enclosure concept is a 45° wedge
about 48 mm wide, 78 mm deep and 78 mm tall: portrait e-paper on the upper
slope, joystick below it, main board flat in the base, USB through the back
wall. In that shape the Jewel 7 status ring faces the **other players** from the
back wall, so the seated player can't see it. A short LED strip on the front lip
would give the **player** their own light. Build this only if the final case
keeps that split; if the case lets one light face both ways, drop it.

- **Hardware:** reserved on Sigil Rev A (2026-09-28): J6, a JST-XH 3-pin
  socket fed by its own data line, GPIO13 through U3 (74AHCT1G125) and R2, same
  pinout as J5. J6, U3, R2 and C4 are fitted on every board, so a strip can be
  added later without rework. A
  separate line suits the case better than the Jewel adapter's chain-out (the
  board is beside the front lip, the Jewel on the back wall) and gives the strip
  its own pixel chain. The chain-out stays as a fallback. Candidate: 6–8 SK6812
  RGBW pixels to match the Jewel's colour order.
- **Feature gate:**
  1. *State owner:* Atlas, unchanged. It already decides each Sigil's
     `LedState`; the Sigil only renders it.
  2. *Intent:* none. Lights are output only and add no gameplay action.
  3. *Validator:* none new. Any user setting (see 5) uses the existing
     accessibility preference path.
  4. *Persistence:* the pixel count is a build or hardware setting on the Sigil,
     not NVS. A per-player front-strip brightness, if added, belongs in the
     existing per-player accessibility preferences (`optional_preferences`).
  5. *Rendering:* the Sigil maps one `LedState` to two groups. The rear ring
     keeps today's table-facing role (turn state, player colour). The front strip
     shows the player's own cues (your turn, timer warnings, pending life
     approval). The exact split is an owner decision.
  6. *Protocol/contract:* probably none. If both groups are derived from
     `LedState` on the Sigil, `shared/include/protocol.h` doesn't change. A
     separate front-strip state from Atlas would be a radio contract change and
     need both firmwares reflashed.
  7. *Third-party dependencies:* none new (Adafruit NeoPixel already drives the
     Jewel).
  8. *Accessibility:* no information may live only on the strip; the e-paper
     repeats everything it shows. Brightness should be adjustable because the
     strip is a few centimetres from the player's eyes. Blink patterns follow
     the existing reduced-motion style (`LedStyle::ReducedMotion`).
- **Power:** USB can't supply full RGBW on 7 + 8 pixels. The firmware brightness
  cap (48/255) has to hold for the whole chain, and the front strip can run
  dimmer than the ring. *Needs verification:* measure current with both groups
  at the cap.
- **Before building:** settle the case (strip position, pixel count, cable
  route), then the owner's choice of which cues go front and which go rear.
