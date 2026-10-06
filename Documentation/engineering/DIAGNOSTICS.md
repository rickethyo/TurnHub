# Diagnostics, Logs and Stability

Where to find evidence when Atlas misbehaves, and the state of the
multi-phone stability work.

## Where the evidence is

| Source | What it holds | How to get it |
|---|---|---|
| USB serial (115200) | Everything, including panic backtraces (Guru Meditation), which go **only** here | `pio device monitor` on the board's port |
| RAM log ring (8 KiB) | Every `TurnHub::serialLog` line, redacted | Developer: `GET /api/diagnostics/log` (streamed) |
| SD `diagnostics.log` | The same redacted lines, kept across reboots | Card: `/turnhub/diagnostics.log` plus `.1`-`.3` archives |
| `GET /api/diagnostics` | Heap, largest block, loop stack minimum, SD state, logger state and lost bytes | Developer |
| CI artifacts | `firmware.elf`, `firmware.map`, `stack-usage.tar.gz` (Atlas) for decoding a panic | Actions run of the exact build flashed; kept 7 days |

Atlas code logs through `TurnHub::serialLog` (`serial_log.h`), never
`Serial.print*`, so every line reaches the RAM ring and the card. Keep secrets
out with `printlnRedacted`. Framework `log_e` output does not reach the ring
yet (staged).

## SD diagnostics log

- A low-priority worker drains the RAM ring to the card once a second in
  1 KiB pieces; the gameplay loop never writes the log. If the ring overruns,
  the next write carries a lost-byte marker.
- `diagnostics.log` plus three archives, each at most 256 KiB (1 MiB total).
  Each boot appends a marker with firmware, reset reason and a random boot ID.
  Timestamps are uptime, not wall-clock.
- An absent card or a failed self-test disables logging until a usable card
  mounts (hot-plug: [Storage and Recovery](STORAGE_AND_RECOVERY.md)). The
  worker never formats or deletes outside its four files. An unexpectedly
  large current log is kept and logging stops.
- Power loss can lose buffered text or damage FAT metadata; logs are best
  effort. Anyone with the card can read identifiers and game activity in it
  (not statistics or secrets).
- Future card users must serialize with this worker through `sd_card.cpp`.

Open: rotation on a real card and responsiveness during slow writes
(**D05**); statistics across a card swap during a game (**D04**).

## Health and HTTP tracing

- `ATLAS|HEALTH` lines log heap, largest free block, Wi-Fi clients and the
  loop stack minimum every minute and on each phone join/leave. Boot logs
  firmware, boot ID, configured loop stack and reset reason.
- `loopStackMinimumFreeBytes` is the historical minimum free stack
  (`uxTaskGetStackHighWaterMark`, bytes), not current usage. A warning is
  logged below 3072 bytes, at most every 30 s.
- `TURNHUB_HTTP_TRACE=1` in `Atlas/platformio.ini` traces state, seats,
  counters, session and status requests (BEGIN/END, route, duration,
  watermark; never tokens or bodies). It is **off** since 2026-10-06 because
  with several phones it crowds older evidence out of the logs; turn it on for
  a bench capture.

## Multi-phone stability (in progress)

**Symptom.** With two to four phones polling, Atlas has panicked or hung
(playtests 2026-09-21, 2026-09-29; a 73 s ESP-NOW transmit stall with
`ESP_ERR_ESPNOW_NO_MEM`). Sigil-only games run for 40+ minutes. Heap was low
(about 48 KB) before the 2026-09-30 RAM reduction doubled it; loop stack
minima were as low as about 1.2 KB of 8 KB.

**Done (Atlas 0.6.7, 2026-10-03):** a 16 KiB loop task stack
(`SET_LOOP_TASK_STACK_SIZE`, `AtlasConfig::LOOP_TASK_STACK_BYTES`), the
tracing and stack warnings above, and smaller status/counter handler and
checkpoint working sets. Atlas 0.6.6 also froze during OTA, likely the same
8 KB stack overflowing during the signature check; 0.6.7 fixes that too, but
a board stuck on 0.6.6 needs a USB flash.

**Remaining stages** (full plan:
[2026-10-02 stability plan](history/2026-10-02-stability-plan.md)):

1. Bench run of the 0.6.7+ build with **unchanged** client polling: two
   active phones for 30 minutes and 100 alternating passes, USB serial
   capturing any panic, stack minimum at least 3 KB, heap trend flat. Decode
   any panic with that build's ELF. (**C06**)
2. Then the wider matrix: four and eight clients, mixed Sigils and phones for
   two hours, join/leave and session expiry during play, slow clients,
   recovery and OTA.
3. Only after measuring: cache seat metadata with an explicit revision, and
   have the app and portal refresh seats on that revision instead of every
   poll (one request in flight, backoff, quieter in the background).

Report the outcome as a confirmed root-cause fix, a validated mitigation with
the cause unknown, or still failing. A larger stack helping supports the
stack theory; it doesn't prove it.

## Capturing logs for the record

Bench captures that back a *Verified* claim go in
[history/logs](history/logs/README.md): name them
`YYYY-MM-DD-<what>-<board>.log` by role (`atlas`, `sigil-eink`,
`sigil-oled`), never by COM port, and replace passwords, keys, tokens and real
MAC addresses before committing.
