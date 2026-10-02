# TurnHub Staged Changes

The queue of agreed work that is not implemented yet. Keep it short: when an
item lands, move any lasting facts into the reference document it belongs to
and delete it here. Git history keeps the old entries.

Owner, 2026-10-02: playtesting and bench testing have verified nearly
everything implemented so far, so the old "awaiting hardware" notes were
removed from this file. The hardware checks still open are in
[Prototype v1 verification](PROTOTYPE_V1_VERIFICATION.md).

## Standing decisions

- **The OLED Sigil leads** (owner, 2026-09-28). New Sigil UI lands on the OLED
  first. The e-ink Sigil stays buildable and supported, follows the OLED, and
  may drop non-essential extras that cost it full refreshes.
- **No compatibility before release** (owner, 2026-09-30). See `CLAUDE.md`.
  When hardware is released, the upgrade path in [Sigil OTA](SIGIL_OTA.md)
  ("Version rules") comes back first.
- **Not on e-ink:** the turn timer on the Sigil screen; try it on an LCD Sigil.

## Open work

### Statistics, recovery and history

- **Crash-safe statistics.** Allocate a durable MatchId and completion receipt
  before replaying completion, so an interrupted profile write can't leave
  missing or partial results. Until then statistics are not exactly-once
  ([completion ordering](COMPLETION_RECOVERY.md)).
- **Game-scoped statistics** (owner, 2026-09-24: the long-term goal). A stable
  `gameProfileId` partitions statistics by game/format; existing totals stay
  as unclassified; a draw counter; raw session facts are canonical and values
  such as win rate are derived. Privacy: participation facts public by
  default, derived performance private by default with opt-in sharing,
  enforced on Atlas before serialization. Hiding stats never stops recording.
- **Life event log.** Record each life change (profile, actor, time, old, new,
  delta) and derive life gained/lost from it. Rapid same-actor reversals within
  about a second count as corrections (`40 -> 48 -> 47` is +7 gained), while
  the raw events stay in the log marked as a correction.
- **Session history.** Bounded game records on the microSD card with enough
  raw facts to rebuild aggregates, a retention limit, and kept separate from
  the small active-match recovery record.

### Storage

- **SD cards across several Atlases** (owner, 2026-09-25). A card records its
  owner Atlas. A card from another Atlas is offered as **Game night** (its
  profiles, statistics and settings are usable here for now, the card is not
  taken over) or **Merge in** (it becomes this Atlas's data and card). Still
  to design: the same profile on both, whose PIN wins, which stats merge, and
  where game-night results are written. No statistics rollback to NVS.
- **Portal theme packs** served from the card as extra token sets; built-in
  themes stay in flash ([Web Portal Design](WEB_PORTAL_DESIGN.md)).
- **Passwords** (implemented 2026-10-02, see
  [Accounts](ACCOUNTS_AND_MODERATION.md)): still to do are the user manual and
  a slow, salted hash (PBKDF2 through mbedTLS) in place of the single SHA-256
  over profile ID and secret, which suits a PIN but is weak for a password if
  Atlas's NVS were ever read out. Changing the hash needs a factory reset.
- **App lock and automatic sign-in** (owner, 2026-10-02; V1 plan phase 7).
  The Android app keeps one profile's secret in the Android Keystore, unlocked
  by the phone's fingerprint, face or screen lock (or an app PIN), and signs
  that profile in to Atlas whenever it connects. It gives the one-tap feel of a
  passkey. Real passkeys (WebAuthn) do not fit an offline Atlas: the relying
  party must be a domain name, not 192.168.4.1, with a certificate browsers
  trust, and Android checks that domain online. HTTPS (planned) alone does not
  change that.
- **Custom avatars:** upload and Admin approval before one becomes public
  (`AVATAR_CUSTOM` is reserved; presets work today).

### Setup and updates

- **First-run setup in the portal:** the same steps on the same endpoints as
  the app; a "Setting up a new table" section in the user manual; later, an
  Atlas name. See [First-run setup](FIRST_RUN_SETUP.md).
- **Atlas fetches updates over home Wi-Fi** (optional, after app delivery).
  Atlas stores a home SSID and password (Admin, presence-gated, NVS); **Check
  for updates** joins it in station mode between games, reads the release feed
  over HTTPS, downloads and verifies the same `.thfw` packages, disconnects and
  returns its AP and ESP-NOW to channel 6. Needs a CA bundle and about 40 KB of
  heap; can't pass a sign-in page. Staying connected is out of scope (the
  router would pull Atlas off channel 6).
- **Framework log capture:** route `log_e`/ESP-IDF output into the Atlas log
  (for example a vprintf hook).

### Table, controllers and accessibility

- **Moderate on the Atlas Player screen:** remove a player and Admin sign-in
  beside Concede.
- **Profile selection:** a per-Sigil startup choice (last profile or picker)
  and labels for duplicate names ([Physical profile selection](PHYSICAL_PROFILE_SELECTION.md)).
- **OLED Sigil:** say on the Sigil when Seat B is refused (today the second
  seat just doesn't appear).
- **Accessibility:** see "Not yet implemented" in [Accessibility](ACCESSIBILITY.md):
  styled Sigil-local lights, e-ink text scale, a longer life-approval window,
  a monochrome-safe portal theme. LED intensity and buzzer volume are on hold
  until the hardware can vary them.
- **Atlas:** a battery gauge.
- **Open bug, not reproduced:** Atlas stuck on the pause screen during harness
  Soak x5 (2026-09-28). Needs the harness serial log and Atlas's diagnostics log.

### Hardware

- **Sigil carrier PCB:** layout, design-rule checks and a test fit of the
  DevKit and Jewel footprints before ordering; the OLED PCB is still an empty
  placeholder ([KiCad](../../KiCad/PCB/Sigilv1/README.md)).
- **Power:** measure idle, radio, display, buzzer and capped LED current, and
  confirm the chosen USB supplies hold up (feeds Sigil sleep and batteries).
- Freeze hardware revisions only after GPIO, power, display, transport,
  tactile-control and accessibility decisions are settled.

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
   table decisions (`hubState`, `pendingPass`, countdown, elimination target,
   win arm, turn-timer cues). `tables[MAX_GAME_TABLES]` has one entry, and the
   old global names are references into `tables[0]`, so behavior is unchanged.
   Next, move call sites to take a `GameTable &` explicitly.
2. Two tables in RAM: controller and profile → table assignment (a Sigil and
   its Seat B stay together; a profile sits at one table), Intent routing via
   `seatForIntentActor` and friends, per-table loop ticks, host scenarios for
   independent tables. Lobby arrays stay indexed by the global controller ID,
   and a controller joins at most one table.
3. Per-table recovery records (today one NVS namespace, `th_game_v1`), each
   restoring paused, and the checkpoint-before-statistics order per table
   (`COMPLETION_RECOVERY.md`).
4. Client contract: per-table state and revisions in `/api/v1/state`,
   schemas, `protocol/examples`, Android models, portal pages.
5. Touchscreen game selector, speaker muting rule, user manual.

Feature gate still to write before step 2 (state owner, Intent, validator,
persistence, clients, contract, dependencies, accessibility).

## Sigil sleep (*Planned*)

*Shipped meanwhile (2026-10-02):* a manual **Sleep** in the Sigil device menu
and on Atlas's Menu > Device screen, using deep sleep and a pin wake (see
`MANUAL_PAIRING.md`). The automatic, Atlas-managed light sleep below is still
planned.

Owner, 2026-09-30: after 5 to 10 minutes with no physical input, a Sigil
sleeps and mostly drops off the radio until its buttons are pressed or Atlas
wants it. Mainly for future battery power (the board's battery connector,
charger and battery ADC on GPIO 34 are unused so far, see the hardware
reference), and also for fewer active radio devices in a large game where
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
- Buttons: light sleep can wake on any GPIO. OLED Sigil: all five keys.
  E-ink Sigil: the stick click (GPIO 32, SW); the analog directions (GPIO 34
  and 35) can't wake it, so the Sigil screen says "Press the stick to wake".
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
   taking one of the bits freed by the baseline Sigil (0x02, 0x04, 0x08, 0x20,
   0x40), read through `helloCapabilities()` from the firmware that
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
(ties into verification item H03); how long a battery lasts either way.
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


## Working rules

1. Keep agreed, unimplemented work here, not in long-lived branches or chat.
2. Use short-lived branches only for code that needs isolation or review;
   `master` is the accepted baseline.
3. Before a structural change, review the engineering index,
   `ARCHITECTURAL_INVARIANTS.md`, this file and the affected references, and
   resolve conflicts in the docs first.
4. Before calling a user-facing feature complete, review it against
   `ACCESSIBILITY.md`.
