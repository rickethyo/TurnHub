# Atlas intent migration verification

Historical branch: `atlas-esp32-port`. Verification date: 2026-09-19 (local time).
Merged into `master` in `6844f94`. On 2026-09-20 the owner confirmed the repository
build and current installed hardware baseline. The records below describe that
migration; they do not validate later identity/storage changes.

## Scope and ownership

Atlas remains the only owner of game, lobby, and decision state. `IntentDispatcher`
binds exactly one handler per implemented intent. The application handlers in
`Atlas/src/main.cpp` (since split into the modules listed in `Atlas/include/atlas_app.h`) validate requests and call the existing game/lobby operations;
the engine's rules and statistics completion callback are unchanged.

The six architectural gate answers for this migration are:

1. Canonical owner: Atlas's existing GameEngine, Lobby, and application decision state.
2. Requests: the intent mapping below, including deferred Atlas transitions.
3. Validator: the bound Atlas handler, followed by existing domain validation.
4. Persistence: no new game persistence; existing Atlas profile/statistics owners remain.
5. Clients: physical Sigils and browser controls submit requests and render Atlas output.
6. Contract: internal intent vocabulary/payload documentation expands; ESP-NOW packet
   values/layout and HTTP endpoints do not change. The future JSON envelope is not a
   live ingress path for these internal operations.

| Control or transition | Authoritative intent |
| --- | --- |
| Physical/browser/Atlas master PASS | Pass |
| Action cancels pending PASS | CancelPass |
| Grace timer expires | CommitPass (Atlas System only) |
| Physical pause/resume; browser toggle | Pause / Resume / TogglePause |
| Browser concession | Concede |
| Physical/browser victory decision | ClaimWin / ConfirmWin / DenyWin |
| Primary module and secondary seat membership | Join / Leave |
| Exact browser seat, physical seat cycle, host random choice | SelectStarter |
| Host hold/release; any module cancels countdown | ArmStart / StartGame / CancelStart |
| Countdown expires | CompleteStart (Atlas System only) |
| Host game-over short/long action; armed lobby reset | Rematch / ResetGame |
| Paused elimination selection/cycle/cancel/confirmation | BeginElimination / CycleElimination / CancelElimination / Eliminate |

No new UI or leave gesture was added. The existing secondary-seat chord issues
Join or Leave; whole-module Leave is now available at the authoritative boundary.
Button held/long/chord/suppression fields remain adapter bookkeeping, not game rules.
Audio/LED invalidation remains adjacent to authoritative transitions for now.
Profile/device naming, authentication sessions, network settings, and statistics
persistence keep their existing owners; they are not alternate game-state mutators.

## Behaviors deliberately retained

- Claimant must be the active living seat; confirmations/denials follow Atlas's
  required player order, including two seats on one physical Sigil.
- Denial resumes a running claim or a completed armed pause-to-claim gesture.
  An ordinary browser claim made while paused stays paused on denial.
- Physical elimination keeps a surviving game paused. Concession restores running
  play only if it was running before concession. Both retain existing winner logic.
- Countdown requires the host and at least two players. Any discovered module can
  cancel it; any module can cancel a selected elimination, as before.
- Rematch retains joined seats; reset empties the lobby.
- PASS still uses a three-second grace interval and the original request timestamp
  for committed turn statistics, with unsigned timer rollover arithmetic.
- Future life/counters, nudges, and actual device pairing remain unimplemented.
  The follow-up below adds only a visual mock PairRequest handler.

## Optional Preferences/NVS reads

`OptionalPreferences` probes the requested NVS string/blob type. Only
`ESP_ERR_NVS_NOT_FOUND` returns the existing default without a framework error.
Other failures go through Preferences error reporting. Optional deletion likewise
treats absence as success and reports erase/commit failures. No log level is lowered.
This covers profile names, PINs, legacy name/PIN keys, bindings, device names,
statistics lengths, and the optional AP password.

No negative cache is introduced: saving a previously missing value is visible on
the next read. Existing migration, storage layout, and defaults are retained.

## Completed validation

- **Clean ESP32 firmware build: PASS.** `platformio run -d Atlas -t clean`, then
  `platformio run -d Atlas`, using installed Espressif32 7.1.3, Arduino ESP32
  `4.20017.260907+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5`.
  RAM: **47,908 / 327,680 bytes (14.6%)**. Flash: **924,281 / 1,310,720 bytes (70.5%)**.
- **Native regression executable: PASS, all six scenario groups.** MSVC
  14.51.36231, C++17, assertions enabled, `/W4`, no compiler warnings. Compiles
  actual application handlers/adapters, GameEngine, Lobby, and IntentDispatcher.
  Hardware, transport, presentation, clock, and NVS are stubbed.
- **Adapter audit: PASS.** `Atlas/tests/host/audit_adapters.py` checks 12 current
  input/transport/timer functions for direct canonical mutators and transition calls.
- **Whitespace validation: PASS.** `git diff --check`.

Native tests cover invalid/stale actors, duplicate binding refusal, unsupported
intents, shared seat ordering, early/out-of-order responses, game completion once,
host restrictions, countdown cancellation, rematch/reset, elimination conflicts,
concession pause restoration, PASS cancellation/grace/rollover, and NVS injected
NOT_FOUND/type/handle/erase/commit failures. These are not hardware persistence tests.

<a id="hardware-verification-still-required"></a>

## Hardware verification — user reported complete

- [x] Flash Atlas only; confirm all implemented intents log BOUND and normal Wi-Fi,
  ESP-NOW, web, front-panel LEDs, and both physical Sigils still initialize.
- [x] Poll portal/status/profile/statistics views with unnamed seats and no PINs;
  verify repeated NOT_FOUND messages are gone. Save/clear/reload values and reboot
  to verify persisted values and legacy migration still work.
- [x] Exercise physical, browser, and master-button PASS; wait three seconds,
  cancel with PASS and Action, and verify turn audio including shared-seat passes.
- [x] Test physical/browser pause/resume and mixed-interface win confirmations and
  denials, including a paused browser claim, armed physical claim, and shared seats.
- [x] Exercise join and secondary join/leave, starter cycle/exact/random selection,
  host countdown, cancellation, start, game-over rematch, and empty reset.
- [x] Test elimination target cycle/cancel/confirm versus running/paused concession;
  verify winner, statistics, LEDs, and sounds once per completed game.
- [x] Recheck browser authentication, OTA gating/partition behavior, and reconnect.

The prior conversation reports physical PASS timing/front-panel success on the
pre-migration build. That evidence does not count as hardware validation of this patch.

## Follow-up: front-panel LED prototype

After supplying the hardware run above, the user confirmed that all remaining
gameplay checks were tested. This is user-reported acceptance; no additional
serial capture was supplied for those checks. The original automated build
figures above refer to the intent migration before the LED follow-up.

The LED follow-up binds PairRequest to an explicitly mock, Atlas-button-only
handler: five seconds of pairing LED flashes, automatic exit, no actual pairing
or persistence. Status flashes three times after startup and then stays on.

LED follow-up validation: PlatformIO Atlas build passed (47,924 bytes RAM,
924,797 bytes flash); all six existing native scenario groups and the adapter
audit passed. These automated checks do not verify physical LED appearance.

- [x] Owner accepted the current build/hardware baseline on 2026-09-20, including
  the checked-in LED prototype. This is user-reported acceptance, not a new
  instrumented capture or verification of real pairing (still unimplemented).
