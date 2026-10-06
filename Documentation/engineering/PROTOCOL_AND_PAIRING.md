# TurnHub Protocol and Pairing

Pairing is deliberate: Atlas opens an admin-adjustable window (60 s minimum),
the Sigil requests within it, and both keep the pairing (MAC plus Secure Link
key). Forgetting uses a 3 s BOOT hold or the Sigil device menu on the Sigil and
Forget on Atlas (`Unpair = 12`). See [Manual Pairing](MANUAL_PAIRING.md).


This document separates three things that are easy to confuse during rapid prototyping:

1. The **logical protocol** TurnHub wants controllers to speak.
2. The **transport** used to carry those messages.
3. The **pairing/trust relationship** that decides which devices belong to an Atlas.

The logical protocol should remain as independent from the transport as practical.

---

## Generation 1 wired serial protocol

**Status:** Historical and verified from surviving Arduino firmware

The Raspberry Pi and wired modules exchanged human-readable serial messages.

### Module to host

Examples:

- `READY|1`
- `PASS|1`
- `ACTION|1|DOWN`
- `ACTION|1|UP`
- `ACTION|1|SHORT`
- `ACTION|1|LONG`
- `ACTION|1|WIN`

### Host to module

Examples:

- `BLUE|1|<0-255>`
- `RED|1|<0|1>`
- `GREEN|1|<0|1>`
- `OFF`

This protocol was simple, debuggable, and appropriate for a bench prototype, but it assumed wired modules and positional numeric IDs.

---

## ESP32 migration packet protocol

**Status:** the current prototype transport: ESP-NOW on channel 6, every packet
sealed by the [Secure Link](SECURE_LINK.md), protocol version 3.

Atlas and Sigil share compact packed packets defined once in
`shared/include/protocol.h`; both PlatformIO projects and the host test runners
add `shared/include` to their include path. The packet types (Hello and its
acknowledgement, menu state and selections, life, light, buzzer, display
snapshots, pairing, unpair, factory reset, harness and update packets) are
listed there; the subsections below record why each one exists. Atlas supports
at most eight physical Sigils.

### Baseline Sigil and the Hello capability byte (2026-09-30)

Owner decision: every Sigil is built the same way, so Atlas assumes the
features they all have and the Hello capability byte carries only what varies.
The **baseline**, which nothing announces: a screen that shows the player's
profile and the game (`GameDisplay`), five-key input with Atlas's action menu
(`MenuState2`, `SelectAction`), the profile picker, life keys (`LifeAdjust`,
`LifeResponse`), adjustable hold times (`InputTiming`) and the NeoPixel Jewel
ring drawn from `LedState`.

| Bit | Name | Meaning |
| --- | --- | --- |
| 0x10 | `CAPABILITY_DISPLAY_OLED` | OLED display; clear: e-paper. Also picks the OTA package |
| 0x40 | `CAPABILITY_SPARE` | A spare Sigil running the inert spare firmware; 0x10 then reports its GPIO4 strap ([Spare Sigil](SPARE_SIGIL.md), 2026-10-06) |
| 0x80 | `CAPABILITY_HARNESS` | The hardware test harness (unchanged) |
| 0x01, 0x02, 0x04, 0x08, 0x20 | free | Retired below (0x01 was `CAPABILITY_INPUT_DPAD`, the OLED Sigil's five buttons, until every Sigil got the thumbstick on 2026-10-01); available for new meanings |

The retired bits were `DISPLAY` (0x01), `DISPLAY_PROFILE` (0x02),
`GAME_DISPLAY` (0x04), `INPUT_TIMING` (0x08), `LED_STATE` (0x20) and `MENU`
(0x40). Every protocol-2 Sigil is firmware 0.8.0 or later (0.8 shipped
2026-09-25, protocol 2 on 2026-09-29) and sent all of them, so no Sigil that
can still connect lacked any of those features.

**Protocol 3.** First this shipped inside protocol 2, keyed on Sigil firmware
0.9.0, because `MIN_UPDATABLE_VERSION` blocked a bump. The same day the owner
set the rule that backward compatibility applies only to released hardware
(see "Protocol 3" below), so the byte now simply means what the table says
and a freed bit takes a new meaning with the protocol version that
introduces it.

**Retired packet numbers** (reserved, never reused): 3-8 (`Pass`,
`ActionDown/Up/Short/Long/Win`, the three-button gestures), 20-22
(`SetBlue/SetRed/SetGreen`, the per-channel light stream) and 26 (`MenuState`,
the pre-0.8 menu encoding). Atlas also dropped its gesture adapter (the
Action + Pass chords and the long-press pause that armed a win hold) and its
copy of the LED cadence tables; the Sigil dropped the three-button fallback
and the one-LED view.

### Sigil menus: MenuState and SelectAction (2026-09-25)

Sigils with five-key input advertise `CAPABILITY_MENU` (0x40). Atlas then sends
`MenuState = 26`: a mask of the `SigilAction`s this Sigil may use now (21 at
most), the default (likely) action and a 6-bit menu revision. Atlas works the
mask out from table state (`Atlas/src/sigil_menu.cpp`), sends it whenever it
changes (bumping the revision) and resends it on every Hello.

The Sigil answers with `SelectAction = 13` (action and revision). Atlas acks it,
drops a choice from an old revision or one no longer offered (and resends the
menu), and otherwise dispatches the same Intents the button gestures produce
(`handleSelectAction` in `sigil_input.cpp`, an audited adapter). Start is Arm
then Start in one choice; Link phone approves a waiting browser link, as an
Action press did. Holds for deliberate actions (`sigilActionHold`) are timed on
the Sigil with the seated players' InputTiming thresholds.

Sigils without the bit keep the Action/Pass gestures, and a menu Sigil talking
to an older Atlas falls back to them. Protocol version stays 1.
*Superseded 2026-09-30:* every Sigil has the menu; the gestures, the fallback
and `MenuState` are retired (see "Baseline Sigil" above).

### Factory reset: FactoryReset (2026-09-25)

`FactoryReset = 28` (Atlas -> Sigil), value `FACTORY_RESET_CONFIRM`
(`0x46524553`, "FRES"). A Sigil honors it only from its saved Atlas, for its own
ID, with that value: it erases its NVS partition and restarts unpaired. Atlas
sends it from the `FactoryReset` Intent (Device Settings) just before forgetting
the Sigil (see [Manual Pairing](MANUAL_PAIRING.md#factory-reset-2026-09-25)).
Older Sigils ignore the unknown type and see only the `Unpair` that follows.
Protocol version stays 1.

### Test harness: HarnessCommand and HarnessReport (2026-09-25)

The hardware test harness (`TestHarness/`) pairs like any Sigil. It pairs twice,
in fact: its station and soft-AP MACs are two virtual menu Sigils. The first
advertises `CAPABILITY_HARNESS` (0x80) in Hello. While that Sigil is online,
the Atlas lobby screen shows **Tests**, which lists the premade tests
(`HarnessTest`: radio check, 2-player game, 4-player game, rematch game,
soak ×5). A choice sends `HarnessCommand = 27` (run a test, or stop). The
harness answers with `HarnessReport = 14`: run state, test, last checkpoint
(`HarnessStep`), and steps passed and failed. It sends one at each checkpoint
and repeats the latest with every Hello. Atlas shows the report on the test
screen (`harness_link.cpp`, `touch_controls.cpp`) and never acts on it. The
harness plays only through `SelectAction` (and, from harness 0.8.0 on
2026-09-26, `PickerKey`, `LifeAdjust` and `LifeResponse`; its checkpoints
gained `PICKER`, `LIFE` and `LEAVE`), so it has no authority a real menu
Sigil lacks. Sigils ignore both packet types. Protocol version stays 1.
*Verified* on the owner's hardware (2026-09-25): Atlas receives the reports.

### Profile picker: ProfilePicker and PickerKey (2026-09-25)

An e-ink menu Sigil running firmware 0.8.0 or later lets a player choose who
joins (see [Physical profile selection](PHYSICAL_PROFILE_SELECTION.md#e-ink-sigil-picker-2026-09-25)).
Atlas sends it only to Sigils advertising `CAPABILITY_MENU` without
`CAPABILITY_HARNESS` (e-ink or OLED) whose Hello reports at least
`PICKER_MIN_FIRMWARE` (the capability byte is full, so the firmware version
gates it). `ProfilePicker = 33` is a 51-byte `ProfilePickerPacket`, told apart
by its length like `GameDisplay`: revision, mode (Closed, List, Confirm), a
notice (needs phone sign-in, unavailable, table full, failed), page and page
count, and up to three items (flags: guest, locked, already at the table;
12-character name). The Sigil answers with `PickerKey = 15`: the key and the
revision of the page it was pressed on; Atlas drops a key from an older page
and resends. Atlas resends the state (Closed when no picker is open) with
every Hello, so a Sigil never stays on a page Atlas forgot. Older Sigils
keep joining as a guest; a new Sigil with an older Atlas never
receives a page. The OLED draws it as a list and sends the same keys.
Protocol version stays 1; both device types need reflashing
to use it. Since 2026-09-30 the only gate is
`CAPABILITY_HARNESS`: every other Sigil gets the picker.

`MenuState2 = 34` (2026-09-25) replaces `MenuState` for the same 0.8.0+ Sigils
(since 2026-09-26 the 0.8.0 test harness too, which plays life changes and
Leave; the profile picker stays off for a Sigil with `CAPABILITY_HARNESS`): the 21 action bits of `MenuState` were all used, so
Since 2026-10-05, `MenuState2` carries 29 action bits and a 3-bit revision.
The unused wire default was removed to fit Commander damage (24) and Undo hit
(25); key defaults are derived by the Sigil menu. All prototype firmware must
be rebuilt together. `SelectAction` bits 11–15 carry the shown receiving player
for Commander actions. See [Sigil Commander damage](SIGIL_COMMANDER_DAMAGE.md).
Menu revisions now wrap at 8 for every Sigil so either encoding can name them.
Its first new action is `Leave = 21`, which Atlas offers only to these Sigils.

**Life on 0.8.0+ Sigils (2026-09-25).** `AdjustLife = 22` in `MenuState2` says
Left/Right may change life now (a living seat, a running or paused game, no win
claim or elimination). The Sigil batches presses and sends one
`LifeAdjust = 16` (player number, signed delta) `LIFE_ADJUST_COMMIT_MS` (2 s)
after the last change; Atlas dispatches `ChangeLife` for that Sigil's own player.
A pending life request aimed at one of a Sigil's players goes to it as
`LifeRequest = 35` (target, requester, 6-bit request tag, delta; 0 = none),
resent with every Hello; the Sigil answers `LifeResponse = 17` (target,
approve, tag) and Atlas dispatches `RespondLifeChange` only if the tag still
matches the pending request.

**Jewel color (2026-09-25).** `SeatColor = 36` (Atlas -> Sigil, value: seat,
set bit, 0xRRGGBB) carries the color a seated profile chose in the portal,
per seat, resent with every Hello; older Sigils ignore it. Sigils apply it only
to the calm Joined and Waiting cues; every action cue keeps its standard
color, and patterns still carry every meaning. Feature gate: state owner is
the profile (a luxury record `k<profileId>` on the microSD card, cached in RAM;
no card means no color); no new Intent (a profile setting, like accessibility,
via `GET`/`POST /api/session/jewel` for the signed-in profile only); rendering
on both Sigils' Jewel; no new dependency.

**Starting life and pass grace (2026-09-26, turntest notes).** `StartingLife =
37` (Atlas -> Sigil, value: the running or paused game's starting life, else 0)
is resent with every Hello to menu Sigils (0.8.0+); older Sigils ignore it. The
Sigil only draws with it: the life heart drained from the top below the
starting life and grew up to 1.5x at double it; 0 kept the plain heart. Since
the Brass look (2026-09-29) both Sigils draw a life dial from the same values:
its arc sweeps down below the starting life and an outer arc grows above it.
`PASS_GRACE_MS` (3 s) moved into `protocol.h` so both firmwares agree on it;
Atlas still owns the grace timer. A Sigil treats "Undo pass is in the menu" as
its pass being pending and shows PASSING, a green ring countdown, and (OLED)
"Click again to undo", where a second Select sends `CancelPass`. Since Sigil
0.9.4 Undo pass prefers the click on both Sigils' compass (the e-ink had it on
Left). Feature gate:
no new Intent (CancelPass already exists), no new state owner or persistence,
rendering on both Sigils and the Atlas screen ("Passing in Ns"), no new
dependency; the number and words still carry every meaning.

`PassPending = 38` (Atlas -> every menu Sigil, value: the passing player's
number, 0 = none, resent with every Hello) lets the whole table see a pending
pass, not only the passer: other Sigils show "P<n> PASSING" and an amber ring
countdown (the passer's stays green). Atlas also plays two new audio cues to
every seated Sigil and its own speaker: `PassPending` (two falling ticks) when
a pass is queued and `PassUndone` (two rising ticks) when the passer undoes it.
Audio stays supplementary: the screens and ring carry the same information.

### Sigil-rendered status light: LedState (2026-09-25)

`LedState = 25` (Atlas -> Sigil) carries the light's *meaning* instead of
channel levels: the cue (`LedCue`), overlay bits (`LedOverlay`), player number,
focused seat (A/B) and whether the Sigil is shared, the seated players' LED
style (Standard, Reduced motion, Monochrome-safe), and the time since the cue's
anchor (turn or countdown start) in 16 ms units so anchored patterns stay in
phase. `encodeLedState`/`decodeLedState` in `protocol.h` define the bit layout;
the cue and overlay enums moved there from Atlas's `led_cues.h`, which now
aliases them.

`TableClock = 39` (Atlas -> each LedState Sigil, value: Atlas's `millis()`)
makes Atlas the one clock for the table's lights. Atlas sends it ahead of the
LedState stream and then every `TABLE_CLOCK_INTERVAL_MS` (2 s). Each Sigil keeps
`offset = Atlas time - local time`, using the largest of its last four samples
(radio delay only ever makes a sample look early), and starts over when Atlas's
clock jumps (an Atlas restart). Every looping cue and overlay pattern (breathe,
blink, seat pulses, the unseated chase) runs on that table time, so all Sigils
show them in step. Sigil-local states (pairing, hold progress, pass
acknowledgement) stay on the Sigil's own clock. Until the first sample arrives
(or from an Atlas that predates it) patterns use local time as before.
Presentation only.

A Sigil advertising `CAPABILITY_LED_STATE` (0x20) gets one LedState per change,
plus a resend whenever its Hello arrives (about every 2 s), so a lost packet or
quiet reboot heals itself. Atlas still decides every cue (`selectSigilLedState`);
the Sigil only draws it (`Sigil/src/sigil_led.cpp`): full color on an RGB LED
(PWM on all three pins) and, on the E-ink Sigil, spatially on the NeoPixel Jewel
(player number as lit pixels, a shared seat as its ring half, the top overlay in
the center). Sigils without the bit keep the `SetBlue`/`SetRed`/`SetGreen`
stream, and a new Sigil still follows those packets from an older Atlas.
*Superseded 2026-09-30:* every Sigil has the Jewel and gets `LedState`; the
channel stream and the RGB-LED view are retired. The accessibility checks on
the ring (reduced motion never faster than 1 s; monochrome-safe pairs differ
by timing) now run in `Sigil/tests/host/led_scenarios.cpp`.
Pairing blink and the Pass acknowledgement flash stay Sigil-local. Protocol
version stays 1.

### Atlas lost (2026-09-28)

Before this, a Sigil kept showing its last state when Atlas went away (for
example "Ready for game" with a running light while Atlas was off). Now a
paired Sigil that hears nothing valid from its Atlas for `LINK_TIMEOUT_MS`
(7 s, in `protocol.h`; the same silence after which Atlas marks a Sigil
offline) shows **Atlas lost**. The timer starts at boot or pairing, so a Sigil
that starts with Atlas off shows it too. Logic: `Sigil/include/atlas_link.h`.

- **Screen:** the same words on both displays (2026-10-02): "Atlas lost /
  Searching for Atlas" (e-ink, sentence case) or "ATLAS LOST / Searching for
  Atlas" (OLED, capitals). Its only action is **Menu** on Up, which opens
  straight on the Device entries (Sleep, Unpair, Factory reset; Sigil
  0.9.10), so a Sigil whose
  Atlas is gone for good can still be unpaired or reset without the Pair
  button.
- **Light:** one orange pixel sweeping back and forth around the ring, center
  dark; with Reduced motion, two opposite pixels steady orange. Only the pairing
  blink outranks it.
- **Input:** Atlas's menu and life keys are ignored while Atlas is lost (only
  the device menu works, `SigilMenu::setOffline`); the stale menu, any unsent
  life change and any life request are dropped. Pair still works. A device-menu
  hold fills the ring over the searching light.
- **Serial:** `SIGIL|ATLAS|LOST`, then `SIGIL|ATLAS|RESTORED`.

The Sigil keeps sending Hello every 2 s. The first valid packet from its Atlas
restores the last screen; Atlas's answer to that Hello resends the light,
menu, screen and life state. No protocol change. Host-tested
(`led_scenarios`, `menu_scenarios`).

### Protocol 3: no compatibility before release (2026-09-30)

`VERSION` is 3. Owner decision: backward compatibility applies only to released
hardware; until then every board is reflashed together, and saved data needs
no migrations until TurnHub saves live stats. So nothing tolerates another
protocol version any more: `MIN_UPDATABLE_VERSION` and the frozen
secure-session handshake are gone, and a handshake, update offer or update
status from another version is refused. Retired packet numbers (3-8, 10-11,
20-22, 26) are free again. Pairings are one record each (MAC and pair key,
[Manual Pairing](MANUAL_PAIRING.md)), and Atlas's saved-data migrations and
the Android app's fallbacks for older Atlas firmware are removed. Upgrading
from earlier builds: reflash every board, factory-reset Atlas, pair again.
What release will need back is
listed in [Sigil OTA](SIGIL_OTA.md), "Version rules".

### Secure link, protocol version 2 (2026-09-29)

`VERSION` is 2. After pairing v2 (a key agreed at pairing, confirmed with a
4-digit code), each connection starts with `SecureHello` / `SecureHelloAck`,
and every other packet in both directions is sealed (AES-128-CCM, rising
counters, per-session keys). Cleartext packets from paired devices are dropped;
the keyless `PairRequest` / `PairAccept` (IDs 10 and 11) are retired. Details,
reasons and the bench results are in [Secure Link](SECURE_LINK.md). Version 1
and version 2 devices cannot talk to each other: reflash every device together.

### Unpair (2026-09-24)

`Unpair = 12` (Atlas -> Sigil, `value` 0) tells a Sigil that Atlas forgot it. The
Sigil honors it only from its saved Atlas MAC with its own Sigil ID, then erases
its pairing. It is best effort and unacknowledged. Older Sigils ignore it.
`UNPAIR_HOLD_MS` (3 s) is the Sigil's Pair hold that forgets locally; `FACTORY_RESET_HOLD_MS` (10 s)
erases the device (see [Manual Pairing](MANUAL_PAIRING.md#the-boot-button-pair-unpair-factory-reset-2026-09-30)).
`PAIRING_WINDOW_MS` is the Sigil's window and Atlas's default: 15 s until
2026-09-29, then 60 s, the minimum on every device (owner decision).
Protocol version stays 1.

### Hold timing (2026-09-24)

Sigils from firmware 0.5.4 advertise `CAPABILITY_INPUT_TIMING = 0x08` in Hello.
Atlas sends those peers `InputTiming = 24` in the existing seven-byte packet:
the long-press threshold in bits 0-15 and the win-hold threshold in bits 16-31
of `value`, both in milliseconds. `protocol.h` holds the defaults (2,000 and
5,000 ms), the limits (1,000-4,000 and 3,000-10,000 ms, 250 ms steps, win hold
at least 1,000 ms longer) and `validInputTiming()`, which both sides use. The
Sigil ignores invalid values and keeps them in RAM only, so a rebooted Sigil
uses the defaults until Atlas resends (every 10 s, or on change). Older Sigils
ignore the unknown type and keep their fixed thresholds. Protocol version stays
1. The thresholds only change when a gesture is recognized; the resulting
packets (`ActionLong`, `ActionWin`) and their Intents mean the same thing.
*Since 2026-09-30* every Sigil gets `InputTiming` (no capability bit); on the
menu they time the deliberate choices: the long hold confirms Leave,
Eliminate and Reset, the win hold confirms a win claim.
Atlas picks the values from the seated players' accessibility preferences
(ACCESSIBILITY.md).

### Running-game Sigil display (2026-09-22)

Sigils advertise `CAPABILITY_GAME_DISPLAY = 0x04` in Hello. Atlas sends those
peers `GameDisplay = 32` while running; existing seven-byte packet meanings,
protocol version 1, `DisplayState = 30`, and seat-name chunks (`31`) are unchanged.
Older peers retain their existing display. Lobby, pairing, ready, pause, and
game-over screens continue using the existing display/profile path. *Since
2026-09-30* every Sigil gets `GameDisplay` (no capability bit).

The new packed ESP-NOW datagram is 110 bytes, with little-endian integers:

| Offset | Field |
| --- | --- |
| 0�2 | Version, type (32), target Sigil ID |
| 3�6 | Existing encoded DisplayState |
| 7�9 | Commander boolean, visible source count (0�3), omitted source count |
| 10�26 | Focused participant: signed int32 life, 13-byte terminated name |
| 27�43 | Secondary participant, same layout (zero when absent) |
| 44�109 | Three sources: player number, 13-byte terminated name, two int32 damage values |

Atlas's existing `LedRenderer::syncDisplay` resolves the focused and secondary
players through `GameEngine::playersForController` and the existing focus rules.
Life comes directly from `lifeTotal(playerNumber)`; names come from the profile
ID captured in the game participant. Guest names fall back to `Player N`.
Name reads reuse the display cache until existing Hello/lifecycle invalidation,
so profile edits appear on the next Hello resynchronization. No life or damage
is stored in profiles or calculated by Sigil.

Commander rows contain `commanderDamage(focusedRecipient, source, 1/2)`.
Sources with no damage and the recipient itself are omitted. The first three
nonzero sources in player-number order are sent, including browser participants
and eliminated sources with recorded damage. Further sources produce a `+N`
indicator. Names are clipped to nine characters in rows. One commander appears
as `Jaime 6`, two as `Jaime 6/3`; damage only from slot 2 appears as `Jaime 3 (C2)`.
`CMD none received` distinguishes an empty Commander display from normal mode.

The 250�122 running screen emphasizes the name and life total, retains Sigil/host
and turn status, and removes P1/P2 badges. Large signed totals shrink to fit.
Shared Sigils emphasize the existing focused participant (active local seat,
otherwise the first local seat), with a smaller secondary name/life summary;
Commander rows belong to the focused participant only.

Snapshots use the existing serialized radio TX queue and paired MAC destination.
Sigil validates exact size, version/type, target, bounds and terminated names,
and accepts only its saved Atlas MAC. A critical section protects the rendering
snapshot across radio/display tasks. Each datagram is complete, so loss cannot
combine life from one update with damage or identity from another. No new
pairing or trust mechanism is introduced.

Atlas compares display snapshots before enqueueing. Existing Hello invalidation
resends current values for reconnect/loss recovery; queue admission failure is
retried by the next state comparison. Sigil compares both received and rendered
snapshots, so identical recovery packets do not refresh the panel. Profile-name
packets cannot force a running-game redraw. Updates received while the panel is
busy wake the display task to render the latest snapshot. There is no periodic
e-ink refresh timer. Delivery still depends on existing ESP-NOW/Hello recovery;
there is no new application acknowledgement protocol.

Host scenarios cover wire size/round-trip/validation, named recipients and
sources, received-vs-dealt semantics, partner damage, shared focus, negative life,
source overflow, unchanged snapshots, resynchronization and legacy peers.

### Historical note

The first ESP-NOW builds used broadcast discovery, so nearby devices became
associated by proximity. Explicit pairing replaced that on 2026-09-22. ESP-NOW
remains the prototype transport; the production transport is not frozen (see
the [verification backlog](VERIFICATION_BACKLOG.md)).

---

## Transport independence

A message such as `PASS_REQUEST` should mean the same thing whether it arrives through:

- Historical USB serial.
- A future BLE link.
- A local Wi-Fi transport.
- A browser HTTP/WebSocket API.
- A simulated controller in a test harness.

Transport-specific code should convert the incoming representation into a semantic event for Atlas.

This prevents a future transport change from requiring a game-engine rewrite.

---

## Proposed logical message families

**Status:** Working design, not frozen protocol numbers

### Device lifecycle

- `DEVICE_HELLO`
- `DEVICE_STATUS`
- `DEVICE_CAPABILITIES`
- `DEVICE_DISCONNECT`

### Pairing

- `PAIR_REQUEST`
- `PAIR_CHALLENGE`
- `PAIR_CONFIRM`
- `PAIR_ACCEPT`
- `PAIR_REJECT`
- `UNPAIR_REQUEST`

### Player/controller assignment

- `PLAYER_ASSIGN`
- `PLAYER_UNASSIGN`
- `PROFILE_SYNC`

### Game input

- `PASS_REQUEST`
- `ACTION_DOWN`
- `ACTION_UP`
- `ACTION_SHORT`
- `ACTION_LONG`
- `CLAIM_WIN`
- `CONCEDE`
- `NUDGE`

### Atlas state/output

- `STATE_SYNC`
- `TURN_CHANGED`
- `GAME_STATE_CHANGED`
- `PLAYER_STATE_CHANGED`
- `DISPLAY_STATE`
- `FEEDBACK_AUDIO`
- `FEEDBACK_LED`
- `FEEDBACK_HAPTIC`

### Firmware/update

- `FIRMWARE_INFO`
- `UPDATE_AVAILABLE`
- `OTA_BEGIN`
- `OTA_DATA` or transport-specific payload path
- `OTA_PROGRESS`
- `OTA_RESULT`

The exact message names, numeric IDs, packet encoding, and transport mapping remain to be finalized.

---

## Device identity

A production Sigil should have a stable device identity that is not merely "the third radio Atlas heard today."

Desired identity properties:

- Stable across reboot.
- Distinct from the temporary player/seat assignment.
- Available before joining a game.
- Able to report hardware revision.
- Able to report firmware/protocol version.
- Able to report capabilities.

A useful conceptual device record is:

```text
device_id
hardware_revision
firmware_version
protocol_version
capabilities
paired_atlas_id
friendly_name (optional)
```

Today a device is identified by its factory MAC address (Atlas shows its MAC as
its `THA-` ID); Hello reports firmware version and capabilities, and the
pairing record holds the peer's MAC and Secure Link key. A hardware revision
field waits for the first PCB revision.

---

## Explicit pairing

**Status:** implemented 2026-09-22 (see [Manual Pairing](MANUAL_PAIRING.md)); the
flow below is the design it follows.

First-time association should require deliberate user action rather than passive proximity discovery.

Conceptual flow:

```text
Unpaired Sigil
    -> user presses/holds Pair
    -> Sigil enters limited pairing window

Atlas
    -> user enables Add/Pair Sigil mode
    -> sees pairing request
    -> validates/accepts device

Both devices
    -> store relationship
    -> exchange versions/capabilities
    -> transition to paired-ready state
```

Normal startup after pairing:

```text
Sigil boots
    -> loads paired Atlas identity
    -> discovers/reaches that Atlas using chosen transport
    -> validates relationship
    -> exchanges version/capability status
    -> receives state assignment
    -> ready
```

### Why this matters

Explicit pairing prevents a table in a game store from accidentally adopting a Sigil belonging to the next table simply because it is nearby.

It also gives us a natural place for:

- Device naming.
- Firmware compatibility checks.
- Hardware capability negotiation.
- Re-pair/reset workflows.
- Future security/authentication.

---

## Capabilities

Atlas reacts to declared capabilities rather than scattering firmware-version
checks across the codebase. Since 2026-09-30 Atlas assumes the baseline every
Sigil has and the Hello capability byte carries only what varies (see
"Baseline Sigil" above). A future Sigil revision adds a capability by taking a
free bit, for example battery reporting or a haptic motor.

---

## Synchronization model

Atlas is authoritative. A controller should be able to recover by asking for a state snapshot after reconnecting rather than reconstructing the game from old local assumptions.

Desired pattern:

```text
controller reconnects
    -> HELLO / identity
    -> Atlas validates pairing
    -> version/capability exchange
    -> STATE_SYNC
    -> normal event flow resumes
```

This is more robust than relying on every incremental event having arrived successfully during a disconnect.

---

## Reliability rules to define later

The production protocol should explicitly decide:

- Which messages require acknowledgement.
- Which actions need deduplication IDs.
- Retry timing.
- Reconnection behavior.
- Ordering guarantees.
- Maximum payload size.
- State snapshot format.
- Compatibility across protocol versions, and behavior when Atlas and a Sigil
  run different versions (today: one version everywhere, see "Protocol 3").

Security and integrity ([Secure Link](SECURE_LINK.md)) and clearing pairings on
factory reset ([Manual Pairing](MANUAL_PAIRING.md)) are settled. The rest should
be specified before calling the protocol production-stable.

