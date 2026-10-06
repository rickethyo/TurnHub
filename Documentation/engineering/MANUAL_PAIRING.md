# Manual prototype pairing

## Current: pairing v2 with a code check (2026-09-29)

Design and reasons are in [Secure Link](SECURE_LINK.md).

1. In the Atlas lobby, tap **Menu → Pair a Sigil**. Atlas's pairing window opens
   (60 s by default and at least; 90 or 120 s if set in Device Settings); the Atlas screen shows
   the countdown and Atlas's on-board LED blinks red.
2. Press the Sigil's Pair button (BOOT; or hold the thumbstick click 3 s on an
   unpaired Sigil). It blinks red and broadcasts `PairRequest2`
   with a fresh X25519 key every 2 s until answered.
3. Atlas answers `PairAccept2` and both sides derive the same pair key and a
   4-digit code. The Sigil shows the code; the Atlas screen shows **Pair Sigil N**
   with the same code, **Codes match** and **Reject**. The portal's Device
   Settings lists it too (Admin, verified at the table).
4. **Codes match** (either place) stores the Sigil's MAC and pair key on Atlas
   as one record (`th_pair/s<slot>`) and tells the Sigil, which stores Atlas's
   MAC, its slot and the key as one record (`th_pair/atlas`) and carries on
   (`SIGIL|PAIR|SUCCESS|SECURE`). A record of the wrong size isn't a pairing
   and is dropped at boot.
5. **Reject**, 60 s without an answer (Atlas), or 65 s (the Sigil's own limit)
   stores nothing; a Sigil that was already paired keeps its old pairing.
   Leaving the lobby rejects every waiting Sigil.

Since the secure link (protocol `VERSION` 2, 2026-09-29) this is the only way
to pair: once paired, a Sigil starts a secure session with `SecureHello` and
every other packet travels sealed (see [Secure Link](SECURE_LINK.md)). The old
keyless `PairRequest` below is retired. Since 2026-09-30 a pairing and its key
are one record, so a keyless pairing can't exist; records from earlier builds
(`th_pair_v1`) are ignored, and those devices simply pair again.

## Pairing rules

- Atlas keeps up to eight pairings, offline ones included, in NVS namespace
  `th_pair` (`s0`-`s7`); a Sigil keeps its Atlas under `atlas`. Pairing never
  touches profiles or statistics.
- Atlas stays open for its whole window and can pair several Sigils; tapping
  Pair again restarts it. The Sigil's own window is 60 s, so with a longer
  Atlas window open Atlas first. Leaving the lobby closes the window, and a
  reboot never opens one.
- Hello never registers an unknown device. A paired Sigil talks only to its
  saved Atlas, and Atlas accepts packets only from saved Sigils over their
  secure session. Pairing again replaces a Sigil's saved Atlas; a failed or
  timed-out attempt keeps the old pairing. A full table of eight or a storage
  failure refuses the pairing.
- Radio callbacks only enqueue packets; NVS work runs in `loop()`.

History: the first manual pairing (2026-09-22) replaced proximity discovery
with a 15 s window on both devices and an unauthenticated MAC association.
The Secure Link's code check replaced it on 2026-09-29, and the 60 s minimum
window arrived the same day.

## The BOOT button: pair, unpair, factory reset (2026-09-30)

One button, three gestures, on every board that has it (`three_part_button.h`,
constants `UNPAIR_HOLD_MS` 3 s and `FACTORY_RESET_HOLD_MS` 10 s in `protocol.h`).
It replaces the old 10 s hold that only forgot the pairing. Pairing now happens
when the button is *released* before 3 s, so a hold never also opens the pairing
window.

| Gesture | Sigil (Pair button = BOOT, GPIO0) | Atlas (BOOT, GPIO0) |
|---|---|---|
| Quick press (released before 3 s) | Opens the Sigil's 60 s pairing window | Pair a Sigil: opens the pairing window (lobby only, like the touchscreen's button) |
| Hold 3 s (tone) | Erases the saved Atlas pairing, shows "Unpaired" (`SIGIL|PAIR|BUTTON|HOLD_UNPAIR`) | Forgets every paired Sigil, lobby only and never one with seated players (`ForgetPairing` Intent, origin `AtlasHardware`) |
| Hold 10 s (tone) | Erases the whole NVS partition and restarts as new (`SIGIL|FACTORY_RESET|ERASING|BUTTON`) | Factory reset Atlas: erases the microSD data and NVS, restarts as new. No Admin or table code, and it works in any state, mid-match included: it is the recovery path when the touchscreen stops responding (`FactoryReset` Intent, origin `AtlasHardware`) |

- A hold passes through the shorter gestures: a 10 s hold unpairs at 3 s first,
  then factory resets. Release at the tone to stop at that step.
- A button already down when the board starts (BOOT held through a reset, which
  enters the ROM downloader) is ignored until it is seen released.
- Since Sigil 0.9.10 (2026-10-06) Unpair, Factory reset and Sleep are under
  **Menu (Up) > Device** on both displays, in any stage, including games; see
  `Sigil/DISPLAY.md`, Menus. The history below describes earlier layouts.
- Since firmware 0.9.4 (2026-10-02) both Sigils also offer **Factory reset**
  (hold Down 5 s) in the device menu, and since 0.9.6 **Unpair** (hold the
  click 3 s): outside a game, **Menu** on the first free of Up/Down opens it
  (see `HARDWARE_REFERENCE.md`, menu controls). Both are device-local: never in
  Atlas's menu mask, never sent to Atlas, only offered while the Sigil is
  paired and its menu is active. On the e-ink there is no Menu key in a game,
  so the button hold is the way; since Sigil 0.9.8 the OLED's Menu list (Up,
  any time) ends with Sleep and Device recovery, a second list holding Unpair
  and Factory reset, held the same 3 s and 5 s on the click. On the **Atlas lost** screen (owner 2026-10-02) Menu is on
  Up whatever was happening, so a Sigil whose Atlas is gone can be unpaired or
  reset from its keys.
- **Joystick backup for Pair (Sigil 0.9.6, owner 2026-10-02).** A case can hide
  BOOT, so an *unpaired* Sigil also opens its pairing window when the
  thumbstick click is held 3 s (`SIGIL|PAIR|JOYSTICK_HOLD`; main.cpp's
  `updateJoystickPair`, one window per press, ring fills while held). Paired,
  the click is the menu's. The Unpaired screen reads "Hold joystick to enter
  pairing mode". Unpair from the device menu, then hold the joystick, to move a
  cased Sigil to another Atlas.
- **Atlas touchscreen (Atlas 0.6.4, owner 2026-10-02).** Menu > **Device**
  (between games) holds **Unpair Sigils** (3 s, lobby only) and **Factory
  reset** (10 s): the BOOT button's 3 s and 10 s gestures as the same
  `AtlasHardware` Intents (`ForgetPairing` all, `FactoryReset` Atlas), for
  when BOOT is inside a case. Like BOOT they need no Admin or table code: the
  screen is physical presence. That is deliberate (owner request) and means
  anyone at the table can erase Atlas between games; the 10 s hold, the
  Menu > Device path and the countdown hint are the guard.
- **Sleep (owner 2026-10-02).** Both device menus have **Sleep**, a tap, and
  waking is a restart (deep sleep: saved data and pairings survive, RAM does
  not).
  - Sigil: device menu, **Up** on the e-ink, the **Sleep** row of the OLED's
    Menu list. The e-ink keeps an "Asleep / Click joystick to
    wake" card; the OLED and ring go dark. A joystick click (GPIO32) or BOOT
    wakes it, and it reconnects. Atlas just sees it go quiet.
  - Atlas: Menu > Device > **Sleep**, between games, touchscreen only (the
    `Sleep` Intent; the web cannot send it). The notice "Going to sleep. Touch
    the screen to wake" shows for 2.5 s, then the backlight, RGB LED and
    amplifier are held off. A touch (the XPT2046 pen interrupt, GPIO36) or
    BOOT (GPIO0) wakes it. The lobby empties and phones sign in again; Sigils
    show Atlas lost until it is back. Refused during a firmware update or a
    scheduled factory reset, and a factory reset is refused while Sleep is
    pending.
  - Current draw asleep is *Needs verification*: the DevKit regulator, USB
    bridge, power LED and the Sigil's NeoPixels still draw a few mA, so this is
    not yet a battery "off" (a soft-latch power switch would be).
- The test harness stops at unpair (3 s); it has nothing else to erase.

## Forgetting a pairing (2026-09-24)

- **On a Sigil:** hold its Pair button for 3 seconds
  (`UNPAIR_HOLD_MS`; a quick press pairs, 10 s factory resets, see above).
  At 3 seconds the Sigil erases its saved `atlas` binding, turns its LEDs off,
  returns to the defaults for hold timing and shows "Unpaired"
  (`SIGIL|PAIR|FORGOTTEN|BUTTON`). Atlas keeps its record; forget it there too,
  or pair the Sigil again (the same MAC reuses its slot).
- **On Atlas (admins):** Device Settings -> Paired Sigils has Forget for each
  Sigil and Forget all Sigils (`POST /api/device/forget`). This is the
  `ForgetPairing` Intent: Admin permission re-checked, lobby only, refused while
  anyone is seated on that Sigil. Atlas removes `th_pair/s<N>`, frees the slot,
  releases that Sigil's saved seat bindings (its custom name stays) and sends a
  best-effort `Unpair = 12` packet. A Sigil that hears it from its saved
  Atlas erases its own pairing (`SIGIL|PAIR|FORGOTTEN|ATLAS`); one that misses it
  stays paired to a record Atlas no longer has, so its packets are ignored until
  it is re-paired or held-to-forget. A storage failure keeps the pairing.
- **Pairing window:** Device Settings also has the Atlas pairing window (60, 90
  or 120 s; `GET/POST /api/pairing`, `ConfigurePairing` Intent), stored in NVS
  `turnhub/pairwin`. Missing or unreadable means 60 s.

## Factory reset (2026-09-25)

Device Settings has **Factory reset** for each paired Sigil and **Factory
reset Atlas** (`POST /api/device/factory-reset`, `module=<id>` or `atlas=1`).
Both are the `FactoryReset` Intent. It needs Admin permission, that Admin
verified at the table (the presence code), and it is never allowed
during a match. Atlas re-checks all of that in the handler.

- **A Sigil** must be free (nobody seated, lobby only). Atlas sends it
  `FactoryReset = 28` carrying `FACTORY_RESET_CONFIRM` ("FRES"), queued ahead of
  the `Unpair` that forgetting it sends, then forgets it exactly as Forget
  does. The Sigil acts only on that packet from its saved Atlas, for its own
  ID, with the confirmation value. It then erases its whole NVS partition,
  pairing included, logs `SIGIL|FACTORY_RESET|ERASING` and restarts as new.
  If the Sigil is out of range, Atlas only forgets it and says so; holding the
  Sigil's Pair button for 3 s clears that side. The test harness clears only
  that virtual Sigil's pairing.
- **Atlas** (lobby or game over) replies first, then after 1.5 s erases its whole
  NVS partition (`nvs_flash_erase`, `factory_reset.cpp`) and restarts. That
  removes every profile, PIN hash, core statistic, account, pairing, the Wi-Fi
  password (back to the default), game settings, speaker volume and touch
  calibration, so Atlas asks to calibrate again. Since 2026-09-29 (owner
  decision) it first empties the microSD card: `wipeSdCard()` (`sd_card.cpp`)
  deletes everything on the card under the card lock, logs
  `ATLAS|SD|WIPE|REMOVED|<n>` and unmounts it, so no detailed statistics or
  diagnostic logs outlive their profiles. This framework (Arduino-ESP32 2.0 /
  IDF 4.4) cannot reformat a card, so it is emptied rather than formatted;
  Atlas recreates `/turnhub` at the next mount. No card, or a failing one,
  never stops the NVS erase. The portal asks the Admin to type RESET first.
