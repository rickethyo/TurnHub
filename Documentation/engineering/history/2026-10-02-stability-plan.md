# TurnHub staged stability changes

Date: October 2, 2026
Status: Implementation started October 3; first changes and remaining gates
are recorded below. No hardware validation completed for these changes.

## Objective and evidence

Make Atlas resilient to multiple active phone clients while preserving responsive Sigil gameplay, recovery, SD logging, and OTA. Implement in small, independently reviewable changes so a successful mitigation does not obscure the original fault.

The supplied investigation reports two panic resets during two-phone gameplay, a roughly 40-minute stable Sigil-only game, healthy heap near the failures, and loop stack high-water marks as low as approximately 1.2 KB. These observations prioritize the HTTP/session/state paths and stack pressure, but do not identify the crashing instruction. A panic reset alone does not prove stack overflow. Improved behavior with a larger stack would support the hypothesis without proving it.

Endpoint names, polling behavior, and framework assumptions below come from the supplied investigation. Confirm them against the implementation checkout before editing. The repository was not available in this workspace for this write-up.

## Stage 0: Preserve a reproducible baseline

- [ ] Record firmware commit, build environment, portal version, Android version, SD contents/version, and phone roles.
- [ ] Preserve the exact ELF/map and raw USB serial output for each test build, so panic addresses can be decoded against the correct binary.
- [ ] Reproduce with two authenticated phone participants alternating passes, with current polling unchanged. Record request rates and actions.
- [ ] Record controls: Sigils only, one phone plus one Sigil, and two associated phones with only one active client.
- [ ] Capture Guru Meditation text, exception registers, and backtrace when possible. An SD diagnostic log is supplementary and may lose buffered tail records.

Exit: a documented reproduction procedure and preserved baseline evidence. Do not delay headroom work indefinitely if the crash cannot immediately be reproduced.

## Stage 1: Add targeted diagnostics

Proposed commit: `Atlas: instrument HTTP resource pressure and reset evidence`

- [ ] Log boot ID, reset reason, firmware identity, configured loop stack size, heap, largest free block, and loop stack watermark.
- [ ] Instrument `/api/v1/state` and `/api/seats` with request ID, route, BEGIN/END, duration, and task stack watermark. Add session/profile/storage substeps only where needed.
- [ ] Distinguish the historical minimum remaining stack from current available stack. Entry/exit watermark changes indicate a new low, not the exact deepest call or current stack consumption.
- [ ] Verify stack metric units for the pinned ESP32 framework before naming fields in bytes.
- [ ] Retain a rate-limited low-stack alert, initially targeting a 3 KB minimum on a 16 KB loop stack. Treat this as an initial engineering target, not a proven safe boundary.
- [ ] Keep instrumentation allocation-light and bounded. Never log credentials, session tokens, profile PINs, or request bodies containing secrets.
- [ ] Use existing safe diagnostics infrastructure. Do not add SD writes, long locks, or large formatting buffers inside every HTTP instrumentation hook.

Exit: handler activity is traceable, low-water alerts work, and diagnostics do not materially degrade gameplay. Preserve raw panic capture independently of application logging.

## Stage 2: Increase loop stack headroom

Proposed commit: `Atlas: provision a 16 KB loop task stack`

- [ ] Confirm actual default stack size and the supported override mechanism in the pinned Arduino-ESP32 core.
- [ ] Set the loop task to 16 KB using the supported mechanism, defined once. Confirm the resulting configuration at boot.
- [ ] Build all affected Atlas environments and repeat the exact two-phone reproducer without changing client polling.
- [ ] Compare minimum stack margin, heap, largest block, handler duration, and resets against baseline.
- [ ] Verify startup, Sigil interaction, recovery, and OTA still have adequate memory.

Exit: configuration is verified and resource tradeoffs are measured. Keep this separate from structural refactors. If the panic persists, use its decoded trace to direct the next fix rather than continuing to increase stack blindly.

## Stage 3: Reduce measured handler stack demand

Proposed commit: `Atlas: reduce HTTP handler temporary working sets`

- [ ] Inspect state, seats, session, profile, JSON, and storage call chains. Use compiler stack-usage output where supported alongside runtime measurements.
- [ ] Move large automatic buffers or structures to explicitly owned, bounded storage where justified. Account for nested calls and error paths.
- [ ] Avoid repeated object copies, redundant storage reads, and deep helper chains in the measured hot path.
- [ ] Reuse bounded response workspaces only when their lifetime and synchronization are clear. Do not make every large local static or introduce unsafe sharing with workers/callbacks.
- [ ] Handle allocation, response-capacity, and storage failures explicitly. Check controller IDs, session expiration, and collection bounds, including virtual controllers 8 and 9.
- [ ] Review SD portal streaming lock duration if evidence implicates it. Do not broadly redesign SD serving based solely on its recency.

Exit: representative requests have reduced measured stack demand, identical response semantics, and safe failure behavior. If the actual fault is invalid access or another bug, fix it explicitly and retain these bounded-resource improvements where useful.

## Stage 4: Cache seat metadata safely

Proposed commit: `Atlas: cache seat metadata with explicit invalidation`

- [ ] Separate seat/profile metadata changes from turn, clock, life, and other game-state revisions.
- [ ] Add a seat metadata revision or equivalent explicit invalidation
  mechanism, updating the API contract, fixtures and all current clients.
- [ ] Invalidate for join/leave, seat movement, binding/unbinding, profile name/avatar changes, relevant session/auth changes, reset, and recovery restore.
- [ ] Cache only data that is safe to share. Keep viewer-specific authorization, PIN state, and private account fields outside a global shared response, or key and bound caches appropriately.
- [ ] Prepare a bounded snapshot and publish it consistently. Keep game state authoritative and avoid rebuilding the same unchanged metadata for every phone.
- [ ] Define fallback behavior for cache failure and stale data. Do not introduce unbounded per-session caches.

Exit: unchanged metadata is reused, membership/profile changes appear promptly, and one user cannot receive another user's private fields.

## Stage 5: Reduce unnecessary client requests

Proposed commit: `Android/portal: refresh seats on metadata changes`

- [ ] Retain approximately one-second live-state polling initially, so server-side fixes are evaluated under the original workload.
- [ ] Refresh seats on initial connection and seat metadata revision changes, with a periodic fallback. A pass alone should not rebuild seat metadata.
- [ ] Update Atlas, Android and the portal together. The repository's standing
  decision is no compatibility before release; do not introduce older-Atlas
  fallback code. A periodic refresh still protects against missed invalidations.
- [ ] Permit only one in-flight polling cycle per client. Bound timeouts, cancel obsolete requests, and back off on failures.
- [ ] Stop or reduce polling when the client is backgrounded/disconnected. Add modest jitter to reduce synchronized request bursts.
- [ ] Inspect both Android and browser portal behavior before applying equivalent changes where relevant.

Exit: turn/clock updates remain responsive, seat changes appear promptly, and request counts fall without hiding a server fault. Atlas must also survive the original polling pattern.

## Stage 6: Stress validation and release gate

Proposed commit: `Test: add repeatable Atlas multi-client stability scenarios`

| Scenario | Proposed minimum run | Required result |
| --- | --- | --- |
| Two active phone participants, original polling | 30 minutes and 100 completed alternating passes | No unexpected reset; correct turn ownership |
| One phone plus one Sigil | Full game | Consistent state and responsive inputs |
| Sigils only | Full game | No gameplay regression |
| Four, then eight active clients where supported | 30 minutes per level | No unexpected reset, bounded memory, usable gameplay |
| Mixed Sigils and active phones | Two-hour soak | No progressive resource loss or stale seat metadata |
| Join/leave, session expiry, profile edits, seat movement | Repeated cycles during gameplay | Correct invalidation and access control |
| Slow/disconnected client and portal asset loading | During active polling/gameplay | No prolonged starvation or unsafe resource retention |
| Controlled restart and recovery | Active match | Match restored correctly in paused state |

Use simulated clients for repeatable HTTP stress, but validate with real phones too. Several sessions from one computer exercise HTTP/session load, not multiple physical Wi-Fi station behavior. Only legitimate active-player actions should change turns; observers contribute read load.

- [ ] Run the repository's relevant existing checks and all affected builds. Add focused tests for cache invalidation, session isolation, bounds, and client polling behavior.
- [ ] Preserve build identities, decoded crash traces if any, request counts, maximum handler durations, heap trends, largest free block, and stack watermarks with results.
- [ ] Require no unexpected panic/watchdog/brownout/reset in the matrix and at least the provisional 3 KB loop stack margin on the 16 KB configuration.
- [ ] Establish latency expectations from baseline and check that HTTP load does not materially delay Sigil inputs or turn commits.
- [ ] Retain low-stack warnings in release diagnostics; reduce detailed request tracing after validation.
- [ ] Mark untested scenarios explicitly. Hardware validation is required before declaring the two-phone crash resolved.

## Delivery and rollback

Implement each stage in a separate commit or small reviewable change. Keep diagnostic and stack provisioning changes independently revertible. Preserve a known-good flashable artifact, portal pack, Android build, and recovery-data compatibility notes for each milestone. Avoid unrelated feature changes during this work.

Stages 1 and 2 are the first stability build. Stage 3 follows measured evidence; stages 4 and 5 address recurring work and client load. Stage 6 is the release gate, not a substitute for capturing the original panic. Report the final outcome as confirmed root-cause fix, validated mitigation with cause unresolved, or still failing, according to the evidence.

## First implementation — October 3, 2026

Baseline checkout: `d7eb2a7` (Atlas 0.6.6). Development version: Atlas 0.6.7.
This is partial implementation of stages 1–3; none of their hardware exits is
accepted yet. Stages 0, 4, 5 and 6 still require their work and evidence.

### Framework facts verified from source

PlatformIO pins espressif32 7.1.3 / Arduino-ESP32 2.0.17. That core's
[`main.cpp`](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/main.cpp)
uses `getArduinoLoopTaskStackSize()` to create loopTask, with an 8192-byte
fallback. Its
[`Arduino.h`](https://github.com/espressif/arduino-esp32/blob/2.0.17/cores/esp32/Arduino.h)
provides `SET_LOOP_TASK_STACK_SIZE`. Atlas overrides it once in `main.cpp`,
using `AtlasConfig::LOOP_TASK_STACK_BYTES` (16384). Verify the resolved SDK's
previous configured size when comparing the actual baseline build.

The bundled
[`task.h`](https://github.com/espressif/arduino-esp32/blob/2.0.17/tools/sdk/esp32/include/freertos/include/freertos/task.h)
documents `uxTaskGetStackHighWaterMark` as historical minimum free **bytes**.
Diagnostics now use `loopStackMinimumFreeBytes`, replacing the misleading
`loopStackFreeBytes`; zero in host fixtures means no measurement. This is not
a current stack-pointer measurement. No claim of current free stack is made.

### Changes

- Boot logging records firmware/build, public boot ID, configured stack size,
  trace setting, reset reason and resource figures. On ESP32 the configured
  size is read from the same function the core uses for allocation.
- The API route table traces state, seats, counters and session/me BEGIN/END
  with a monotonic per-boot request sequence, constant route, elapsed
  milliseconds and calling-task watermark. Administrative requests and log
  downloads are excluded to avoid diagnostics tracing their own snapshots.
  `/api/status` is traced separately because it bypasses that table. Query
  arguments, tokens and bodies never enter these logs. END also reports
  whether the watermark set a new low, not the precise deepest call.
- HTTP completion and periodic health sampling retain a low-stack warning
  below 3072 bytes, at most once per 30 seconds. This is a provisional target.
  The hooks use the existing RAM/serial logger and perform no SD writes.
- Detailed tracing is enabled by `TURNHUB_HTTP_TRACE=1`. It was on in the
  first stability builds and was turned off (`0`) on 2026-10-06 after Atlas
  0.6.7 was flashed, because with several phones polling it crowds older
  evidence out of the RAM and SD logs. Set it back to `1` in
  `Atlas/platformio.ini` for a bench capture; warnings remain either way.
  Compare tracing on/off on the bench because serial output itself has a cost.
- PlatformIO emits compiler `.su` stack-usage reports beside object files.
  Preserve these, `firmware.elf`, the map and resolved package versions.
- The 1024-byte status formatting buffer now has a documented loopTask-only
  lifetime, with truncation checked before sending JSON. Counter snapshots
  use a separate loopTask-owned workspace held through response transmission;
  nested use fails with 503. The authoritative callback clears/rebuilds the
  snapshot for each viewer. The counter serializer avoids long temporary
  String chains, reserves for the actual bounded result, rejects oversized
  snapshots and returns 503 if reservation fails.
- Polling, radio packets and gameplay semantics remain the baseline workload.
  `/api/seats` combines metadata with live life/active/eliminated fields; stage
  4 must cache metadata separately rather than freeze that entire response.
  The portal polls status/seats/session/counters; Android polls v1 state and
  refreshes seats on revision changes or periodically. Both already guard
  their primary refresh loops. Preserve Android background turn notifications
  when reducing background requests in stage 5.

### Resource evidence and remaining checks

The first GitHub firmware build passed all seven CI jobs. Its ESP32 compiler
reports 96 B for `handleCounters`, 240 B for `handleStatus`, but also revealed
528 B in `readCounters`, 2768 B in `GameEngine::checkpoint`, and 2816 B in
`decodeCheckpoint`. The latter three clear an existing workspace through a
large aggregate temporary. The measured follow-up replaces those assignments
with in-place field/array resets, preserving settings and player-seat defaults,
the record schema and completion ordering. Recovery tests exercise reused
scratch storage and empty-match defaults. The subsequent CI build validates
these follow-up changes; use its artifact for the bench.

GCC host `-Os -fstack-usage`, real source with host stubs, before/after:

| Function | Before | After |
| --- | --- | --- |
| `handleCounters` | 1280 B | 160 B |
| `handleStatus` | 1376 B | 352 B |

These are individual host frame sizes, not nested ESP32 stack demand; the
status frame is bounded dynamic stack usage. Measure the actual target `.su`
reports and runtime watermark before accepting the margin. The working sets
add about 1.5 KiB of static RAM; the loop stack adds 8 KiB of allocated RAM
relative to an 8 KiB baseline. Firmware RAM/flash and actual heap cost remain
unmeasured until a PlatformIO build and bench run.

First bench procedure: preserve baseline and new ELF/map, flash the first
stability build, confirm `loopStackSizeBytes=16384`, then run two real active
phones with unchanged polling for **both** 30 minutes and 100 committed
alternating passes. Capture raw USB panic output independently of SD logs.
Record stack minima, heap/largest-block trends and latency; compare Sigil-only
and mixed play. Decode any panic with that exact ELF. Proceed to the rest of
the stage 6 matrix, including recovery paused and OTA, before acceptance.

Local validation: all four Atlas host executables pass with AddressSanitizer
and UBSan, both with tracing disabled and with `TURNHUB_HTTP_TRACE=1`. New
scenarios check viewer isolation in the reused counter workspace, reserve
failure and release, oversized snapshot rejection, warning rate limiting and
uptime rollover. The adapter audit and generated/shared client-contract
checks pass. LeakSanitizer was disabled locally because the sandbox prevents
its process inspection; GitHub CI retains its normal leak checking. Browser
smoke could not bind its localhost fixture server in this sandbox (`EPERM`).

GitHub CI builds the review branch, and its Atlas artifact now preserves the
ELF, map, resolved build identity and `.su` archive alongside firmware and the
signed OTA package when the signing key is available. Download the exact
artifact used for the bench before its seven-day retention expires.

An improved soak is evidence for mitigation, not proof of the crashing
instruction. Framework UART/panic capture, request rates, current-stack
inspection, latency measurements and real Wi-Fi stations remain bench work.
