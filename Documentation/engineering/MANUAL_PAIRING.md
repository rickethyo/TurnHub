# Manual prototype pairing

Implemented 2026-09-22 after the owner confirmed the hardware buttons report correctly.
This supersedes the five-second visual mock and the planned temporary boot trigger.

## Operation

1. Install the updated firmware on Atlas and each Sigil.
2. In the Atlas lobby, press Atlas Pair (GPIO32). Its Pair LED blinks for the
   pairing window: 15 seconds by default, or 30/60 seconds if an admin chose that
   under Device Settings (2026-09-24).
3. Press the Sigil Pair button (GPIO19). Its red LED blinks while requesting pairing.
4. Sigil logs `SIGIL|PAIR|SUCCESS`, stops blinking, and requests its Atlas state/profile.
   Joining the table still uses the existing Action/profile workflow.

Either button may be pressed first if the windows overlap. The Sigil's own
window is always 15 seconds, so with a longer Atlas window press Atlas first,
then the Sigil. Atlas stays open for its window and can accept multiple Sigils; only enable pairing on the intended
Atlas nearby. Repeated Atlas presses restart its window. Repeated Sigil presses
while pairing do not extend its window. Sigil retries every two seconds. Timeout
restores its previous red LED state and preserves any previous association.
Atlas closes pairing when leaving the lobby. Reboots never open pairing windows.

## Storage and packet rules

Atlas stores each accepted MAC under a stable slot in NVS namespace `th_pair_v1`
(`s0` through `s7`). Sigil stores the Atlas MAC plus assigned slot in the same
namespace under `atlas`. Neither operation changes player profiles or statistics.
Old automatic associations were never durable; pair each device once after updating.

PairRequest (10) carries a fresh per-window random token; PairAccept (11) echoes
it. Sigil accepts that response only in its current local window with the matching
token and a valid slot. Atlas persists before accepting; Sigil persists before
reporting success. Radio callbacks only enqueue packets; NVS work runs in loop().
Queued Atlas requests received before opening its window are rejected.

Hello no longer registers unknown devices. Saved Sigils unicast Hello to their
Atlas; other Sigil packets require the saved sender MAC and assigned slot. Sigils
ignore commands from other Atlas MACs. Restored Atlas records remain offline until
traffic arrives. An unknown device cannot consume a slot through Hello/gameplay.

Pressing Pair again on both devices can replace the Sigil's saved Atlas. Failed
or timed-out attempts keep its previous pairing. The previous Atlas retains its
record until an admin forgets it (below). Capacity is eight saved
Sigils, including offline ones. Capacity/storage failure does not grant association
and reports a serial rejection/error. If an acceptance is lost, retries during
the Atlas window are idempotent; after expiry, reopen both windows to retry.

This is deliberate prototype association, not encrypted or authenticated device
trust: ESP-NOW remains unencrypted and MAC spoofing is not prevented. The token
correlates responses; it is not a cryptographic identity proof. Production trust
and factory-reset integration remain future work.

## Forgetting a pairing (2026-09-24)

- **On a Sigil:** hold its Pair button for 10 seconds
  (`FORGET_PAIRING_HOLD_MS`). The press first opens the pairing window as usual;
  at 10 seconds the Sigil erases its saved `atlas` binding, turns its LEDs off,
  returns to the defaults for hold timing and shows "Unpaired"
  (`SIGIL|PAIR|FORGOTTEN|BUTTON`). Atlas keeps its record; forget it there too,
  or pair the Sigil again (the same MAC reuses its slot).
- **On Atlas (admins):** Device Settings -> Paired Sigils has Forget for each
  Sigil and Forget all Sigils (`POST /api/device/forget`). This is the
  `ForgetPairing` Intent: Admin permission re-checked, lobby only, refused while
  anyone is seated on that Sigil. Atlas removes `th_pair_v1/s<N>`, frees the slot,
  releases that Sigil's saved seat bindings (its custom name stays) and sends a
  best-effort `Unpair = 12` packet. A Sigil on 0.5.5+ that hears it from its saved
  Atlas erases its own pairing (`SIGIL|PAIR|FORGOTTEN|ATLAS`); one that misses it
  stays paired to a record Atlas no longer has, so its packets are ignored until
  it is re-paired or held-to-forget. A storage failure keeps the pairing.
- **Pairing window:** Device Settings also has the Atlas pairing window (15, 30 or
  60 seconds; `GET/POST /api/pairing`, `ConfigurePairing` Intent), stored in NVS
  `turnhub/pairwin` as `{schema 1, seconds}`. Missing or unreadable values mean 15 s.

*Needs verification* on hardware: host scenarios cover the Atlas handlers, HTTP
routes, storage codec and portal; the Sigil hold and `Unpair` handling were built
(`sigil`, `sigil-wokwi`) but not flashed.

## Factory reset (2026-09-25)

Device Settings has **Factory reset** for each paired Sigil and **Factory
reset Atlas** (`POST /api/device/factory-reset`, `module=<id>` or `atlas=1`).
Both are the `FactoryReset` Intent. It needs Admin permission, and admin
unlocked on the Atlas screen (someone at the table), and it is never allowed
during a match. Atlas re-checks all of that in the handler.

- **A Sigil** must be free (nobody seated, lobby only). Atlas sends it
  `FactoryReset = 28` carrying `FACTORY_RESET_CONFIRM` ("FRES"), queued ahead of
  the `Unpair` that forgetting it sends, then forgets it exactly as Forget
  does. The Sigil acts only on that packet from its saved Atlas, for its own
  ID, with the confirmation value. It then erases its whole NVS partition,
  pairing included, logs `SIGIL|FACTORY_RESET|ERASING` and restarts as new.
  If the Sigil is out of range, Atlas only forgets it and says so; holding the
  Sigil's Pair button for 10 s clears that side. The test harness clears only
  that virtual Sigil's pairing.
- **Atlas** (lobby or game over) replies first, then after 1.5 s erases its whole
  NVS partition (`nvs_flash_erase`, `factory_reset.cpp`) and restarts. That
  removes every profile, PIN hash, core statistic, account, pairing, the Wi-Fi
  password (back to the default), game settings, speaker volume and touch
  calibration, so Atlas asks to calibrate again. The microSD card is **not**
  erased. The portal asks the Admin to type RESET first.

*Needs verification* on hardware: host scenarios cover permission, the unlock
window, refusal while seated or in a match, the packet and forget, and the
delayed Atlas erase. The Sigil and Atlas erases are firmware-only and untested.

## Verification

Atlas and Sigil PlatformIO builds pass. Native gameplay/storage regressions pass,
including new PairRequest origin authorization, unavailable radio, 15-second
expiry, clock rollover, and gameplay exclusion scenarios. These use the transport
fixture and do not prove on-air delivery or physical NVS persistence.

Bench acceptance still required (firmware has not been flashed by this change):

- Boot an unpaired Sigil: no discovery/adoption; screen instructs Pair on both.
- Press only one Pair button: no association; timeout at 15 seconds.
- Press both: success, then normal join/pass/display behavior.
- Reboot Atlas and Sigil independently: same slot, no new Pair press needed.
- Try unknown Hello/actions and commands from another Atlas: no adoption/control.
- Pair during a running game: rejected; a lobby window closes at game start.
- Retry after lost acceptance; re-pair a saved device; check two nearby Atlases.
- Fill all eight slots and exercise storage failure: no unsaved association.
