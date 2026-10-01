# Code review, 2026-10-01 (whole repository)

Scope: Atlas, Sigil, `shared/`, TestHarness, Android, `protocol/`, `tools/`
and the docs they touch, read for bugs, unintended behavior, wording,
clunky behavior and misplaced code, with small improvements made where the
code was open anyway. Done while the prototype Sigils are out of service
(rewired into a new enclosure), so nothing here is hardware-tested.

Branch: `ccr-490e3776-lytu1y`. Earlier review: [2026-09-26](CODE_REVIEW_2026_09_26.md).

## Checks run

| Check | Result |
|---|---|
| Atlas host suites (`run-linux.sh`, ASan + UBSan) | Pass |
| Sigil host suites (`run-linux.sh`, ASan + UBSan) | Pass |
| `audit_adapters.py`, `check_client_contract.py` | Pass (27 adapters; 10 responses, 9 fixtures) |
| Firmware builds: `atlas`, `sigil`, `sigil-oled`, `sigil-wokwi`, `harness` | Pass (PlatformIO 6.2.0 on Linux) |
| Android `testDebugUnitTest` | Pass (139 tests) |
| `tools/firmware/test_thfw.py`, `export_manual.py --check` | Pass |
| KiCad `verify_schematic.py` | Not run (needs kicad-cli netlist exports) |

All of the behavior below is *Host-tested only* unless marked firmware-only;
each needs verification on hardware.

## Bugs fixed

1. **E-ink Sigils could never use Switch seat.** The compass layout had no key
   for `SigilAction::SwitchSeat`, so the playtest #3 fix (show and adjust the
   other seat) worked only on the OLED list. It now takes whatever is left of
   click, Up and Down, never Left/Right (life). Waiting on another player's
   turn, that is the click. `Sigil/src/sigil_menu.cpp`, menu host test.
2. **Leaving from a phone left the Sigil seat bound.** A profile seated on a
   Sigil that left from its phone (or was removed by a Game Master) kept the
   Sigil's seat binding: the Sigil still showed the name and its next Join
   seated the same profile. Drop seat B had the same gap. Both now free the
   binding like the Sigil's own Leave (`TurnHubControllers::releasePhysical`).
3. **A Sigil reboot dropped seated players' profiles.** Atlas cleared a Sigil's
   seat bindings whenever it asked for its names after starting up, even while
   seated in the lobby, so a power blip turned its players into guests. A
   seated Sigil now keeps them (`SigilBus::setSeatedQuery`). Firmware-only.
4. **Turn statistics could record a ~49-day turn.** `passTurn`, `resume` and
   the end-of-game pause used plain unsigned subtraction, so a clock sampled
   before a fresh stamp wrapped; the rest of `GameEngine` already used the
   wrap-safe helper. They do now, and `passTurn` checks for a next player
   before touching statistics.
5. **Return from Paused could leave a Sigil on the paused screen.** The Sigil
   drops its game view on any `DisplayState`; Atlas only resent the game view
   if the snapshot changed, and relied on an `invalidateAll()` at resume. The
   renderer now also compares the display payload.
6. **Game start during an Atlas firmware upload.** Start waited for a Sigil
   update but not for an Atlas OTA upload, which restarts Atlas when it lands.
7. **The shipped Wi-Fi password could be stored as the owner's**, through
   Network settings, breaking "a stored password always means the owner chose
   it". Both password paths now refuse it.
8. **The profile picker never said "Table is full"**; it now does when that is
   why a join failed.

## Load (playtest crash with several phones)

The 4-phone crash is still unexplained (see the 2026-09-29 playtest notes,
section G). Three per-request or per-loop costs were cut:

- The life-expiry tick dispatched every loop pass re-encoded and CRC'd the
  recovery checkpoint each time; it now skips that when nothing expired (the
  once-a-second recovery poll still runs).
- Seat colors (profile lookups and String churn per seat) were re-read every
  loop pass; now every 500 ms.
- `/api/seats` and `/api/devices` re-resolved every session for every seat;
  they now resolve sessions once per request (`resolveAllSessions`).

## Small improvements

- Yu-Gi-Oh! life in hundreds everywhere: Atlas Player screen (100/1000),
  Android life pad and request dialog, and Sigils (steps of 100 when a game
  starts at 1000+ life; the ring counts steps).
- Firmware upload errors read as sentences (`errorMessage`) on the Atlas and
  Sigil update pages instead of codes such as `olderVersion`.
- The Atlas screen's Paused detail names an open elimination.
- Durations spelled out in messages are tied to their constants with
  `static_assert` (pass grace, life approval, the 3 s Pair hold); the claim
  message uses `CLAIM_TIMEOUT_MS`.

## Wording and placement

Stale "admin unlock" text (one in a portal dialog), "the table host" (portal
hint, Android timer editor), "press Action" (claim flow, now Link phone), the
retired Action button in the PASS reply, "not used by the radio yet" (secure
link self test), the RGB LED pins, radio version 1/2 in the client contract
(it is 3), and comments that had drifted away from their code (pairing window,
factory reset, presence-code screens, InputTiming, a profile-store migration
that no longer exists). `/api/status` lost its dead `"host": -1`.

## Found, not changed

- **`DISPLAY_FLAG_HOST`** is never set; the Sigils still draw a crown for it.
  Left for the "Retire host" work in `STAGED_CHANGES.md` (now commented).
- **`hostModuleId` and `host`** remain in the client contract for the same
  reason.
- **Session lookups read NVS** (`TurnHubAccounts::load`, `primaryAdmin`) on
  every authenticated request. Cheap per call, but a RAM cache would remove it
  if the multi-phone load is still a problem after a logged replay.
- **The user manual** (`Documentation/User Manual/*.docx`) does not yet
  describe the changes above, nor the playtest items listed in
  `STAGED_CHANGES.md`.
- **No firmware version bump.** Raise `PATCH` before installing these builds
  over OTA on boards that already run 0.9.3.
