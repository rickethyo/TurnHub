# TurnHub stabilization and client foundation — 2026-09-22

Performed on the existing `master` branch from a clean worktree. No new branch,
dependency, hardware assignment, Android project or Pi integration.

## Audit findings

Inspected Atlas/Sigil sources and headers, HTTP/session adapters, Intent routing,
GameEngine, lobby/profile/storage code, pairing and radio/display synchronization,
shared schemas, Android architecture, host scenarios and PlatformIO configuration.

- Gameplay adapters already converge on Atlas's dispatcher. The documented
  versioned snapshot/Intent routes and revisions were not implemented.
- Atlas already compares display snapshots before transmission and uses separate
  transmit work. Hello-driven invalidation remains necessary recovery behavior.
- Sigil deduplicates game snapshots, but repeated completed profile names still
  marked the display dirty. Settings/policy saves already skip identical values;
  profile/device names and PIN hashes did not.
- Display name synchronization created temporary sanitized Strings for sends and
  logs. Account revocation allocated Strings for fixed-buffer token comparisons.
- Wokwi's C++ shim was force-included in dependency/framework sources, breaking
  that build. Project-only source flags correct its scope.
- Large implementation units remain; broad extraction was not justified. The
  new read model is isolated in `client_state.h/.cpp`. Checkpoint codec scaffolding
  is not wired into the current main loop; it was retained without activation.
- Mirrored radio headers and legacy capability paths remain unchanged rather than
  assuming compatibility can be removed without a firmware rollout decision.

## Cleanup and resource tradeoffs

- Compare bounded rendered Sigil names, including shortening, clearing and
  truncation. Repeated deliveries still complete synchronization and wake the
  worker, but do not independently request an e-ink refresh. Layout is unchanged.
- Use 13-byte stack buffers for Atlas's sanitized display names and direct token
  comparison for revocation. Preserve radio packets, retries, profile requests,
  Hello recovery and numeric protocol values.
- Skip writes of identical non-empty profile/device names and PIN hashes. Changed
  values and empty-value/remove semantics retain their existing storage paths.
- The heap-free gameplay read model uses **2,732 bytes** of static Atlas RAM
  (ELF symbol size), plus small metadata/callback storage. This pass is not a net
  firmware RAM reduction. The no-op expiry loop avoids rebuilding the Commander
  matrix. HTTP serialization reserves space for non-zero Commander entries.
- No new loop delays, blocking logging or telemetry dependency was introduced.

## API and Android groundwork

- Added public `GET /api/v1/info`: identity, firmware/API/radio versions, boot
  epoch, gameplay revision and capability flags. No credentials or secrets.
- Added `GET /api/v1/state`: one authoritative gameplay projection with life,
  Commander damage, pending decisions, participant handles and sampled clocks.
- Existing authenticated controls still resolve the actor and dispatch Intents.
  Results include status/revision/boot ID after dispatch. Optional expected
  revision/boot fields reject stale session-control requests before dispatch.
- Generic `/api/v1/intent`, events and request deduplication remain unsupported,
  explicitly advertised as such. Native PASS uses `/api/control/pass`. Specialized
  life/counter/profile/settings endpoint contracts remain unchanged.
- Added schemas, seven response fixtures, a PASS request descriptor, live HTTP and
  reconnect documentation, and updated the Android foundation milestone.
  See [the complete contract](../../protocol/http-v1.md).

## Validation

- Baseline host suite passed before edits.
- `run-gcc.ps1`: gameplay, storage and real profile-store executables all pass.
  New tests cover revision changes/no-ops, stale boot/revision and malformed input,
  session authorization, PASS grace/commit, reconnect, Commander/life approval,
  expiry rollover, full 16-player snapshots, bounded names and redundant writes.
- `check_client_contract.py`: eight generated responses and seven shared fixtures
  pass schema/reference checks, using only Python's standard library.
- `audit_adapters.py`: all 17 audited adapters retain dispatcher-only mutation.
- `portal_smoke.cjs` and `counter_smoke.cjs`: pass in Edge via Playwright, including
  responsive layout, life controls, two-browser approval and reconnect. These use
  local fixtures, not real Atlas hardware.
- PlatformIO Atlas: pass; static RAM **73,860 bytes (22.5%)**, flash
  **1,042,357 bytes (79.5%)**.
- PlatformIO production Sigil: pass; RAM **48,336 bytes (14.8%)**, flash
  **769,081 bytes (58.7%)**.
- PlatformIO Sigil Wokwi: pass after correcting shim flag scope; RAM
  **48,320 bytes (14.7%)**, flash **793,737 bytes (60.6%)**.
- Final diff, whitespace and scope reviewed. No deleted legacy architecture
  reintroduced. No firmware flashed or physical validation claimed.

## Preserved behavior and next step

Gameplay rules, PASS grace/cancellation, life/Commander calculations, pairing,
GPIOs, AP/browser UI, profile ownership, recovery behavior and display design
remain intact. No Android-specific rules, BLE or Raspberry Pi integration.

Next: implement the Android protocol adapter and `AtlasRepository` with fake
transport tests using the fixtures; then a minimal Compose/ViewModel flow for
manual endpoint, info, login/join, snapshot, PASS and reconnect. Compare the
identity/boot/revision tuple, handle 401/409 and never replay ambiguous PASS.

## Files changed

- `Android/README.md`
- `Atlas/include/client_state.h`
- `Atlas/include/intent_dispatcher.h`
- `Atlas/include/web_api.h`
- `Atlas/src/client_state.cpp`
- `Atlas/src/intent_dispatcher.cpp`
- `Atlas/src/main.cpp`
- `Atlas/src/profile_store.cpp`
- `Atlas/src/sigil_bus.cpp`
- `Atlas/src/web_api.cpp`
- `Atlas/tests/host/README.md`
- `Atlas/tests/host/check_client_contract.py`
- `Atlas/tests/host/profile_store_scenarios.cpp`
- `Atlas/tests/host/run-gcc.ps1`
- `Atlas/tests/host/run.cmd`
- `Atlas/tests/host/scenarios.cpp`
- `Documentation/engineering/STABILIZATION_2026_09_22.md`
- `Sigil/include/display_name.h`
- `Sigil/include/sigil_display.h`
- `Sigil/platformio.ini`
- `Sigil/src/main.cpp`
- `Sigil/src/sigil_display.cpp`
- `protocol/README.md`
- `protocol/control-result-v1.schema.json`
- `protocol/examples/commander.response.json`
- `protocol/examples/conflict.response.json`
- `protocol/examples/info.response.json`
- `protocol/examples/lobby.response.json`
- `protocol/examples/pass-result.response.json`
- `protocol/examples/pass.request.json`
- `protocol/examples/reconnected.response.json`
- `protocol/examples/running.response.json`
- `protocol/http-v1.md`
- `protocol/info-v1.schema.json`
- `protocol/state-v0.1.schema.json`
