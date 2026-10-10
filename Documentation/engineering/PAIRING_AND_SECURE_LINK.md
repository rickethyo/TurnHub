# Pairing and the Secure Link

How a Sigil joins an Atlas, how the radio link is encrypted, and how
pairings are forgotten or erased. Packet numbers are in
[Radio Protocol](RADIO_PROTOCOL.md).

## Pairing a Sigil

1. In the Atlas lobby, tap **Menu > Pair a Sigil** (or quick-press Atlas's
   BOOT button). Atlas's window opens for 60 s by default (90 or 120 s in
   Device Settings; never below 60 s, `PAIRING_WINDOW_MS`). The screen counts
   down and Atlas's on-board LED blinks red.
2. Quick-press the Sigil's Pair button (BOOT), or on an unpaired Sigil hold
   the thumbstick click 3 s (for a case that hides BOOT). The Sigil blinks
   red and broadcasts `PairRequest2` with a fresh X25519 public key every 2 s
   for its own 60 s window.
3. Atlas answers `PairAccept2`. Both sides derive the same pair key and a
   4-digit code. The Sigil shows the code; the Atlas screen shows **Pair
   Sigil N** with the code, **Codes match** and **Reject**. The portal's
   Device Settings lists waiting Sigils too, with the code as text (Admin
   verified at the table).
4. **Codes match** (the `PairConfirm` Intent, from either place) stores the
   Sigil's MAC and pair key on Atlas as one record (`th_pair/s<slot>`) and
   sends `PairConfirmed`, sealed with the new key. The Sigil stores Atlas's
   MAC, its slot and the key (`th_pair/atlas`) only if that checks out
   (`SIGIL|PAIR|SUCCESS|SECURE`).
5. **Reject**, 60 s without an answer, or the Sigil's own 65 s limit stores
   nothing; an already-paired Sigil keeps its old pairing. Leaving the lobby
   rejects every waiting Sigil.

Rules:

- Atlas keeps up to eight pairings, offline ones included. Pairing never
  touches profiles or statistics. The same MAC reuses its slot. A full table
  or a storage failure refuses the pairing.
- Hello never registers an unknown device. A paired Sigil talks only to its
  saved Atlas, and Atlas accepts packets only from saved Sigils over their
  secure session.
- The window stays open for several Sigils; tapping Pair again restarts it.
  A reboot never opens one. Several Sigils can wait at once, each with its
  own code.
- Radio callbacks only enqueue; NVS work runs in `loop()`.
- **Spares** (owner, 2026-10-06) have no screen, so they send
  `PairRequestSpare` and Atlas confirms them during its window without the
  code check. That pairing is marked spare-only (`th_pair/p<slot>`): Atlas
  drops all its input but Hello and update status, and forgets it if it
  later announces a normal Sigil, which then pairs again with the code check.

## Forgetting and erasing

**The BOOT button** (`three_part_button.h`, on every board): a hold passes
through the shorter gestures, and a tone marks each step.

| Gesture | Sigil (BOOT, GPIO0) | Atlas (BOOT, GPIO0) |
|---|---|---|
| Quick press (released before 3 s) | Opens the Sigil's pairing window | Opens Atlas's pairing window (lobby only) |
| Hold 3 s (`UNPAIR_HOLD_MS`) | Forgets its saved Atlas, shows "Unpaired" | Forgets every Sigil (lobby only, never one with seated players) |
| Hold 10 s (`FACTORY_RESET_HOLD_MS`) | Erases its NVS and restarts as new | Factory reset Atlas, in any state: the recovery path when the touchscreen stops responding |

A button already down at boot (held through reset into the ROM downloader)
is ignored until released.

**Other routes:**

- **Sigil Device menu** (Menu on Up, then Device; Sigil 0.9.10): Sleep,
  Unpair (hold the click 3 s) and Factory reset (hold 5 s). Device-local:
  never sent to Atlas. On the **Atlas lost** screen Menu opens straight on
  these, so a Sigil whose Atlas is gone can still be unpaired or reset.
- **Atlas touchscreen**, Menu > Device, between games: **Unpair Sigils**
  (hold 3 s, lobby only), **Factory reset** (hold 10 s) and **Sleep**. Like
  BOOT they need no Admin or table code: the screen is physical presence, so
  anyone at the table can erase Atlas between games (owner's choice); the
  long hold is the guard.
- **Portal, Device Settings** (Admin): Forget per Sigil or Forget all
  (`ForgetPairing`, lobby only, refused while anyone is seated on that
  Sigil). Atlas removes the record, releases that Sigil's seat bindings
  (its name stays) and sends a best-effort `Unpair`. A Sigil that misses it
  stays paired to a record Atlas no longer has; its packets are ignored until
  it is re-paired or held to forget.
- **Portal factory reset** (Admin verified at the table, never during a
  match): for a free Sigil, Atlas sends `FactoryReset` ("FRES") ahead of the
  `Unpair`; an out-of-range Sigil is only forgotten. For Atlas (lobby or game
  over; the portal asks for RESET to be typed), Atlas replies, waits 1.5 s,
  empties the microSD card (the portal stays in firmware), erases NVS and restarts:
  every profile, statistic, account, pairing, the Wi-Fi password, settings and
  touch calibration go, and setup starts again at Welcome.

## Sleep

Both Device menus have **Sleep**: deep sleep, so waking is a restart and
saved data and pairings survive. A Sigil wakes on the joystick click or BOOT
(the e-ink keeps an "Asleep / Click joystick to wake" card). Atlas sleeps
from the touchscreen only, between games, and wakes on a touch or BOOT; the
lobby empties, phones sign in again and Sigils show Atlas lost until it is
back. Sleep current is *Needs verification*; the DevKit regulator, USB bridge
and NeoPixels still draw a few mA, so this is not a battery "off".

**Auto sleep** (2026-10-08) is the same sleep, chosen by the device itself. A
Sigil sleeps after 10 minutes with no key, joystick or Pair press outside a
game (Ready, the lobby, game over), or after 3 minutes when Atlas is lost or
the Sigil is unpaired; never mid-game, while pairing or during an update
(`idle_sleep.h`). Atlas sleeps by itself only on its battery (HARDWARE.md,
"Auto sleep on battery"); its Sigils then see Atlas lost and follow. The
Atlas-managed light sleep is in [Planned Designs](PLANNED_DESIGNS.md).

## The Secure Link

Every packet between Atlas and a paired Sigil is encrypted and authenticated
(protocol 2, 2026-09-29). Before it, anyone could send PASS or a win claim as
any Sigil by copying its MAC, and Sigil OTA needs to send the Wi-Fi password
over the radio.

**Why our own encryption, not ESP-NOW's.** The Arduino core we build with
(2.0.17) allows only 7 encrypted ESP-NOW peers (`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`,
fixed in the prebuilt Wi-Fi libraries), and Atlas needs 8. ESP-NOW's
encryption also gives no key agreement and its replay behavior across
restarts is undocumented. Rebuilding the core, rotating the 7 slots or
leaving some Sigils in the clear were rejected. Revisit only if a framework
allows enough encrypted peers and its replay behavior is verified.

**Pairing key agreement.** Both sides make fresh X25519 key pairs; the shared
secret, both MACs and both public keys go through HMAC-SHA256 to give the
16-byte pair key and the 4-digit code. Private keys are discarded. Listening
to pairing reveals nothing; the code check stops an active impersonator
during the window.

**Sessions.** Each connection (boot, Atlas restart, lost link) starts with
`SecureHello` (23 bytes: Hello info, an 8-byte nonce, a MAC under the pair
key) and `SecureHelloAck` (27 bytes). Session key = HMAC(pair key, both
nonces), so a packet recorded in an earlier session can't be replayed. A
Sigil sends SecureHello when it has no session or Atlas has been quiet for
more than two Hello intervals; otherwise its keep-alive is a sealed Hello.

**Envelope.** `version | Secure | sigilId | counter (4) | AES-128-CCM(packet) | tag (8)`.
The nonce combines direction, Sigil ID, counter and session; the receiver
requires a rising counter. Overhead is 15 bytes: control packets become 22,
picker pages 66, `GameDisplay` 125 (ESP-NOW allows 250). Sealing happens on
Atlas's application task so counters go out in order. Anything else from a
paired MAC (cleartext, a failed tag) is dropped and counted, never logged
with key material.

**Code.** `shared/include/secure_link.h` (envelope, nonce, counters),
`pairing_v2.h` (pairing state machines), `secure_session.h` (sessions),
`secure_link_mbedtls.h` (mbedTLS: X25519, AES-CCM, HMAC-SHA256, all in the
Arduino core, Apache-2.0). Host tests run the logic through a test crypto
backend (`Sigil/tests/host/secure_link_scenarios.cpp`,
`pairing_scenarios.cpp`); firmware runs a known-answer self-test at boot
(`SECURE_LINK|SELF_TEST|PASS|<ms>`): RFC 7748, HMAC and CCM vectors, then one
packet sealed with a pair key derived from the known secret. It runs two X25519
multiplications (it ran eight and took about 1.6 s before 2026-10-06; the
new time is *Needs verification* from the boot log).

**Limits.** NVS isn't flash-encrypted, so someone holding a board can read
its keys; physical extraction is out of scope for now. Keys are erased by
forget, unpair and factory reset.

## Status

Pairing, re-pairing, forget, factory reset and the sealed link are verified
on hardware (bench 2026-09-29; owner 2026-10-02). Still worth a deliberate
bench run: Reject and the 60 s timeout, portal Codes match, eight Sigils at
once, and forged, replayed or cleartext packets being refused.
